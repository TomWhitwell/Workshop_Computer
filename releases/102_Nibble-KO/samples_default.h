// samples_default.h — the baked sample BANK, and which voice plays what.
//
// samples.h is generated at build time by tools/mksamples.py from
// samples/smpNN.raw (8-bit signed mono, 48kHz). Neither the .raw files nor
// the generated header are committed: with no samples present the build still
// succeeds and every voice falls back to its synthesised version. Same
// __has_include trick WorkshopBio and WorkshopZX use.
//
// TWO SEPARATE THINGS, and keeping them separate is the point:
//
//   THE BANK     an indexed library of recordings. Entry 3 is "the third
//                sample", not "the open hat".
//   THE MAPPING  which bank entry each of the twelve voice slots plays, or
//                -1 for "stay synthesised".
//
// The indirection exists because the WebUI will let any entry be assigned to
// any slot at runtime. Baking a sample-per-slot would have to be undone the
// moment that lands, and it would also stop two voices sharing one recording
// without a second copy in flash.

#pragma once
#include <stdint.h>
#include "drums.h"   // kNumVoices

namespace nko {

/// Bank capacity. Must match MAX_SAMPLES in tools/mksamples.py.
///
/// Sixty-four is not a flash figure — it is how many entries the offset and
/// size tables carry, at eight bytes each, so the tables cost 512 bytes
/// whether or not the bank is full. Flash is the real limit on how many can
/// actually be shipped; see the note in mksamples.py.
constexpr int kMaxSamples = 64;

#if defined(__has_include)
#  if __has_include("samples.h")
#    define NKO_HAVE_SAMPLES 1
#  endif
#endif

#ifdef NKO_HAVE_SAMPLES

// Defines kSampleBank[], kSampleBankLen, kSampleOff[], kSampleSize[].
#include "samples.h"

constexpr bool kHaveSamples = true;

#else

constexpr bool kHaveSamples = false;

// Stubs so everything compiles with no samples present. kHaveSamples is
// false, so these are never dereferenced.
static const signed char kSampleBank[1] = { 0 };
static const uint32_t    kSampleBankLen = 0;
static const uint32_t    kSampleOff[kMaxSamples] = {};
static const uint32_t    kSampleSize[kMaxSamples] = {};

#endif

/// Which BANK ENTRY each voice slot plays, or -1 to stay synthesised.
///
/// The current bank, imported from the Cheetah SpecDrum's original kit:
///
///   0  kick        1  snare       2  closed hat
///   3  open hat    4  clap
///
/// Five slots sampled, seven synthesised — deliberately a mix rather than a
/// wholesale swap. The synthesised voices are not a fallback here: a
/// bit-crushed synth tom and a sampled clap in the same kit is the point of
/// the card, and the ones left synthesised (the deep kick, the snappy snare,
/// the metallic hat, the crash, the cowbell, two toms) are the ones whose
/// Y-knob reshaping earns its keep.
///
/// TODO(webui): this becomes a runtime table loaded from flash, so any entry
/// can be assigned to any slot from the browser. The shape is already right
/// for that — it is indices, not audio.
constexpr int8_t kVoiceSample[kNumVoices] = {
	 0,   //  0  kick             <- bank 0
	-1,   //  1  kick deep           synth
	 1,   //  2  snare            <- bank 1
	-1,   //  3  snare snappy        synth
	 2,   //  4  closed hat       <- bank 2
	-1,   //  5  hi-hat metallic     synth
	 3,   //  6  open hi-hat      <- bank 3
	-1,   //  7  crash               synth
	-1,   //  8  cowbell             synth
	-1,   //  9  tom 1 "pew"         synth
	-1,   // 10  syn tom 2           synth
	 4,   // 11  clap             <- bank 4  (was "syn drum 3")
};

/// Where a bank entry's audio lives, and how long it is. A null pointer means
/// "nothing here" — an unfilled index, or a build with no samples at all.
struct SampleRef
{
	const int8_t *data;
	uint32_t      len;
};

static inline SampleRef BakedSample(int index)
{
	if (!kHaveSamples || index < 0 || index >= kMaxSamples)
		return { nullptr, 0 };
	if (kSampleSize[index] == 0) return { nullptr, 0 };
	return { reinterpret_cast<const int8_t *>(kSampleBank) + kSampleOff[index],
	         kSampleSize[index] };
}

} // namespace nko
