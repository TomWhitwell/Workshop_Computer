#include "modes/triad.h"
#include "pico.h"

namespace lp {

namespace {

const Wave kPath[4] = { Wave::Sine, Wave::Tri, Wave::Saw, Wave::Square };

// Semitones above the root for root / third / fifth.
const int8_t kChord[4][3] = {
	{ 0, 4, 7 },   // Major
	{ 0, 3, 7 },   // Minor
	{ 0, 2, 7 },   // Sus2
	{ 0, 5, 7 },   // Sus4
};

} // namespace

void TriadMode::OnDownPress()
{
	flavour_ = (flavour_ + 1) % kFlavours;
	flavourChanged_ = true;
}

void TriadMode::ControlTick(const SensorFrame &, const Ctrl &c, EngineOut &out)
{
	morph_.Set(kPath, 4, c.main);

	int32_t rootQ8 = (kRootNote << 8) + ((pitchIn_ * kSemisQ8PerUnit) >> 6);
	for (int i = 0; i < 3; i++)
		inc_[i] = note_to_inc(rootQ8 + (kChord[flavour_][i] << 8));

	if (flavourChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(flavour_ + 1);
		flavourChanged_ = false;
	}
}

void __not_in_flash_func(TriadMode::AudioTick)(const SensorFrame &f, const Inputs &in, EngineOut &out)
{
	// ~5ms smoothing on the pitch input: ADC noise would otherwise be a
	// fraction-of-a-semitone jitter on every control tick.
	pitchIn_ = slew(pitchIn_, in.audio1 * 64, 8);

	const int32_t amp[3] = { f.ur, f.ug, f.ub };
	int32_t sum = 0;
	for (int i = 0; i < 3; i++)
	{
		phase_[i] += inc_[i];
		// Q15 * Q16 >> 16: 32767 * 65535 still fits 31 bits.
		sum += (morph_.Render(phase_[i]) * amp[i]) >> 16;
	}
	// Divide by three, not sqrt(3): three full-level voices can peak together,
	// and a clipped drone is a harsh one. 98301 * 1365 >> 16 == 2047.
	int16_t s = clamp12((sum * 1365) >> 16);
	out.audio1 = s;
	out.audio2 = s;
}

} // namespace lp
