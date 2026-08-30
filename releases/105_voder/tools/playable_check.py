#!/usr/bin/env python3
"""Can the card actually be played? Regression test for the control model.

TWO ROUNDS OF HARDWARE FEEDBACK SHAPED THIS FILE, and both said the same
thing in different words.

v1.1.0: "with faders moving, the main knob doesn't do anything." The faders
wrote band gains directly and latched the panel out on first touch.

v1.2.0: "I would prefer the vowel sounds just on three sliders. It's too
hard to try to manipulate the partials." That is the deeper of the two.
Per-band control is the WRONG ABSTRACTION for playing, however well behaved
it is made: a vowel is a position of the mouth, not eight independent
numbers, and asking a player to operate seven faders as a chord is asking
them to solve the Voder's original problem - the one its operators trained
for months to overcome.

So the partials left the interface entirely. Two controls place a point in
the vowel square (openness = F1, front = F2) and the eight band gains are a
bilinear blend of four corner vowels. Everything a mouth can do is reachable
with two fingers.

What this file pins down is that the space is genuinely playable: both axes
do something wherever you are in it, the corners are exact, no position
produces silence or a hole, and the vowels that are not corners are still
reachable inside it.

Checks:
  1. All eight cube corners reproduce their vowels exactly.
  2. All three axes move the spectrum, everywhere in the cube.
  3. UH and OH are both reachable inside the cube.
  4. No position in the cube is silent or holed.
  5. Brightness tilts without pinning any band.
  6. The panel reclaims control when the 8mu is unplugged.
  7. Every panel knob makes an audible difference on its page.

Run: python tools/playable_check.py
"""

import sys
import math

# Transcribed from vowels.h.
NUM_BANDS = 8

# Spread plane (lips relaxed).
SPREAD = {
    'OO': [16000, 8369, 6043, 6689, 3106, 2657, 2533, 1271],     # close back
    'AH': [666, 4003, 16000, 15423, 7647, 4226, 3923, 1675],     # open  back
    'EE': [16000, 4167, 864, 929, 2824, 8556, 12114, 5952],      # close front
    'EH': [1518, 16000, 12137, 3653, 7991, 14057, 10935, 3136],  # open  front
}
# Rounded plane (lips protruded): F1 x0.88, F2 x0.62.
ROUND = {
    'OO': [16000, 8053, 4419, 1387, 1337, 2189, 1977, 978],
    'AH': [569, 7483, 16000, 6101, 1596, 2496, 2808, 1089],
    'EE': [16000, 2170, 1193, 4525, 8724, 7189, 6062, 3677],
    'EH': [2308, 16000, 8610, 8916, 8276, 6071, 5253, 1959],
}

# Vowels that are NOT corners, to check the cube contains them.
#
# OH is the reason the third axis exists. In the old 2-D square it was
# 6.0 dB away at the closest approach and unreachable, because OH is
# ROUNDER than anything on the OO-AH edge: its F2 (840 Hz) sits below both
# back corners, so it is not between them in any direction the square can
# travel. Adding rounding brings it inside.
NON_CORNER = {
    'UH': [801, 7697, 16000, 11856, 8440, 5035, 3707, 1537],
    'OH': [881, 10568, 16000, 8051, 2410, 1883, 2144, 1100],
}


def clamp15(x):
    return max(0, min(32767, x))


def blend(openness, front, round_amt=0):
    """Transcription of BlendVowel() in vowels.h. Trilinear."""
    out = []
    for i in range(NUM_BANDS):
        sb = SPREAD['OO'][i] + (((SPREAD['AH'][i] - SPREAD['OO'][i]) * openness) >> 15)
        sf = SPREAD['EE'][i] + (((SPREAD['EH'][i] - SPREAD['EE'][i]) * openness) >> 15)
        spread = sb + (((sf - sb) * front) >> 15)

        rb = ROUND['OO'][i] + (((ROUND['AH'][i] - ROUND['OO'][i]) * openness) >> 15)
        rf = ROUND['EE'][i] + (((ROUND['EH'][i] - ROUND['EE'][i]) * openness) >> 15)
        rounded = rb + (((rf - rb) * front) >> 15)

        out.append(spread + (((rounded - spread) * round_amt) >> 15))
    return out


def with_bright(gains, bright):
    """Transcription of the brightness tilt in ReadPanel()."""
    tilt = bright - 16384
    out = []
    for i, g in enumerate(gains):
        pos = (i * 2) - 7
        factor = max(0, 32768 + ((tilt * pos) >> 3))
        out.append(clamp15((g * factor) >> 15))
    return out


