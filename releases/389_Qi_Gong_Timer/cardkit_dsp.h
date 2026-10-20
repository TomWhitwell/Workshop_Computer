// =============================================================================
//  cardkit_dsp.h : small fixed-point building blocks shared by card code
//  (clamping, rounded multiplies, soft clipping, random numbers, sine / pitch tables).
//  AI-assisted (Claude / Anthropic).
// =============================================================================
#pragma once
#include <stdint.h>
#include <math.h>

namespace cardkit {

// ----------------------------------------------------------------------------
// Small helpers
// ----------------------------------------------------------------------------
static inline int32_t Clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int32_t Abs(int32_t v) { return v < 0 ? -v : v; }
// Linear blend of a and b by t (t is 0..4096).  Keep |b-a| below ~500,000.
static inline int32_t Lerp(int32_t a, int32_t b, int32_t t) { return a + (((b - a) * t) >> 12); }

// Rounded fixed-point multiplies.  A plain ">> 16" rounds toward minus infinity, which leaves a
// small NEGATIVE bias every time it is used inside a one-pole filter (a DC offset).
static inline int32_t MulR16(int32_t a, int32_t c) { return (a * c + 32768) >> 16; }
static inline int32_t MulR15(int32_t a, int32_t c) { return (a * c + 16384) >> 15; }
// Multiply that always rounds TOWARD ZERO, so echoes shrink all the way to true silence.
static inline int32_t MulTZ15(int32_t a, int32_t c) { return a >= 0 ? (a * c) >> 15 : -((-a * c) >> 15); }

// Half of x, rounding toward ZERO (a plain ">> 1" rounds negatives away from zero, which lets tiny numbers circulate forever).
static inline int32_t Half(int32_t x) { return x >= 0 ? x >> 1 : -((-x) >> 1); }

// Gentle soft clipper: y = x - x^3/3.  12-bit version (+/-2047) ...
static inline int32_t SoftClip(int32_t x)
{
	x = Clamp(x, -2047, 2047);
	int32_t x2 = (x * x) >> 11;
	int32_t x3 = (x2 * x) >> 11;
	return x - ((x3 * 21845) >> 16);
}
// ... and a 16-bit version (+/-32767) for the delay lines
static inline int32_t SoftClipQ4(int32_t x)
{
	x = Clamp(x, -32767, 32767);
	int32_t x2 = (x * x) >> 15;
	int32_t x3 = (x2 * x) >> 15;
	return x - ((x3 * 21845) >> 16);
}

// Tiny, fast pseudo-random number generator (xorshift32).
struct Rng
{
	uint32_t s = 0x1234567u;
	uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	uint32_t Below(uint32_t n) { return ((Next() >> 16) * n) >> 16; }              // 0..n-1  (n < 65536)
	uint32_t BelowBig(uint32_t n) { return (uint32_t)(((uint64_t)Next() * n) >> 32); }   // any n (slower; used rarely)
	bool Chance(int32_t p12) { return (int32_t)(Next() >> 20) < p12; }            // p12 = probability * 4096
	void Stir(uint32_t extra) { s ^= extra * 2654435761u; if (!s) s = 0x1234567u; Next(); }
};

// ----------------------------------------------------------------------------
// Look-up tables, filled once at start-up
// ----------------------------------------------------------------------------
static int16_t  gSine[1025];     // one cycle of sine, +1 guard point for interpolation
static uint32_t gNoteInc[130];   // oscillator phase step for each MIDI note

static void InitTables()
{
	for (int i = 0; i <= 1024; ++i)
		gSine[i] = (int16_t)lrintf(2047.0f * sinf(6.28318530718f * (float)i / 1024.0f));
	for (int n = 0; n < 130; ++n)
	{
		float f = 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f);
		gNoteInc[n] = (uint32_t)(f * 89478.485f);   // 2^32 / 48000
	}
}

// Sine with linear interpolation. phase is a full 32-bit "turn".
static inline int32_t SineLookup(uint32_t phase)
{
	uint32_t i = phase >> 22;
	int32_t  f = (int32_t)((phase >> 14) & 255);
	int32_t  a = gSine[i], b = gSine[i + 1];
	return a + (((b - a) * f) >> 8);
}

// Pitch in Q16 semitones (MIDI note * 65536) -> oscillator phase step.
static inline uint32_t PitchToInc(int32_t pitchQ16)
{
	int32_t p = Clamp(pitchQ16, 12 << 16, 120 << 16);
	int32_t n = p >> 16;
	int32_t f = (p >> 8) & 255;
	uint32_t a = gNoteInc[n], b = gNoteInc[n + 1];
	return a + ((((b - a) >> 4) * (uint32_t)f) >> 4);
}

// Phase step per control tick (1500 ticks a second) for a cycle that lasts `sec` seconds.
static constexpr uint32_t CycleInc(int sec) { return (uint32_t)(4294967296.0 / ((double)sec * 1500.0)); }

// mu-law (G.711) 8-bit companding, used by the long delay to fit 9.7 s into RAM.
static inline uint8_t MuEnc(int32_t s)
{
	int sign = 0;
	if (s < 0) { s = -s; sign = 0x80; }
	if (s > 32635) s = 32635;
	s += 0x84;
	int e = 7;
	for (int m = 0x4000; (s & m) == 0 && e > 0; m >>= 1) --e;
	int mant = (s >> (e + 3)) & 0x0F;
	return (uint8_t)~(sign | (e << 4) | mant);
}
static inline int32_t MuDec(uint8_t u)
{
	int v = (uint8_t)~u;
	int sign = v & 0x80, e = (v >> 4) & 7, mant = v & 0x0F;
	int32_t s = (((mant << 3) + 0x84) << e) - 0x84;
	return sign ? -s : s;
}

}  // namespace cardkit
