#pragma once
#include <cstdint>

namespace imp
{

// The card is split across the RP2040's two cores:
//
//   core 0 (the audio interrupt, ~20us per sample): only audio-rate work --
//     recording, the slice engine's voices, the reverb, the jacks.
//   core 1 (a free-running loop): everything that only needs to happen a
//     few thousand times a second -- knobs, catch-up, deciding when to
//     re-cut, generating slice maps, all the division-heavy maths behind
//     each grain's envelope, and the LEDs.
//
// Core 1 hands its results to core 0 in an EngineParams block (see
// Impermanence.h for how the two cores take turns so a block is never read
// while half-written). Everything core 0 needs per grain is precomputed
// here, so the audio interrupt never divides and never does a burst of
// work on one sample.

constexpr int kLayers = 3;
constexpr int kMaxSlots = 16;

// Pitch-scatter ratios, Q12, all whole octaves: unison, +1, -1, +2, -2.
// Higher chaos reaches further along the list.
constexpr int32_t kPitch[5] = {4096, 8192, 2048, 16384, 1024};

struct Slot
{
	uint8_t src;	 // which slice (masked to the current slice count)
	uint8_t pitch;	 // index into kPitch
	uint8_t reverse; // 1 = play backwards
	int16_t jitter;	 // start offset, in 1/1024ths of a slice
};

struct SliceMap
{
	uint8_t exp; // Texture slices = 1 << exp
	int32_t fadeQ12;
	int32_t decayQ12;
	Slot slot[kLayers][kMaxSlots];
};

struct LayerParams
{
	int32_t count = 1;	 // slots per cycle
	int32_t srcMask = 0; // slice count - 1
	int32_t sliceLen = 36000;
	uint32_t slotQ = 36000u << 16; // sliceLen in Q16, for the Texture slot clock
	int32_t regionStart = 0;
	int32_t level = 0; // Q12

	// Where each slot starts and how it plays, worked out on core 1 whenever
	// the map or the geometry changes. The audio interrupt just reads them:
	// starting a grain used to be the most expensive thing it did.
	int32_t slotStart[kMaxSlots] = {}; // frame, already wrapped into the take
	int32_t slotPitch[kMaxSlots] = {}; // Q12 rate, negative = plays backwards

	// Texture grain envelope at the current speed (samples, Q16 steps).
	int32_t texHold = 36000;
	int32_t texAttackStep = 65536;
	int32_t texReleaseStep = 65536;
};

struct EngineParams
{
	// Bumped whenever the take length, the map, the mode or a layer's slot
	// count changes, so core 0 checks one number per sample, not six.
	uint32_t epoch = 0;
	int32_t recLength = 36000;
	bool rhythm = false;
	int32_t map = 0;		// which of the two slice maps is live
	uint32_t reseedSeq = 0; // bumps on every new map
	// Timing and pitch are separate. A clock on Pulse In 2 changes how fast
	// the card works through its slots (slotSpeed), while the playheads keep
	// reading at their own rate (readRate), so the loop follows the clock
	// without the tape-style pitch shift the card used to have. Setting
	// readRate = slotSpeed instead would restore that varispeed behaviour.
	// slotSpeed carries 16 fractional bits, not 12: a clock period rarely
	// divides the take exactly, and at Q12 the rounding left the loop
	// drifting about a sample per cycle against the clock (~3.6ms a minute).
	int32_t slotSpeed = 1 << 16; // Q16, how fast slots are played through
	int32_t readRate = 4096;	 // Q12, how fast the audio itself is read
	int32_t internalPeriod = 12000;

	// Rhythm hit envelope for the current clock period.
	int32_t rhyHold = 48;
	int32_t rhyAttackStep = 1366;
	int32_t rhyReleaseStep = 65536;

	LayerParams layer[kLayers];

	// Commands from core 1, acted on by core 0 when the count changes.
	uint32_t clearSeq = 0;	// Main turned fully anticlockwise: fade out, clear
	uint32_t recordSeq = 0; // ...and turned back up: record a new take

	int32_t mix = 0;	   // wet amount 0..4095 (knob + CV2)
	int32_t decay = 8192;  // reverb tank decay, Q15
	int32_t wander = 0;	   // CV Out 1 value
};

} // namespace imp