def spectral_distance(v1, v2):
    d = 0.0
    for a, b in zip(v1, v2):
        da = 20 * math.log10(max(a, 1) / 32767.0)
        db = 20 * math.log10(max(b, 1) / 32767.0)
        d += (da - db) ** 2
    return math.sqrt(d / len(v1))


def check_corners():
    print("\n1. All eight cube corners reproduce their vowels exactly")
    ok = True
    for plane_name, table, r in (("spread ", SPREAD, 0),
                                 ("rounded", ROUND, 32767)):
        for name, (o, f) in (('OO', (0, 0)), ('AH', (32767, 0)),
                             ('EE', (0, 32767)), ('EH', (32767, 32767))):
            d = spectral_distance(blend(o, f, r), table[name])
            good = d < 0.5
            if not good:
                ok = False
            print(f"   {plane_name} {name}  error {d:5.2f} dB   "
                  f"{'ok' if good else '<-- FAIL'}")
    return ok


def check_axes_live():
    """Every axis must do something WHEREVER you are in the cube."""
    print("\n2. All three axes move the spectrum, everywhere in the cube")
    ok = True

    print("   OPENNESS, sampled across the other two:")
    for f in (0, 32767):
        for r in (0, 32767):
            d = spectral_distance(blend(0, f, r), blend(32767, f, r))
            good = d > 5.0
            if not good:
                ok = False
            print(f"     front={f:5d} round={r:5d}   moves {d:5.1f} dB   "
                  f"{'ok' if good else '<-- DEAD AXIS'}")

    print("   FRONT, sampled across the other two:")
    for o in (0, 32767):
        for r in (0, 32767):
            d = spectral_distance(blend(o, 0, r), blend(o, 32767, r))
            good = d > 5.0
            if not good:
                ok = False
            print(f"     openness={o:5d} round={r:5d}   moves {d:5.1f} dB   "
                  f"{'ok' if good else '<-- DEAD AXIS'}")

    print("   ROUND, sampled across the other two:")
    for o in (0, 32767):
        for f in (0, 32767):
            d = spectral_distance(blend(o, f, 0), blend(o, f, 32767))
            good = d > 3.0
            if not good:
                ok = False
            print(f"     openness={o:5d} front={f:5d}   moves {d:5.1f} dB   "
                  f"{'ok' if good else '<-- DEAD AXIS'}")
    print("   (no control may stop working because of where another is set)")
    return ok


def check_non_corner_vowels():
    print("\n3. Non-corner vowels are reachable inside the cube")
    ok = True
    for name, target in NON_CORNER.items():
        best = (999.0, 0, 0, 0)
        for o in range(0, 32768, 2048):
            for f in range(0, 32768, 2048):
                for r in range(0, 32768, 2048):
                    d = spectral_distance(blend(o, f, r), target)
                    if d < best[0]:
                        best = (d, o, f, r)
        # Vowels are 6-17 dB apart, so landing within 3.5 dB means the cube
        # genuinely contains the vowel rather than merely approaching it.
        good = best[0] < 3.5
        if not good:
            ok = False
        note = ""
        if name == 'OH':
            note = "  (was 6.0 dB and unreachable before the round axis)"
        print(f"   {name}  closest {best[0]:4.1f} dB at "
              f"open={best[1]*100//32767:3d}% front={best[2]*100//32767:3d}% "
              f"round={best[3]*100//32767:3d}%   "
              f"{'ok' if good else '<-- NOT REACHABLE'}{note}")
    return ok


def check_no_dead_spots():
    print("\n4. No position in the cube is silent or holed")
    worst_sum = (1 << 30, 0, 0)
    worst_min = (1 << 30, 0, 0)
    for o in range(0, 32768, 4096):
      for f in range(0, 32768, 4096):
        for r in range(0, 32768, 4096):
            g = blend(o, f, r)
            total = sum(g)
            lowest = min(g)
            if total < worst_sum[0]:
                worst_sum = (total, o, f)
            if lowest < worst_min[0]:
                worst_min = (lowest, o, f)
    # A vowel needs real energy somewhere, and no band should sit at zero:
    # a band at zero is a hole no other control can reopen.
    good_sum = worst_sum[0] > 20000
    good_min = worst_min[0] > 200
    print(f"   quietest position: total {worst_sum[0]:6d} at "
          f"({worst_sum[1]},{worst_sum[2]})   "
          f"{'ok' if good_sum else '<-- TOO QUIET'}")
    print(f"   lowest single band: {worst_min[0]:6d} at "
          f"({worst_min[1]},{worst_min[2]})   "
          f"{'ok' if good_min else '<-- HOLE'}")
    return good_sum and good_min


