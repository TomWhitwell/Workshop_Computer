#!/usr/bin/env python3
"""
Constancy's morning, baked into a C header.

The card replays one dawn: 17 April 2024 at Calamansac on the Helford River,
Cornwall, the last day of the Music Thing Modular residency there. This
script gathers that morning's sun, tide, wind and cloud, scales each to the
full 0..1 range across the window, and writes them out as src/DayData.h: a
small table the card reads with the Time knob.

    python3 tools/data/build_data.py            build from the saved data
    python3 tools/data/build_data.py --fetch    download it again first

Only the Python standard library is used.

Where the numbers come from
  Sun elevation   calculated here (NOAA's solar position equations), for
                  every point in the table.
  Tide height     Open-Meteo Marine API, "sea_level_height_msl", hourly.
                  The nearest sea cell in its model is about 15km south of
                  Calamansac, off the Manacles -- the tide's timing there is
                  within minutes of the Helford's.
  Wind, cloud     Open-Meteo historical weather archive, hourly.
  Weather data by Open-Meteo.com (https://open-meteo.com/), CC BY 4.0, from
  DWD (Deutscher Wetterdienst) and other national weather services.

The window runs from astronomical dawn (sun 18 degrees below the horizon) to
the moment the sun is 18 degrees above it, about two hours after sunrise:
the four hours in which the light changes fastest.
"""
import datetime as dt
import json
import math
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "raw")
OUT = os.path.join(HERE, "..", "..", "src", "DayData.h")

# Calamansac, on the north shore of the Helford by Port Navas Creek.
LAT, LON = 50.098, -5.137
DATE = dt.date(2024, 4, 17)
BST = dt.timedelta(hours=1)  # British Summer Time: UTC + 1

# The table: this many evenly spaced points from window open to close
# (about 2 minutes apart). The card blends smoothly between neighbours.
POINTS = 128

MARINE_URL = (
    "https://marine-api.open-meteo.com/v1/marine?latitude={lat}&longitude={lon}"
    "&hourly=sea_level_height_msl&start_date=2024-04-16&end_date=2024-04-18&timezone=UTC"
)
WEATHER_URL = (
    "https://archive-api.open-meteo.com/v1/archive?latitude={lat}&longitude={lon}"
    "&hourly=wind_speed_10m,cloud_cover&start_date=2024-04-16&end_date=2024-04-18&timezone=UTC"
)


# ---- The sun -------------------------------------------------------------

def sun_elevation(t_utc):
    """Degrees above the horizon (no refraction) at Calamansac, NOAA method."""
    jd = (t_utc - dt.datetime(2000, 1, 1, 12)).total_seconds() / 86400 + 2451545.0
    T = (jd - 2451545.0) / 36525
    L0 = (280.46646 + T * (36000.76983 + 0.0003032 * T)) % 360
    M = math.radians(357.52911 + T * (35999.05029 - 0.0001537 * T))
    e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T)
    C = (math.sin(M) * (1.914602 - T * (0.004817 + 0.000014 * T))
         + math.sin(2 * M) * (0.019993 - 0.000101 * T) + math.sin(3 * M) * 0.000289)
    omega = math.radians(125.04 - 1934.136 * T)
    lam = math.radians(L0 + C - 0.00569 - 0.00478 * math.sin(omega))
    eps = math.radians(23 + (26 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60) / 60
                       + 0.00256 * math.cos(omega))
    dec = math.asin(math.sin(eps) * math.sin(lam))
    y = math.tan(eps / 2) ** 2
    L0r = math.radians(L0)
    eot = 4 * math.degrees(y * math.sin(2 * L0r) - 2 * e * math.sin(M)
                           + 4 * e * y * math.sin(M) * math.cos(2 * L0r)
                           - 0.5 * y * y * math.sin(4 * L0r) - 1.25 * e * e * math.sin(2 * M))
    minutes = t_utc.hour * 60 + t_utc.minute + t_utc.second / 60 + eot + 4 * LON
    ha = math.radians(minutes / 4 - 180)
    lat = math.radians(LAT)
    return math.degrees(math.asin(math.sin(lat) * math.sin(dec) + math.cos(lat) * math.cos(dec) * math.cos(ha)))


