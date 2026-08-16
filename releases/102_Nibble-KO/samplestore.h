// samplestore.h — the sound library, and what each voice plays from it.
//
// TWO LIBRARIES, ONE NUMBERING. A voice slot names a sound by a single value:
//
//     -1                        synthesise it (kSourceSynth)
//     0 .. kMaxSamples-1        a BAKED entry, compiled into the firmware
//     kUserSourceBase + n       a USER entry, uploaded over USB
//
// so any slot can play any sound the card holds, and only ResolveSample()
// needs to know which library a given value came from. The baked half means a
// fresh card works with no upload; the user half sits at a FIXED flash offset
// so reflashing firmware does not move or wipe it.
//
// EVERYTHING HERE IS INDIRECTION, DELIBERATELY. The full chain is:
//
//     pattern event -> voice index -> gVoiceSample -> sound -> audio
//
// and each arrow is a lookup rather than a copy. That is what lets uploads
// change what a pattern SOUNDS like without touching the pattern, which stays
// a bare list of voice indices (see looper.h's note on LoopEvent). It also
// means two voices can share a recording, and that re-pointing a slot costs
// one byte rather than a second copy of the audio.
//
// V1 DID NOT WORK THAT WAY, and the difference is the point of v2. V1 keyed
// uploads BY VOICE — offset[12]/size[12], one upload per pad — so a recording
// belonged to a pad, could not be shared, and a dropdown could only ever
// offer the baked bank. The header comment above described the library model
// the whole time; v1 simply had not got there. See UserSampleHeader.
//
// Both the library AND the slot map live in one 4KB sector, so a single erase
// commits an upload together with the assignment that uses it. A reboot can
// therefore never come up with audio present but nothing pointing at it, or a
// map naming an entry that was never written.
//
// Ported originally from WorkshopBio/samplestore.h, whose mode x round-robin
// grid this replaced with flat voice slots.

#pragma once
#include <stdint.h>
#include "nibbleko.h"         // kFlashBase/kFlashSize/kFlashSector
#include "drums.h"            // kNumVoices
#include "samples_default.h"  // baked fallback

namespace nko {

// --- Flash layout ----------------------------------------------------------
// kFlashBase/kFlashSize/kFlashSector live in nibbleko.h, which describes the
// physical part once for every region that carves it up.
//
// The user region is the last 1MB of the 2MB part — the same split
// WorkshopBio uses.
constexpr uint32_t kUserRegionOff = 1u * 1024 * 1024;   // offset within flash
constexpr uint32_t kUserRegionLen = 1u * 1024 * 1024;

/// How many uploaded samples the card can hold. The limit is the header
/// sector, not the audio region: 32 entries of (offset, size, name) plus the
/// slot map is comfortably inside 4KB, and 32 distinct one-shots is already
/// more than a twelve-voice kit can use at once.
constexpr int kMaxUserSamples = 32;

/// Characters stored per entry name, including the terminator. Names are
/// OPTIONAL — an entry with an empty name displays as USER1, USER2... by
/// index, so uploading never forces you to name anything.
constexpr int kNameLen = 16;

/// Slot source sentinel: synthesise this voice with its OWN character.
///
/// Note this is the STORED value. webui.h has kWireSynth (0x7F) for the same
/// idea on the wire, because SysEx bytes are 7-bit and cannot carry -1. The
/// two are deliberately named differently: they are the same concept in two
/// encodings, and conflating them is how a sign error gets in.
constexpr int8_t kSourceSynth = -1;

/// WHAT A SOURCE VALUE MEANS. One flat numbering covers every sound the card
/// can make, so a dropdown can offer all of them for any slot and only
/// ResolveSample() needs to know which library a value came from:
///
///     0  .. 63    baked bank      (kMaxSamples entries)
///     64 .. 95    user library    (kUserSourceBase + kMaxUserSamples)
///     96 .. 107   synth voices    (kSynthBase + kNumVoices)
///     -1          synth, this slot's OWN character (kSourceSynth)
///
/// THE RANGES MUST NOT OVERLAP, and getting that wrong is silent: a slot
/// would play a sample where a synth voice was asked for. The static_asserts
/// below are what stop that happening when one of these is next widened.
///
/// kSourceSynth stays as the shorthand for "my own character" — what a slot
/// resets to and what the factory kit uses. It is equivalent to kSynthBase
/// plus the slot's own index; keeping it distinct means the common case does
/// not have to know its own number.
constexpr int8_t kUserSourceBase = 64;
constexpr int8_t kSynthBase      = 96;

static_assert(kUserSourceBase >= kMaxSamples,
              "the user range must start above the baked bank");
static_assert(kSynthBase >= kUserSourceBase + kMaxUserSamples,
              "the synth range must start above the user library");
static_assert(kSynthBase + kNumVoices <= 127,
              "every source value must fit in an int8_t and a 7-bit SysEx byte");

/// Directory written at the start of the user region. Fixed size so it can be
/// erased and rewritten as one 4KB sector.
///
/// V2 CHANGED THE MODEL. V1 keyed uploads BY VOICE: offset[12]/size[12], one
/// upload per pad. That made a sample belong to a pad, so the same recording
/// could not be shared and a dropdown could only ever offer the baked bank.
///
/// V2 makes uploads a LIBRARY: entries exist independently, and slotSource[]
/// says which sound each voice plays — baked index, user entry, or synth.
/// That is the shape samplestore.h's own header always described ("two voices
/// can share a recording, and re-pointing a slot costs one byte rather than a
/// second copy of the audio"); v1 simply had not got there yet.
///
/// The slot map lives HERE rather than in its own sector so one erase commits
/// both an upload and the assignment that uses it — a reboot can never come
/// up with audio present but pointing nowhere, or a map pointing at an entry
/// that was not written.
struct UserSampleHeader
{
	uint32_t magic;       // kUserMagic when a valid upload is present
	uint32_t version;     // layout version, for future changes

