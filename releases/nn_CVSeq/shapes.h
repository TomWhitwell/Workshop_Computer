// Step shape library for CVSeq
//
// 32 unipolar shapes, each one step long: x runs 0 to 1 across the step and
// the shape gives 0 to 1.  Built into tables at power-up.  web/index.html
// carries the same definitions, so keep the two in step.

#ifndef CVSEQ_SHAPES_H
#define CVSEQ_SHAPES_H

#include <stdint.h>
#include <math.h>

static constexpr int kShapeBits = 8;
static constexpr int kShapeSize = 1 << kShapeBits;
static constexpr int kNumShapes = 32;

// 0-4095, with a guard point so interpolation never wraps
static int16_t gShapes[kNumShapes][kShapeSize + 1];

namespace shapegen
{

static float Clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Eight fixed random levels, for the two random shapes
static float RandomLevel(int i)
{
	uint32_t r = 2463534242u + uint32_t(i) * 2654435761u;
	r ^= r << 13; r ^= r >> 17; r ^= r << 5;
	return float(r >> 8) * (1.0f / 16777216.0f);
}

static float Shape(int s, float x)
{
	const float pi = 3.14159265f;
	switch (s)
	{
	case 0: return 1.0f;                                   // High
	case 1: return 0.5f;                                   // Middle
	case 2: return 0.0f;                                   // Low
	case 3: return x;                                      // Ramp up
	case 4: return 1.0f - x;                               // Ramp down
	case 5: return 1.0f - fabsf(2.0f * x - 1.0f);          // Triangle
	case 6: return fabsf(2.0f * x - 1.0f);                 // Valley
	case 7: return sinf(pi * x);                           // Hump
	case 8: return 0.5f - 0.5f * cosf(2.0f * pi * x);      // Sine (from low)
	case 9: return 0.5f + 0.5f * cosf(2.0f * pi * x);      // Cosine (from high)
	case 10: return x * x * x;                             // Exp rise
	case 11: return (1.0f - x) * (1.0f - x) * (1.0f - x);  // Exp fall
	case 12: return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); // Log rise
	case 13: return 1.0f - x * x * x;                      // Log fall
	case 14: return x * x * (3.0f - 2.0f * x);             // S rise
	case 15: return 1.0f - x * x * (3.0f - 2.0f * x);      // S fall
	case 16: return x < 0.5f ? 1.0f : 0.0f;                // Square
	case 17: return x < 0.5f ? 0.0f : 1.0f;                // Square, late
	case 18: return x < 0.25f ? 1.0f : 0.0f;               // Pulse
	case 19: return x >= 0.75f ? 1.0f : 0.0f;              // Pulse, late
	case 20: return floorf(x * 4.0f) / 3.0f;               // Stairs up
	case 21: return 1.0f - floorf(x * 4.0f) / 3.0f;        // Stairs down
	case 22: return 2.0f * x - floorf(2.0f * x);           // Saw x2
	case 23: return 1.0f - (2.0f * x - floorf(2.0f * x));  // Saw down x2
	case 24: { float y = 2.0f * x - floorf(2.0f * x); return 1.0f - fabsf(2.0f * y - 1.0f); } // Triangle x2
	case 25: return 0.5f - 0.5f * cosf(4.0f * pi * x);     // Sine x2
	case 26: return x < 0.05f ? x / 0.05f : expf(-(x - 0.05f) * 6.0f); // Pluck
	case 27: return x > 0.95f ? (1.0f - x) / 0.05f : expf(-(0.95f - x) * 6.0f); // Swell
	case 28:                                               // ADSR
		if (x < 0.1f) return x / 0.1f;
		if (x < 0.3f) return 1.0f - 0.4f * (x - 0.1f) / 0.2f;
		if (x < 0.8f) return 0.6f;
		return 0.6f * (1.0f - (x - 0.8f) / 0.2f);
	case 29: return fabsf(sinf(3.0f * pi * x)) * (1.0f - x); // Bounce
	case 30: return RandomLevel(int(x * 8.0f));            // Random steps
	default:                                               // Random smooth
	{
		float p = x * 8.0f;
		int i = int(p);
		float f = p - float(i);
		float w = 0.5f - 0.5f * cosf(pi * f);
		return RandomLevel(i) + (RandomLevel(i + 1) - RandomLevel(i)) * w;
	}
	}
}

static void Build()
{
	for (int s = 0; s < kNumShapes; s++)
	{
		for (int n = 0; n <= kShapeSize; n++)
		{
			// The guard point repeats the last point rather than wrapping:
			// a step's shape doesn't loop back to its start
			int m = n < kShapeSize ? n : kShapeSize - 1;
			float v = Clamp01(Shape(s, float(m) / float(kShapeSize)));
			gShapes[s][n] = int16_t(v * 4095.0f + 0.5f);
		}
	}
}

} // namespace shapegen

#endif
