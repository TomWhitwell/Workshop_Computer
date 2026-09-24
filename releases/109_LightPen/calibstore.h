// calibstore.h — the calibration, kept in one flash sector.
//
// Same shape as WorkshopNibbleDrum's calibstore.h (magic + version header in a
// fixed sector 512KB in, so reflashing the firmware never moves or wipes it),
// but simpler to write safely: LightPen saves only AFTER ComputerCard::Run()
// has returned. Abort() has by then removed both of the library's interrupt
// handlers (DMA_IRQ_0 and PWM_IRQ_WRAP), so nothing can execute from flash
// while the erase has XIP switched off. main.cpp reboots straight afterwards.
//
// Version 2 adds the three primary captures and the calibration mode. A
// version-1 record still loads, as a two-point calibration; it is rewritten
// as version 2 the next time the user calibrates.
//
// Only the captures are stored. Everything derived from them — the power-law
// exponents, the un-mix matrix, the tables — is recomputed at every boot, so
// improving that maths takes effect without anyone recalibrating.
//
// Include from main.cpp only.

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico.h"
#include "calibration.h"

extern "C" char __flash_binary_end;

namespace lp {

// 512KB in: past any plausible firmware image (0.1MB today), inside the
// Pico's 2MB. Checked against the linked image size at run time below.
constexpr uint32_t kCalibFlashOffset = 512u * 1024;

constexpr uint32_t kCalibMagic   = 0x3143504Cu;   // "LPC1"
constexpr uint32_t kCalibVersion = 2;

struct CalibRecord
{
	uint32_t  magic;
	uint32_t  version;
	CalibData data;
	uint32_t  check;
};

/// The version-1 layout, for reading old records only.
struct CalibRecordV1
{
	uint32_t magic;
	uint32_t version;
	int32_t  black[3];
	int32_t  white[3];
	uint32_t check;
};

static_assert(sizeof(CalibRecord) <= FLASH_PAGE_SIZE, "CalibRecord must fit one flash page");

namespace detail {

/// Over every word before the trailing checksum, so both layouts can share it.
static inline uint32_t CalibChecksum(const void *record, size_t bytesBeforeCheck)
{
	const uint32_t *w = static_cast<const uint32_t *>(record);
	uint32_t sum = 0x5A5A5A5Au;
	for (size_t i = 0; i < bytesBeforeCheck / 4; i++) sum = (sum << 5) + sum + w[i];
	return sum;
}

static inline bool ImageClearOfCalib()
{
	return reinterpret_cast<uintptr_t>(&__flash_binary_end) <= XIP_BASE + kCalibFlashOffset;
}

// Can run from flash: flash_range_erase/program are RAM-resident in the SDK
// and restore XIP before returning. Interrupts are masked because any handler
// running mid-erase would fetch from flash.
static inline void WriteCalibPage(const uint8_t *page)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(kCalibFlashOffset, FLASH_SECTOR_SIZE);
	flash_range_program(kCalibFlashOffset, page, FLASH_PAGE_SIZE);
	restore_interrupts(ints);
}

} // namespace detail

/// A usable saved calibration, or false (never saved, erased, corrupt, from a
/// layout we do not know, or failing the same span check as a capture).
static inline bool LoadCalibration(CalibData &out)
{
	if (!detail::ImageClearOfCalib()) return false;
	const void *src = reinterpret_cast<const void *>(XIP_BASE + kCalibFlashOffset);

	uint32_t head[2];
	memcpy(head, src, sizeof(head));
	if (head[0] != kCalibMagic) return false;

	if (head[1] == 1)
	{
		CalibRecordV1 r;
		memcpy(&r, src, sizeof(r));
		if (r.check != detail::CalibChecksum(&r, offsetof(CalibRecordV1, check))) return false;
		out = CalibDefaults();
		memcpy(out.black, r.black, sizeof(r.black));
		memcpy(out.white, r.white, sizeof(r.white));
		out.mode = static_cast<uint8_t>(CalibMode::TwoPoint);
		return CalibFailMask(out) == 0;
	}

	if (head[1] != kCalibVersion) return false;
	CalibRecord r;
	memcpy(&r, src, sizeof(r));
	if (r.check != detail::CalibChecksum(&r, offsetof(CalibRecord, check))) return false;
	out = r.data;
	return CalibFailMask(out) == 0;
}

/// Only with audio stopped: after Run() has returned. Blocks for the erase
/// (tens of ms).
static inline void SaveCalibration(const CalibData &d)
{
	if (!detail::ImageClearOfCalib()) return;

	CalibRecord r{};
	r.magic = kCalibMagic;
	r.version = kCalibVersion;
	r.data = d;
	r.check = detail::CalibChecksum(&r, offsetof(CalibRecord, check));

	uint8_t page[FLASH_PAGE_SIZE];
	memset(page, 0xFF, sizeof(page));
	memcpy(page, &r, sizeof(r));
	detail::WriteCalibPage(page);
}

} // namespace lp