	// The library. Byte offsets are relative to the START of the user
	// region's data area; a size of 0 means "this entry is empty".
	uint32_t offset[kMaxUserSamples];
	uint32_t size[kMaxUserSamples];
	char     name[kMaxUserSamples][kNameLen];

	// What each VOICE plays: kSourceSynth, a baked bank index, or
	// kUserSourceBase + library index.
	int8_t   slotSource[kNumVoices];
	uint8_t  pad[4 - (kNumVoices % 4)];   // keep totalBytes 4-byte aligned

	uint32_t totalBytes;
	uint32_t reserved[8];
};

constexpr uint32_t kUserMagic   = 0x4B4F3102u;   // "KO1" + layout version
constexpr uint32_t kUserVersion = 2;

static_assert(sizeof(UserSampleHeader) <= kFlashSector,
              "the directory must fit in one erasable sector");

// The header occupies the first flash sector of the region; audio follows.
constexpr uint32_t kUserHeaderLen = kFlashSector;
constexpr uint32_t kUserDataOff   = kUserRegionOff + kUserHeaderLen;
constexpr uint32_t kUserDataLen   = kUserRegionLen - kUserHeaderLen;

/// Read-only view of the user region as mapped through XIP.
static inline const UserSampleHeader *UserHeader()
{
	return reinterpret_cast<const UserSampleHeader *>(kFlashBase + kUserRegionOff);
}

static inline const int8_t *UserData()
{
	return reinterpret_cast<const int8_t *>(kFlashBase + kUserDataOff);
}

static inline bool HaveUserSamples()
{
	const UserSampleHeader *h = UserHeader();
	return h->magic == kUserMagic && h->version == kUserVersion;
}

// SampleRef lives in samples_default.h, since the baked bank is the source
// that always exists; the user region is the override on top of it.

/// WHAT each voice slot plays, at runtime: kSourceSynth, a baked bank index,
/// or kUserSourceBase + a user library index.
///
/// Seeded at boot from the SAVED map if the card holds one, otherwise from
/// samples_default.h's baked defaults — see LoadSlotSources(), called from
/// main(). The browser rewrites it per-slot over MSG_SET_SOURCE, and
/// MSG_SAVE_MAP commits it.
///
/// `inline` because both drums.cpp and webui.cpp include this header and both
/// need the same array, not one each.
inline int8_t gVoiceSample[kNumVoices] = {
	kVoiceSample[0],  kVoiceSample[1],  kVoiceSample[2],  kVoiceSample[3],
	kVoiceSample[4],  kVoiceSample[5],  kVoiceSample[6],  kVoiceSample[7],
	kVoiceSample[8],  kVoiceSample[9],  kVoiceSample[10], kVoiceSample[11],
};
static_assert(kNumVoices == 12, "gVoiceSample's initialiser is written out "
                                "per slot and must match kNumVoices");

/// True if a user library entry holds audio.
static inline bool UserEntryPresent(int entry)
{
	if (entry < 0 || entry >= kMaxUserSamples) return false;
	if (!HaveUserSamples()) return false;
	const UserSampleHeader *h = UserHeader();
	return h->size[entry] > 0 &&
	       h->offset[entry] + h->size[entry] <= kUserDataLen;
}

/// Is this source value a user library entry?
static inline bool IsUserSource(int src) { return src >= kUserSourceBase; }

/// Is this source value "synthesise, with voice N's character"?
static inline bool IsSynthSource(int src)
{
	return src >= kSynthBase && src < kSynthBase + kNumVoices;
}

/// Which synth voice a slot should render as: its own, or a borrowed one.
static inline int SynthVoiceFor(int voice, int src)
{
	return IsSynthSource(src) ? (src - kSynthBase) : voice;
}

/// Validate a source value: synth (own or borrowed), a real baked entry, or a
/// filled user entry.
///
/// Bounds-checked because the caller is a SysEx message from a browser, i.e. a
/// system boundary rather than trusted internal code.
static inline bool SourceValid(int src)
{
	if (src == kSourceSynth) return true;
	if (IsSynthSource(src)) return true;
	if (IsUserSource(src)) return UserEntryPresent(src - kUserSourceBase);
	return src >= 0 && src < kMaxSamples;
}

/// Point a slot at any sound: synth, baked, or user library entry.
static inline bool SetVoiceSample(int voice, int src)
{
	if (voice < 0 || voice >= kNumVoices) return false;
	if (!SourceValid(src)) return false;
	gVoiceSample[voice] = static_cast<int8_t>(src);
	return true;
}

/// Install the saved slot map, if the card holds a valid one.
///
/// Called once from main(), BEFORE the card starts playing. Without it an
/// upload would survive a power cycle but the assignment using it would not,
/// so a saved kit would come back up half restored — which is exactly what
/// v1 did.
///
/// A saved source pointing at an entry that is no longer present (the audio
/// was deleted from under it) falls back to synth rather than to silence: a
/// pad that makes no sound reads as a broken card, where a pad that reverts
/// to its synth voice reads as an empty slot, which is what it is.
static inline void LoadSlotSources()
{
	if (!HaveUserSamples()) return;
	const UserSampleHeader *h = UserHeader();
	for (int v = 0; v < kNumVoices; v++)
	{
		const int8_t s = h->slotSource[v];
		gVoiceSample[v] = SourceValid(s) ? s : kSourceSynth;
	}
}

/// Resolve one VOICE SLOT to the audio it should play, or {nullptr, 0} for
/// "this voice is synthesised".
///
/// One flat numbering covers both libraries — baked below kUserSourceBase,
/// uploaded at or above it — so a slot can name any sound the card holds and
/// this is the only place that has to know which library it came from.
///
/// Two slots naming the same entry cost one copy of the audio, because the
/// entry is the thing that owns the bytes and the slot only points at it.
static inline SampleRef ResolveSample(int voice)
{
	if (voice < 0 || voice >= kNumVoices) return { nullptr, 0 };

	const int src = gVoiceSample[voice];
	// Either flavour of synth: no audio, and DrumKit uses SynthVoiceFor() to
	// decide whose character to render.
	if (src == kSourceSynth || IsSynthSource(src)) return { nullptr, 0 };

	if (IsUserSource(src))
	{
		const int e = src - kUserSourceBase;
		if (!UserEntryPresent(e)) return { nullptr, 0 };   // deleted: synth
		const UserSampleHeader *h = UserHeader();
		return { UserData() + h->offset[e], h->size[e] };
	}

	return BakedSample(src);
}

/// True if this slot is playing an UPLOADED sample rather than a baked one.
static inline bool VoiceIsUserLoaded(int voice)
{
	if (voice < 0 || voice >= kNumVoices) return false;
	return IsUserSource(gVoiceSample[voice]);
}

/// True if there is anything at all to play for this slot.
static inline bool VoiceHasSample(int voice)
{
	if (voice < 0 || voice >= kNumVoices) return false;
	return ResolveSample(voice).len > 0;
}

/// How many bytes of the user region are actually referenced by an entry.
/// Uploads APPEND, so this is what is live, not what has been written.
static inline uint32_t UserBytesUsed()
{
	if (!HaveUserSamples()) return 0;
	const UserSampleHeader *h = UserHeader();
	uint32_t n = 0;
	for (int e = 0; e < kMaxUserSamples; e++)
		if (h->size[e] > 0) n += h->size[e];
	return n;
}

} // namespace nko
