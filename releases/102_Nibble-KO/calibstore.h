// calibstore.h — the ten learned Four Voltages levels, saved to flash.
//
// Sibling to samplestore.h: same magic/version-header shape, own 4KB sector,
// fixed address so reflashing firmware never moves or wipes it. Unlike the
// sample region this is a single sector with no data area after it — ten
// int32_t levels plus a header comfortably fit in one page.
//
// levels.h used to say calibration was RAM-only, on the reasoning that the
// Four Voltages knob moves every learned level, so a saved calibration could
// silently restore a WRONG one after the knob gets bumped. That is still a
// real risk, but the knob is not expected to move in normal use, and
// recalibrating on every single power-up (today's behaviour, see main.cpp's
// TODO(calibstore) markers) makes the card untestable and unplayable within
// seconds of switching it on. Persisting wins the trade; the escape hatch is
// the alt-boot learn, unchanged and still one gesture away.
//
// WRITING: this is not the WebUI upload path (webui.cpp/WorkshopBio), so the
// full park-core-0 / mask-three-interrupts dance that a live USB upload needs
// does not apply here — TinyUSB is not built into this firmware at all (see
// CMakeLists.txt). But ProcessSample() is still a flash-resident DMA
// interrupt handler running on core 0, and flash_range_erase/program stall
// XIP, which would fault it mid-instruction. The five-step protocol still
// applies, minus the USB-specific steps:
//
//   1. Flag core 0 to stop touching flash-resident audio code and confirm
//      it has parked (main.cpp's SaveCalibration() does this: disables the
//      DMA IRQ, from the same core, between control ticks).
//   2. Disable DMA_IRQ_0 (the only flash-resident handler this firmware has;
//      no PWM_IRQ_WRAP, no USBCTRL_IRQ, since neither is used).
//   3. Prime the SDK's boot2 RAM copy with a zero-length erase.
//   4. Wrap the real erase/program in __not_in_flash_func with interrupts
//      saved.
//   5. Nothing needs buffering — the ten levels are already sitting in
//      LevelTracker's RAM state, read directly.
//
// Called only from main.cpp's LearnTick(), on the control tick, never from
// inside ProcessSample() as a whole — see SaveLevels()'s own comment.

#pragma once
#include <stdint.h>
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pico.h"
#include "nibbleko.h"   // kNumLevels

namespace nko {

// --- Flash layout ------------------------------------------------------
// kFlashBase/kFlashSize/kFlashSector live in nibbleko.h, which describes the
// physical part once for every region that carves it up.
//
// 512KB in: past any plausible firmware size, and 512KB clear of the
// user-sample region above. CMakeLists.txt's checksize.cmake step fails the
// build if the image ever grows far enough to reach this sector.
constexpr uint32_t kCalibRegionOff = 512u * 1024;
constexpr uint32_t kCalibSector    = kFlashSector;

struct CalibHeader
{
	uint32_t magic;                 // kCalibMagic when valid
	uint32_t version;                // layout version
	int32_t  level[kNumLevels];      // LevelTracker's learned level_[]
	uint8_t  collisions;             // LevelTracker::CollisionCount() at save
	uint8_t  reserved[3];
	uint32_t reservedWords[4];
};

constexpr uint32_t kCalibMagic   = 0x4B4F4331u;   // "KOC1"
constexpr uint32_t kCalibVersion = 1;

static_assert(sizeof(CalibHeader) <= kCalibSector,
              "CalibHeader must fit in one flash sector");

static inline const CalibHeader *CalibFlashHeader()
{
	return reinterpret_cast<const CalibHeader *>(kFlashBase + kCalibRegionOff);
}

/// True if a valid saved calibration exists to load.
static inline bool HaveSavedCalibration()
{
	const CalibHeader *h = CalibFlashHeader();
	return h->magic == kCalibMagic && h->version == kCalibVersion;
}

/// Copy the saved levels out, e.g. straight into LevelTracker::LearnFrom().
/// Caller must check HaveSavedCalibration() first.
static inline void LoadSavedCalibration(int32_t *levelsOut)
{
	memcpy(levelsOut, CalibFlashHeader()->level, sizeof(int32_t) * kNumLevels);
}

namespace detail {

static inline void __not_in_flash_func(EraseCalibSector)()
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(kCalibRegionOff, kCalibSector);
	restore_interrupts(ints);
}

static inline void __not_in_flash_func(ProgramCalibPage)(const uint8_t *page)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_program(kCalibRegionOff, page, 256);
	restore_interrupts(ints);
}

} // namespace detail

/// Save ten learned levels to flash. Synchronous: blocks for the duration of
/// the erase+program (a few ms), and disables/restores DMA_IRQ_0 itself, so
/// audio output stalls briefly rather than corrupting mid-word.
///
/// MUST be called from the control tick (core 0, interrupts otherwise
/// enabled), never from inside an interrupt handler — save_and_disable_
/// interrupts() nests fine, but this also directly disables DMA_IRQ_0 by
/// name, and re-enabling an IRQ that was already masked by its caller would
/// unmask audio mid-something-else. main.cpp's LearnTick() is the only
/// caller, and it runs there, not in ProcessSample() itself.
static inline void SaveCalibration(const int32_t *levels, uint8_t collisions)
{
	// A whole flash page (256B) buffered in RAM first: flash_range_program
	// writes in page-sized units and the header is smaller than one page, so
	// the rest of the page must be included (as 0xFF, erased-state filler)
	// rather than left to whatever flash_range_program does with a short
	// length.
	uint8_t page[256];
	memset(page, 0xFF, sizeof(page));

	CalibHeader h{};
	h.magic   = kCalibMagic;
	h.version = kCalibVersion;
	memcpy(h.level, levels, sizeof(int32_t) * kNumLevels);
	h.collisions = collisions;
	memcpy(page, &h, sizeof(h));

	// Silence the one flash-resident interrupt handler this firmware has.
	// No USB, no second PWM handler — see this file's header comment for why
	// that is less than WorkshopBio's webui.cpp needs.
	irq_set_enabled(DMA_IRQ_0, false);

	detail::EraseCalibSector();
	detail::ProgramCalibPage(page);

	irq_set_enabled(DMA_IRQ_0, true);
}

} // namespace nko
