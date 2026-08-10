// samplestore.h — where the card gets its recordings from.
//
// Two sources, in priority order:
//
//   1. USER FLASH  — a 1MB region at a fixed address, written over USB by the
//                    web app (interface.html). Overrides per mode, so you can
//                    replace just the geese and keep everything else.
//   2. BAKED       — compiled into the firmware from samples/*.raw. The factory
//                    default, so a fresh card works with no upload.
//
// The user region sits at a FIXED offset rather than after the code, so
// reflashing the firmware does not move or wipe it.

#pragma once
#include <stdint.h>
#include "biomimicry.h"
#include "voices.h"          // kNumVariants
#include "samples_default.h" // baked fallback

namespace bio {

// --- Flash layout ----------------------------------------------------------
// RP2040 XIP window. The user region is the last 1MB of a 2MB part.
constexpr uint32_t kFlashBase     = 0x10000000u;
constexpr uint32_t kFlashSize     = 2u * 1024 * 1024;
constexpr uint32_t kUserRegionOff = 1u * 1024 * 1024;   // offset within flash
constexpr uint32_t kUserRegionLen = 1u * 1024 * 1024;

/// Directory written at the start of the user region. Fixed size so it can be
/// erased and rewritten as one 4KB sector.
struct UserSampleHeader
{
	uint32_t magic;       // kUserMagic when a valid upload is present
	uint32_t version;     // layout version, for future changes
	// Byte offsets are relative to the START of the user region's data area,
	// and a size of 0 means "this slot is empty, fall back to baked".
	uint32_t offset[kNumModes][kNumVariants];
	uint32_t size[kNumModes][kNumVariants];
	uint32_t totalBytes;
	uint32_t reserved[8];
};

constexpr uint32_t kUserMagic   = 0x42494F31u;   // "BIO1"
constexpr uint32_t kUserVersion = 1;

/// Set in a slot's size[] to mean "play a BAKED recording, not uploaded audio":
/// offset[] then holds the mode and the low bits of size[] the variant. Sizes
/// never approach this, so the bit was always spare — and firmware that predates
/// it simply fails the bounds check and falls back to baked, which is exactly
/// what such a slot is asking for anyway.
constexpr uint32_t kBakedFlag = 0x80000000u;

// The header occupies the first flash sector of the region; audio follows.
// RP2040 flash erases a sector at a time; every erase must be sector-aligned.
constexpr uint32_t kFlashSector   = 4096;

constexpr uint32_t kUserHeaderLen = 4096;
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

/// Where a given mode+variant's audio actually lives, whichever source wins.
struct SampleRef
{
	const int8_t *data;
	uint32_t      len;
};

/// Resolve one slot. A user upload for that exact mode+variant wins; otherwise
/// the baked recording is used. This is what lets you replace one mode without
/// losing the rest.
static inline SampleRef ResolveSample(int mode, int variant)
{
	if (HaveUserSamples())
	{
		const UserSampleHeader *h = UserHeader();
		uint32_t sz = h->size[mode][variant];

		// Bit 31 set means "this slot plays a BAKED recording", with the mode in
		// offset[] and the variant in the low bits of size[].
		//
		// A slot's offset is relative to the user region, so before this there
		// was no way for the header to name a built-in recording at all — you
		// could not put the stock horse hoof on Geese 1 without re-uploading it.
		// A flag bit rather than a version bump because sizes are at most 128KB,
		// so the top bits were always free, and because older firmware reads the
		// huge size, fails its bounds check below, and falls back to baked —
		// which is the right answer rather than a crash.
		if (sz & kBakedFlag)
		{
			int bm = static_cast<int>(h->offset[mode][variant]);
			int bv = static_cast<int>(sz & ~kBakedFlag);
			if (kHaveSamples && bm >= 0 && bm < kNumModes &&
			    bv >= 0 && bv < kNumVariants)
				return { kModeSample[bm] + kModeSampleOff[bm][bv],
				         kModeSampleSize[bm][bv] };
			return { nullptr, 0 };
		}

		if (sz > 0 && h->offset[mode][variant] + sz <= kUserDataLen)
			return { UserData() + h->offset[mode][variant], sz };
	}

	if (kHaveSamples)
		return { kModeSample[mode] + kModeSampleOff[mode][variant],
		         kModeSampleSize[mode][variant] };

	return { nullptr, 0 };
}

/// True if a mode has ANY uploaded audio, so it should ignore its baked set.
static inline bool ModeIsUserLoaded(int mode)
{
	if (!HaveUserSamples()) return false;
	const UserSampleHeader *h = UserHeader();
	for (int v = 0; v < kNumVariants; v++)
		if (h->size[mode][v] > 0) return true;
	return false;
}

/// How many round-robin variants a mode should actually use.
///
/// If ANY slot of a mode has been uploaded, only the uploaded ones count. Upload
/// two geese and you get two geese — not your two mixed with six of the card's
/// own, which is what a blind round robin over all eight slots would give you and
/// is never what someone replacing a mode wants.
///
/// Counts the TOTAL, not the leading run. An earlier version counted the run and
/// so silently ignored everything after a gap, which made "add a sample to slot
/// 5" behave as if nothing had been uploaded. Gaps are handled by
/// PickUserVariant() instead, which walks past empty slots — so any combination
/// of filled slots works and the browser does not have to police the order.
static inline int VariantCount(int mode)
{
	if (HaveUserSamples())
	{
		const UserSampleHeader *h = UserHeader();
		int n = 0;
		for (int v = 0; v < kNumVariants; v++)
			if (h->size[mode][v] > 0) n++;
		if (n > 0) return n;
	}
	return kHaveSamples ? kNumVariants : 0;
}

/// Map a 0..VariantCount-1 pick onto an actual filled slot, skipping gaps.
///
/// For a mode with no uploads this is the identity, so baked sets are unchanged.
static inline int PickUserVariant(int mode, int pick)
{
	if (!ModeIsUserLoaded(mode)) return pick;

	const UserSampleHeader *h = UserHeader();
	int seen = 0;
	for (int v = 0; v < kNumVariants; v++)
	{
		if (h->size[mode][v] == 0) continue;
		if (seen == pick) return v;
		seen++;
	}
	return 0;   // pick out of range: fall back to the first filled slot
}

/// True if there is anything at all to play — either source.
static inline bool AnySamples()
{
	return kHaveSamples || HaveUserSamples();
}

} // namespace bio