def crossing(degrees, after):
    """First moment (UTC) after `after` that the sun climbs past `degrees`, to the second."""
    lo, hi = after, after + dt.timedelta(hours=8)
    for _ in range(40):
        mid = lo + (hi - lo) / 2
        if sun_elevation(mid) < degrees:
            lo = mid
        else:
            hi = mid
    return hi.replace(microsecond=0)


# ---- Hourly data, smoothed -------------------------------------------------

def fetch(url, name):
    path = os.path.join(RAW, name)
    with urllib.request.urlopen(url.format(lat=LAT, lon=LON), timeout=30) as r:
        data = json.load(r)
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
    print(f"fetched {name}: grid point {data['latitude']:.3f}, {data['longitude']:.3f}")


def hourly(name, key):
    """{UTC datetime: value} from a saved Open-Meteo response."""
    with open(os.path.join(RAW, name)) as f:
        h = json.load(f)["hourly"]
    return {dt.datetime.fromisoformat(t): v for t, v in zip(h["time"], h[key]) if v is not None}


def smooth(series, t):
    """Value at `t` from hourly points, by a Catmull-Rom curve through them:
    it passes through every hourly reading with no corners between hours,
    so neither the sound nor the CV outputs jolt on the hour."""
    base = t.replace(minute=0, second=0, microsecond=0)
    u = (t - base).total_seconds() / 3600
    h = dt.timedelta(hours=1)
    p0, p1, p2, p3 = (series[base + k * h] for k in (-1, 0, 1, 2))
    return 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u
                  + (-p0 + 3 * p1 - 3 * p2 + p3) * u * u * u)


def normalise(values):
    lo, hi = min(values), max(values)
    return [(v - lo) / (hi - lo) for v in values], lo, hi


# ---- Build -----------------------------------------------------------------