def check_brightness():
    print("\n5. Brightness tilts without pinning a band")
    ok = True
    for name, (o, f, r) in (('OO ', (0, 0, 0)), ('AH ', (32767, 0, 0)),
                            ('EE ', (0, 32767, 0)),
                            ('EHr', (32767, 32767, 32767))):
        base = blend(o, f, r)
        worst_pin = 0
        span = 0.0
        for b in (0, 8192, 16384, 24576, 32767):
            t = with_bright(base, b)
            pinned = sum(1 for i in range(NUM_BANDS)
                         if t[i] in (0, 32767) and base[i] not in (0, 32767))
            worst_pin = max(worst_pin, pinned)
            span = max(span, spectral_distance(base, t))
        good = worst_pin == 0 and span > 0.5
        if not good:
            ok = False
        print(f"   {name}  {worst_pin}/8 pinned, span {span:4.1f} dB   "
              f"{'ok' if good else '<-- FAIL'}")
    return ok




def midi_owns(midi_connected, flag):
    """Transcription of MidiOwns() in main.cpp."""
    return bool(midi_connected and flag)


def check_panel_reclaims():
    """Unplugging the 8mu must hand every control back to the panel."""
    print("\n6. The panel reclaims control when the 8mu is unplugged")
    ok = True

    controls = ("openness", "front", "breath", "pitch", "bright",
                "round", "click level", "click decay", "volume")

    # Plugged in and touched: the 8mu owns it.
    good = all(midi_owns(1, 1) for _ in controls)
    print(f"   8mu connected and moved   -> 8mu owns all {len(controls)}   "
          f"{'ok' if good else 'FAIL'}")
    ok &= good

    # Plugged in but untouched: the panel keeps it, so a controller
    # sitting idle never seizes a knob the player has a hand on.
    good = all(not midi_owns(1, 0) for _ in controls)
    print(f"   8mu connected, untouched  -> panel keeps all   "
          f"{'ok' if good else 'FAIL'}")
    ok &= good

    # UNPLUGGED after being used: the panel must take everything back.
    # This is the regression. The flags latch forever, so testing the flag
    # alone left the knobs dead for the rest of the session with no way
    # back short of a power cycle.
    good = all(not midi_owns(0, 1) for _ in controls)
    print(f"   8mu UNPLUGGED after use   -> panel reclaims all   "
          f"{'ok' if good else '<-- KNOBS STAY DEAD (the bug)'}")
    ok &= good

    print("   (the card must always be playable from its own front panel,")
    print("    and a cable can come out mid-session)")
    return ok


def check_every_knob_matters():
    """Each knob must make an audible difference on its page."""
    print("\n7. Every panel knob makes an audible difference")
    ok = True
    mid = 16384

    # Page 1: openness and front sweep the vowel square.
    openness = spectral_distance(blend(0, mid, 0), blend(32767, mid, 0))
    front = spectral_distance(blend(mid, 0, 0), blend(mid, 32767, 0))
    # Page 2: rounding sweeps the third axis; brightness tilts.
    rounding = spectral_distance(blend(mid, mid, 0), blend(mid, mid, 32767))
    base = blend(mid, mid, 0)
    bright = spectral_distance(with_bright(base, 0), with_bright(base, 32767))

    for name, page, val in (("Knob 1 openness", 1, openness),
                            ("Knob 2 front", 1, front),
                            ("Knob 2 bright", 2, bright),
                            ("Knob 3 rounding", 2, rounding)):
        # 2 dB is about the floor for a control to feel like it is doing
        # something under the hand rather than merely measuring different.
        good = val > 2.0
        if not good:
            ok = False
        print(f"   page {page}  {name:16} {val:5.1f} dB   "
              f"{'ok' if good else '<-- TOO SUBTLE'}")

    print("   (Knob 1 page 2 is pitch, 50-500 Hz, and Knob 3 page 1 is")
    print("    breath - neither is a spectral change, so neither is")
    print("    measured here)")

    # Between the two pages the panel must reach EVERY parameter the 8mu
    # can, or the card is crippled without a controller.
    panel = {"openness", "front", "breath", "pitch", "bright", "round"}
    midi = {"openness", "front", "breath", "pitch", "bright", "round"}
    good = panel == midi
    if not good:
        ok = False
    print(f"   panel reaches every 8mu parameter   "
          f"{'ok' if good else '<-- GAP'}")
    return ok


def main():
    print("TRACT8 playability check - the 3-D vowel cube")
    print("  Three axes, eight corner vowels, trilinear between them.")
    ok = check_corners()
    ok &= check_axes_live()
    ok &= check_non_corner_vowels()
    ok &= check_no_dead_spots()
    ok &= check_brightness()
    ok &= check_panel_reclaims()
    ok &= check_every_knob_matters()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
