#include "modes/hueorgan.h"
#include "pico.h"

namespace lp {

namespace {

struct Scale
{
	int8_t steps[12];
	int    len;
};

const Scale kScales[4] = {
	{ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },   // Chromatic
	{ { 0, 2, 4, 5, 7, 9, 11 }, 7 },                     // Major
	{ { 0, 2, 3, 5, 7, 8, 10 }, 7 },                     // Natural minor
	{ { 0, 2, 4, 7, 9 }, 5 },                            // Major pentatonic
};

// Below these (unipolar Q16) there is no colour to read. PLACEHOLDERS: gelled
// LDRs never separate cleanly, so the chroma floor wants setting on the bench.
constexpr int32_t kMinBright = 6554;    // 10%
constexpr int32_t kMinChroma = 2048;    // ~3% between brightest and dimmest

// Rotate the wheel 30 degrees so its wrap point is at magenta, not red.
// Otherwise red, the commonest colour, would flicker between the lowest and
// the highest note as its hue dithers either side of zero.
constexpr int32_t kHueShift = 5461;

constexpr int kDwellTicks = 45;         // 30ms
constexpr int kZoneMargin = 48;         // knob units of hysteresis per zone edge

inline int32_t Max3(int32_t a, int32_t b, int32_t c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
inline int32_t Min3(int32_t a, int32_t b, int32_t c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }

} // namespace

int32_t HueOrganMode::HueQ16(int32_t r, int32_t g, int32_t b)
{
	// 12-bit working values keep (difference * 65536/6) inside 31 bits.
	r >>= 4; g >>= 4; b >>= 4;
	int32_t mx = Max3(r, g, b);
	int32_t d = mx - Min3(r, g, b);
	if (d == 0) return -1;
	int32_t h;
	if (mx == r)      h = ((g - b) * 10923) / d;
	else if (mx == g) h = 21845 + ((b - r) * 10923) / d;
	else              h = 43691 + ((r - g) * 10923) / d;
	return h & 0xFFFF;
}

void HueOrganMode::OnEnter()
{
	degree_ = pending_ = -1;
	dwell_ = trig_ = 0;
	ampTarget_ = 0;
	synced_ = false;
}

void HueOrganMode::OnDownRelease(int ticks)
{
	// downTicks_ saturates at kHoldTicks, so this is exactly the old
	// "did the hold fire" test.
	if (ticks < kHoldTicks) transpose_ = (transpose_ + 1) % 12;
}

void HueOrganMode::OnDownHold()
{
	transpose_ = 0;
}

void HueOrganMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	// Scale zone, with hysteresis at the quarter boundaries. On arrival, take
	// the knob's zone silently, so the mode-number flash is not overwritten.
	if (!synced_)
	{
		scale_ = static_cast<int>(c.main >> 10);
		synced_ = true;
	}
	int32_t lo = scale_ * 1024 - kZoneMargin;
	int32_t hi = (scale_ + 1) * 1024 + kZoneMargin;
	if (c.main < lo || c.main >= hi)
	{
		scale_ = static_cast<int>(c.main >> 10);
		degree_ = pending_ = -1;   // re-quantise into the new scale
		dwell_ = 0;
		scaleChanged_ = true;
	}
	if (scaleChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(scale_ + 1);
		scaleChanged_ = false;
	}

	const Scale &sc = kScales[scale_];
	int32_t mx = Max3(f.ur, f.ug, f.ub);
	int32_t mn = Min3(f.ur, f.ug, f.ub);
	bool valid = mx >= kMinBright && (mx - mn) >= kMinChroma;
	int32_t hue = valid ? HueQ16(f.ur, f.ug, f.ub) : -1;
	valid = hue >= 0;

	if (valid)
	{
		hue = (hue + kHueShift) & 0xFFFF;
		// Two octaves of the scale plus the top root, evenly spaced in hue.
		int n = 2 * sc.len + 1;
		int32_t band = 65536 / n;

		int cand = -1;
		if (degree_ >= 0)
		{
			// Stay put until the hue is a quarter-band past the current band.
			int32_t bLo = degree_ * band - band / 4;
			int32_t bHi = (degree_ + 1) * band + band / 4;
			if (hue >= bLo && hue < bHi) cand = degree_;
		}
		if (cand < 0)
		{
			cand = static_cast<int>(hue / band);
			if (cand >= n) cand = n - 1;
		}

		if (cand == degree_)
		{
			pending_ = -1;
			dwell_ = 0;
		}
		else if (cand != pending_)
		{
			pending_ = cand;
			dwell_ = 0;
		}
		else if (++dwell_ >= kDwellTicks)
		{
			degree_ = cand;
			pending_ = -1;
			dwell_ = 0;
			trig_ = kTrigTicks;
		}
	}
	else
	{
		pending_ = -1;
		dwell_ = 0;
	}

	if (degree_ >= 0)
	{
		int note = kBaseNote + transpose_ + 12 * (degree_ / sc.len) + sc.steps[degree_ % sc.len];
		out.cv2Note = static_cast<int16_t>(note);
		inc_ = note_to_inc(note << 8);
	}

	ampTarget_ = (valid && degree_ >= 0) ? 32767 : 0;
	out.pulse1 = trig_ > 0;
	out.pulse2 = valid;
	if (trig_ > 0) trig_--;
}

void __not_in_flash_func(HueOrganMode::AudioTick)(const SensorFrame &, const Inputs &, EngineOut &out)
{
	// ~10ms swell and fade. Falling, the floored shift always reaches zero.
	amp_ = slew(amp_, ampTarget_, 9);
	if (amp_ == 0) { out.audio1 = out.audio2 = 0; return; }

	phase_ += inc_;
	int32_t v = fast_sin(phase_) + (fast_sin(phase_ << 1) >> 1);   // +/-49150
	v = (v * amp_) >> 15;
	int16_t s = clamp12((v * 2729) >> 16);                          // 49150 -> 2046
	out.audio1 = s;
	out.audio2 = s;
}

} // namespace lp