def main():
    if "--fetch" in sys.argv:
        os.makedirs(RAW, exist_ok=True)
        fetch(MARINE_URL, "marine.json")
        fetch(WEATHER_URL, "weather.json")

    night = dt.datetime.combine(DATE, dt.time(1, 0))  # 01:00 UTC, well before dawn
    moments = {
        "astronomical dawn": crossing(-18, night),
        "nautical dawn": crossing(-12, night),
        "civil dawn": crossing(-6, night),
        # Sunrise as seen: the sun's upper edge lifted by the air's refraction.
        "sunrise": crossing(-0.833, night),
        "window close": crossing(18, night),
    }
    start, end = moments["astronomical dawn"], moments["window close"]
    span = end - start
    times = [start + span * (i / (POINTS - 1)) for i in range(POINTS)]

    tide = hourly("marine.json", "sea_level_height_msl")
    wind = hourly("weather.json", "wind_speed_10m")
    cloud = hourly("weather.json", "cloud_cover")

    sun_deg = [sun_elevation(t) for t in times]
    tide_m = [smooth(tide, t) for t in times]
    # How fast the tide is running (metres per hour), either way: it slows to
    # nothing as it turns at low water.
    minute = dt.timedelta(minutes=1)
    tide_rate = [abs(smooth(tide, t + minute) - smooth(tide, t - minute)) * 30 for t in times]
    wind_kmh = [smooth(wind, t) for t in times]
    cloud_pc = [min(100.0, max(0.0, smooth(cloud, t))) for t in times]

    # The sun climbs almost evenly through the window, -18 to +18 degrees, so
    # scale it by angle rather than stretching to its exact min and max:
    # 0.5 is then the true horizon.
    sun_n = [(d + 18) / 36 for d in sun_deg]
    tide_n, tide_lo, tide_hi = normalise(tide_m)
    rate_n, _, rate_hi = normalise(tide_rate)
    wind_n, wind_lo, wind_hi = normalise(wind_kmh)
    cloud_n, cloud_lo, cloud_hi = normalise(cloud_pc)

    def q12(v):
        return max(0, min(4096, round(v * 4096)))

    def pos(t):  # table position, Q8 (points x 256)
        return round((t - start) / span * (POINTS - 1) * 256)

    low = min(range(POINTS), key=lambda i: tide_m[i])
    local = lambda t: (t + BST).strftime("%H:%M:%S")

    lines = []
    w = lines.append
    w("// Generated by tools/data/build_data.py -- don't edit by hand; edit and rerun that.")
    w("//")
    w("// The morning of 17 April 2024 at Calamansac, Helford River, Cornwall")
    w(f"// ({LAT:.3f} N, {-LON:.3f} W), from astronomical dawn to the sun 18 degrees up.")
    w("// Times are BST.")
    w("//")
    for name, t in moments.items():
        w(f"//   {name:18s} {local(t)}   table position {pos(t) / 256:6.2f}")
    w(f"//   low water          {local(times[low])}   table position {low:6.2f}")
    w("//")
    w(f"// {POINTS} points, {span.total_seconds() / (POINTS - 1):.1f} seconds apart. Every stream is scaled")
    w("// to 0..4096 across the window (the sun by angle: 0 = 18 below, 2048 =")
    w("// the horizon, 4096 = 18 above). Actual ranges:")
    w(f"//   tide        {tide_lo:+.2f} m to {tide_hi:+.2f} m (mean sea level); a neap tide, falling to low")
    w("//               water then turning")
    w(f"//   tide rate   0 to {rate_hi:.2f} m per hour, either direction")
    w(f"//   wind        {wind_lo:.1f} to {wind_hi:.1f} km/h")
    w(f"//   cloud       {cloud_lo:.0f}% to {cloud_hi:.0f}%")
    w("//")
    w("// Weather data by Open-Meteo.com (CC BY 4.0), from DWD and other national")
    w("// weather services. Sun positions from NOAA's solar equations.")
    w("#pragma once")
    w("#include <cstdint>")
    w("")
    w("namespace constancy")
    w("{")
    w("")
    w(f"constexpr int kDayPoints = {POINTS};")
    w("")
    w("// Where the moments of the morning fall, as table positions x 256.")
    w(f"constexpr int32_t kNauticalDawnQ8 = {pos(moments['nautical dawn'])};")
    w(f"constexpr int32_t kCivilDawnQ8 = {pos(moments['civil dawn'])};")
    w(f"constexpr int32_t kSunriseQ8 = {pos(moments['sunrise'])};")
    w(f"constexpr int32_t kLowWaterQ8 = {low * 256};")
    w("// The sun's level at sunrise as seen (just under the true horizon).")
    w(f"constexpr int32_t kSunriseSunQ12 = {q12((-0.833 + 18) / 36)};")
    w("")
    w("// One point of the morning. All 0..4096.")
    w("struct DayPoint")
    w("{")
    w("\tuint16_t sun;      // elevation")
    w("\tuint16_t tide;     // height")
    w("\tuint16_t tideRate; // how fast it is running, rising or falling")
    w("\tuint16_t wind;     // speed")
    w("\tuint16_t cloud;    // cover")
    w("};")
    w("")
    w("constexpr DayPoint kDay[kDayPoints] = {")
    w("\t// sun  tide  rate  wind cloud    BST")
    for i, t in enumerate(times):
        vals = [q12(sun_n[i]), q12(tide_n[i]), q12(rate_n[i]), q12(wind_n[i]), q12(cloud_n[i])]
        w("\t{" + ", ".join(f"{v:4d}" for v in vals) + "}," + f" // {local(t)[:5]}")
    w("};")
    w("")
    w("} // namespace constancy")

    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {os.path.relpath(OUT)}")
    for name, t in moments.items():
        print(f"  {name:18s} {local(t)}")
    print(f"  low water          {local(times[low])}")
    print(f"  tide {tide_lo:+.2f}..{tide_hi:+.2f} m, wind {wind_lo:.1f}..{wind_hi:.1f} km/h, cloud {cloud_lo:.0f}..{cloud_hi:.0f}%")


if __name__ == "__main__":
    main()
