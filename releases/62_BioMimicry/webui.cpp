// webui.cpp — USB-MIDI SysEx sample upload.

#include "webui.h"
#include "crosscore.h"
#include <string.h>
#include "tusb.h"
#include "profile.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "hardware/dma.h"

namespace bio {

volatile bool WebUI::usbMode = false;
volatile bool WebUI::uploadMode = false;
volatile bool WebUI::core0Parked = false;
volatile uint8_t WebUI::stage = 0;

// ---------------------------------------------------------------------------
// 7-bit encoding
// ---------------------------------------------------------------------------

uint32_t Encode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst,
                    uint32_t dstMax)
{
	uint32_t o = 0;
	for (uint32_t i = 0; i < srcLen; i += 7)
	{
		uint32_t n = (srcLen - i < 7) ? (srcLen - i) : 7;
		if (o + n + 1 > dstMax) break;
		uint8_t high = 0;
		for (uint32_t k = 0; k < n; k++)
			if (src[i + k] & 0x80) high |= static_cast<uint8_t>(1u << k);
		dst[o++] = high;
		for (uint32_t k = 0; k < n; k++)
			dst[o++] = src[i + k] & 0x7F;
	}
	return o;
}

uint32_t Decode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst,
                    uint32_t dstMax)
{
	uint32_t o = 0;
	for (uint32_t i = 0; i < srcLen; i += 8)
	{
		uint8_t high = src[i];
		uint32_t n = (srcLen - i - 1 < 7) ? (srcLen - i - 1) : 7;
		for (uint32_t k = 0; k < n; k++)
		{
			if (o >= dstMax) return o;
			dst[o++] = static_cast<uint8_t>(src[i + 1 + k]
			         | ((high & (1u << k)) ? 0x80 : 0x00));
		}
	}
	return o;
}

// ---------------------------------------------------------------------------
// Flash writing
// ---------------------------------------------------------------------------
//
// Erase and program stall XIP, so the caller must already have stopped audio
// (see WebUI::Uploading). Both cores must be quiet: core 1 is not running any
// BioMimicry code, but interrupts are disabled anyway because a flash-resident
// ISR firing mid-erase would hard-fault.

/// Stop the audio engine for the duration of an upload.
///
/// Writing flash means XIP goes down, and core 0 cannot survive that: its DMA
/// interrupt handler dispatches through a vtable in flash, so it faults before
/// any software guard could run. The previous attempt tried to park core 0 in a
/// RAM-resident spin and deadlocked the card every time.
///
/// So the card stops pretending to be a synth: the audio interrupt is switched
/// off, the outputs are zeroed, and the card reboots when the upload finishes.
/// The web UI already warns that the card goes silent -- this makes that true
/// rather than aspirational.
static void EnterUploadMode()
{
	if (WebUI::uploadMode) return;

	// Raise the flag FIRST, then wait for core 0 to acknowledge that it has
	// reached its RAM-resident park. Disabling the interrupt alone was not
	// enough and this is why the second attempt still hung:
	//
	//   - ComputerCard::AudioCallback and BufferFull live in FLASH, so core 0
	//     can be executing them at the moment XIP drops, and
	//   - the CV output runs a SECOND interrupt, PWM_IRQ_WRAP -> OnCVPWMWrap,
	//     also in flash, which kept firing after DMA_IRQ_0 was masked.
	//
	// Either one faults the instant flash_range_erase kills XIP. So core 0 now
	// parks itself in RAM and says so, and only then is it safe to write.
	WebUI::uploadMode = true;
	WebUI::stage = 1;

	// Wait for the park BEFORE masking anything. ProcessSample only runs when
	// DMA_IRQ_0 fires, so masking first would guarantee it never sees the flag
	// and never parks. At 48kHz the next callback is 21us away.
	absolute_time_t deadline = make_timeout_time_ms(250);
	while (!WebUI::core0Parked && !time_reached(deadline)) tight_loop_contents();

	WebUI::stage = 2;

	// Silence every flash-resident interrupt handler. DMA_IRQ_0 and PWM_IRQ_WRAP
	// are the audio path; USBCTRL_IRQ is TinyUSB's own, and dcd_rp2040_irq lives
	// in flash like the rest of the stack. THIS is why upload hung at stage 5
	// with the erase already done: the erase itself was fine, but the moment
	// interrupts came back a pending USB interrupt vectored into flash and the
	// stack never recovered, so the ack was queued and never sent.
	//
	// Nothing can talk to the host after this point, which is why the whole
	// upload is buffered in RAM first and only committed here.
	irq_set_enabled(DMA_IRQ_0, false);
	irq_set_enabled(PWM_IRQ_WRAP, false);
	irq_set_enabled(USBCTRL_IRQ, false);

	// Prime the SDK's boot2 copy while XIP is definitely still up.
	//
	// flash_range_erase/program restore XIP afterwards by calling a RAM copy of
	// the boot2 stage, and flash_init_boot2_copyout() lazily populates that copy
	// by reading FROM XIP the first time any flash function runs. A zero-length
	// erase reaches that initialiser (it runs before the "no flash accesses"
	// barrier) and does nothing else, so the copy is taken here, unhurried,
	// rather than during the first real write.
	flash_range_erase(kUserRegionOff, 0);
	WebUI::stage = 3;
}

/// Keep servicing USB for `ms` so queued replies actually reach the host.
///
/// tud_midi_stream_write only fills a buffer; tud_task() moves it onto the wire.
/// Anything that replies and then immediately reboots must pump the stack in
/// between, or the reply dies with the reboot.
static void __not_in_flash_func(FlushUsb)(uint32_t ms)
{
	absolute_time_t until = make_timeout_time_ms(ms);
	while (!time_reached(until)) tud_task();
}

static void __not_in_flash_func(EraseRegion)(uint32_t off, uint32_t len)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(off, len);
	restore_interrupts(ints);
}

static void __not_in_flash_func(ProgramPage)(uint32_t off, const uint8_t *data)
{
	uint32_t ints = save_and_disable_interrupts();
	flash_range_program(off, data, 256);
	restore_interrupts(ints);
}

// ---------------------------------------------------------------------------

void WebUI::Init()
{
	tusb_init();
	memset(&hdr_, 0, sizeof(hdr_));
}

int32_t __not_in_flash_func(WebUI::Progress)() const
{
	if (!uploading_ || expected_ == 0) return 0;
	uint64_t p = static_cast<uint64_t>(writeOff_) * kQ16One / expected_;
	return (p > kQ16One) ? kQ16One : static_cast<int32_t>(p);
}

void __not_in_flash_func(WebUI::Task)()
{
	tud_task();

	uint8_t packet[4];
	while (tud_midi_available())
	{
		if (!tud_midi_packet_read(packet)) break;

		// Reassemble SysEx from USB-MIDI 4-byte packets. The code-index number
		// in the low nibble tells us how many of the three data bytes are real.
		uint8_t cin = packet[0] & 0x0F;
		uint32_t n = 0;
		switch (cin)
		{
		case 0x4: n = 3; break;                 // sysex continues
		case 0x5: n = 1; break;                 // ends with 1 byte
		case 0x6: n = 2; break;                 // ends with 2 bytes
		case 0x7: n = 3; break;                 // ends with 3 bytes
		default:  n = 0; break;
		}

		for (uint32_t i = 0; i < n; i++)
		{
			uint8_t b = packet[1 + i];
			if (b == 0xF0) { inSysex_ = true; rxLen_ = 0; continue; }
			if (b == 0xF7)
			{
				if (inSysex_) HandleSysex(rx_, rxLen_);
				inSysex_ = false;
				rxLen_ = 0;
				continue;
			}
			if (inSysex_ && rxLen_ < sizeof(rx_)) rx_[rxLen_++] = b;
		}
	}

	// Anything queued by a reply above is pushed here, outside the packet loop,
	// where re-entering tud_task() is safe. During an upload the next thing the
	// caller does is an erase or a page program with interrupts off, so without
	// this the ack would sit in the FIFO while the browser -- which waits for one
	// before sending the next chunk -- timed out.
	if (txPending_)
	{
		txPending_ = false;
		tud_task();
	}
}

void __not_in_flash_func(WebUI::Send)(const uint8_t *payload, uint32_t len)
{
	uint8_t buf[64];
	if (len + 3 > sizeof(buf)) return;
	uint32_t o = 0;
	buf[o++] = 0xF0;
	buf[o++] = kManufacturerId;
	memcpy(buf + o, payload, len);
	o += len;
	buf[o++] = 0xF7;
	tud_midi_stream_write(0, buf, o);

	// Do NOT call tud_task() here to push it out. That was tried, and it is a
	// reentrancy bug: Send() is reached from inside Task()'s packet loop via
	// HandleSysex(), so a nested tud_task() re-enters that loop and corrupts
	// rx_/rxLen_ half way through parsing the message being replied to.
	//
	// Task() flushes on the way out instead -- see the pump at the end of it.
	txPending_ = true;
}

void __not_in_flash_func(WebUI::SendAck)(uint8_t code, uint32_t value)
{
	uint8_t p[6] = { MSG_UP_ACK, code,
	                 static_cast<uint8_t>((value >> 14) & 0x7F),
	                 static_cast<uint8_t>((value >> 7)  & 0x7F),
	                 static_cast<uint8_t>( value        & 0x7F), 0 };
	Send(p, 5);
}

void __not_in_flash_func(WebUI::SendErr)(uint8_t code)
{
	uint8_t p[2] = { MSG_UP_ERR, code };
	Send(p, 2);
	uploading_ = false;
}

void __not_in_flash_func(WebUI::FlushPage)()
{
	if (pageFill_ == 0) return;
	// Pad the tail so a partial page still programs cleanly.
	memset(page_ + pageFill_, 0, 256 - pageFill_);
	ProgramPage(pageAddr_, page_);
	pageAddr_ += 256;
	pageFill_ = 0;
}

/// Commit the RAM-staged upload to flash. Runs only after EnterUploadMode(),
/// with audio stopped and USB already dead — nothing here can report progress,
/// so the LED stage counter is the only visible signal.
void __not_in_flash_func(WebUI::WriteStagedBuffer)()
{
	// Where the new audio lands, appended after whatever is already stored and
	// rounded up to a sector so the erase cannot clip the previous recording.
	uint32_t base = (baseOff_ + kFlashSector - 1u) & ~(kFlashSector - 1u);
	uint32_t dst  = kUserDataOff + base;

	uint32_t end = (dst + bufLen_ + kFlashSector - 1u) & ~(kFlashSector - 1u);
	if (end > dst) EraseRegion(dst, end - dst);
	WebUI::stage = 5;

	// Whole 256-byte pages, then a padded tail.
	uint32_t off = 0;
	while (off + 256 <= bufLen_)
	{
		ProgramPage(dst + off, buf_ + off);
		off += 256;
	}
	if (off < bufLen_)
	{
		memset(page_, 0, sizeof(page_));
		memcpy(page_, buf_ + off, bufLen_ - off);
		ProgramPage(dst + off, page_);
	}
	WebUI::stage = 6;

	// Staging-buffer offsets become flash offsets now that the base is known.
	for (int m = 0; m < kNumModes; m++)
		for (int v = 0; v < kNumVariants; v++)
			if (hdr_.size[m][v] > 0 && touched_[m][v])
				hdr_.offset[m][v] += base;

	writeOff_ = base + bufLen_;
	CommitHeader();
}

void __not_in_flash_func(WebUI::CommitHeader)()
{
	FlushPage();
	hdr_.magic = kUserMagic;
	hdr_.version = kUserVersion;
	hdr_.totalBytes = writeOff_;

	// The header lives in its own sector, written last: until this lands the
	// magic is absent, so an interrupted upload falls back to baked samples
	// rather than pointing at half-written audio.
	static uint8_t sector[4096];
	memset(sector, 0xFF, sizeof(sector));
	memcpy(sector, &hdr_, sizeof(hdr_));
	EraseRegion(kUserRegionOff, 4096);
	for (uint32_t i = 0; i < 4096; i += 256)
		ProgramPage(kUserRegionOff + i, sector + i);
}

void __not_in_flash_func(WebUI::HandleSysex)(const uint8_t *msg, uint32_t len)
{
	if (len < 2 || msg[0] != kManufacturerId) return;
	const uint8_t *p = msg + 1;
	uint32_t n = len - 1;

	switch (p[0])
	{
	case MSG_HELLO:
	{
		// Report version, capacity and whether user samples are loaded, so the
		// browser can show what is actually on the card.
		// Bytes already committed, so the browser can say how much room is left.
		uint32_t used = 0;
		if (HaveUserSamples())
		{
			const UserSampleHeader *uh = UserHeader();
			used = uh->totalBytes;
			if (used > kUserDataLen) used = kUserDataLen;
		}

		uint8_t info[14] = {
			MSG_INFO, kUserVersion,
			static_cast<uint8_t>(HaveUserSamples() ? 1 : 0),
			static_cast<uint8_t>(kHaveSamples ? 1 : 0),
			// Report the STAGING BUFFER size, not the flash region. One upload
			// is now limited by the RAM it is buffered in, not by the 1MB it
			// eventually lands in -- the browser must reject on the real limit.
			static_cast<uint8_t>((kUploadMax >> 14) & 0x7F),
			static_cast<uint8_t>((kUploadMax >> 7)  & 0x7F),
			static_cast<uint8_t>( kUploadMax        & 0x7F),
			static_cast<uint8_t>(kNumModes),
			// How much of the 1MB region is spoken for, and how big it is. The
			// per-upload cap above is a TRANSFER limit set by the RAM buffer, not
			// a storage limit -- uploads append, so several passes fill the card.
			// Without these the browser can only report the transfer cap, and
			// "too big" reads as "the card is full" when it usually is not.
			static_cast<uint8_t>((used >> 14) & 0x7F),
			static_cast<uint8_t>((used >> 7)  & 0x7F),
			static_cast<uint8_t>( used        & 0x7F),
			static_cast<uint8_t>((kUserDataLen >> 14) & 0x7F),
			static_cast<uint8_t>((kUserDataLen >> 7)  & 0x7F),
			static_cast<uint8_t>( kUserDataLen        & 0x7F) };
		Send(info, 14);
		break;
	}

	case MSG_UP_BEGIN:
	{
		// payload: 3 septets of total byte count
		if (n < 4) { SendErr(ERR_PROTOCOL); break; }
		expected_ = (static_cast<uint32_t>(p[1]) << 14)
		          | (static_cast<uint32_t>(p[2]) << 7)
		          |  static_cast<uint32_t>(p[3]);
		if (expected_ > kUserDataLen) { SendErr(ERR_TOO_BIG); break; }

		// NOTE: the card keeps playing through the whole transfer now. Flash is
		// not touched until MSG_UP_END, because writing it means killing USB --
		// TinyUSB's device stack and its USBCTRL_IRQ handler are all in flash, so
		// the card cannot receive the next chunk and write the last one at the
		// same time. Everything is buffered in RAM and committed in one go.
		uploading_ = true;
		pageFill_ = 0;

		// Start from what is already on the card rather than a blank table, so
		// uploading one slot leaves the other 47 alone. Previously every upload
		// memset the header and erased all 1MB, which meant replacing a single
		// 6KB recording destroyed the whole library -- and the erase itself was
		// a multi-second stall the browser read as a hang.
		const UserSampleHeader *cur = UserHeader();
		if (cur->magic == kUserMagic && cur->version == kUserVersion)
		{
			memcpy(&hdr_, cur, sizeof(hdr_));
			baseOff_ = cur->totalBytes;
		}
		else
		{
			memset(&hdr_, 0, sizeof(hdr_));
			baseOff_ = 0;
		}
		memset(touched_, 0, sizeof(touched_));
		memset(modeCleared_, 0, sizeof(modeCleared_));

		WebUI::stage = 4;

		// New audio is appended after whatever is already stored, and the append
		// point is rounded UP to a sector boundary. Erase works a whole sector at
		// a time, so starting mid-sector would erase the tail of the recording
		// already sitting there -- silently corrupting a slot this upload never
		// touched, which is the exact failure this change exists to prevent.
		writeOff_ = (writeOff_ + kFlashSector - 1u) & ~(kFlashSector - 1u);
		if (writeOff_ + expected_ > kUserDataLen) { SendErr(ERR_TOO_BIG); break; }
		pageAddr_ = kUserDataOff + writeOff_;

		// No erase here any more -- see above. Just check it will fit the RAM
		// staging buffer, which is the real limit on one upload now.
		if (expected_ > sizeof(buf_)) { SendErr(ERR_TOO_BIG); break; }
		bufLen_ = 0;

		WebUI::stage = 5;
		SendAck(0, 0);
		break;
	}

	case MSG_UP_SLOT:
	{
		if (!uploading_ || n < 3) { SendErr(ERR_PROTOCOL); break; }
		slotMode_ = p[1];
		slotVariant_ = p[2];
		if (slotMode_ >= kNumModes || slotVariant_ >= kNumVariants)
		{
			SendErr(ERR_BAD_SLOT);
			break;
		}
		// Optional 4th byte: 1 = this upload REPLACES the mode, 0/absent = ADDS
		// to it. The stock web UI always sends 0 — clearing a mode is done with
		// MSG_CLEARMODE, an explicit button, rather than a flag on an upload
		// whose effect only becomes visible after the card reboots.
		//
		// The flag stays in the protocol because it is free and because a
		// different client may want a one-shot replace. Clearing unconditionally
		// on the first slot was the bug it replaced: uploading just slot 2 to sit
		// alongside an existing slot 1 wiped slot 1.
		bool replaceMode = (n >= 4) && (p[3] != 0);
		if (replaceMode && !modeCleared_[slotMode_])
		{
			modeCleared_[slotMode_] = true;
			for (int v = 0; v < kNumVariants; v++)
			{
				hdr_.offset[slotMode_][v] = 0;
				hdr_.size[slotMode_][v]   = 0;
				touched_[slotMode_][v]    = false;
			}
		}

		// Slots are staged in RAM, each starting page-aligned so it can still be
		// programmed independently when the whole lot is committed at UP_END.
		bufLen_ = (bufLen_ + 255u) & ~255u;
		slotStart_ = bufLen_;
		slotLen_ = 0;
		SendAck(1, bufLen_);
		break;
	}

	case MSG_UP_ALIAS:
	{
		// Point this slot at audio that has ALREADY been sent, instead of
		// sending it again. payload: mode, variant, srcMode, srcVariant.
		//
		// The header is a plain (offset, size) pair per slot, so nothing stops
		// two slots naming the same bytes — the firmware plays it in both places
		// without knowing they are shared. That makes reuse free: map one
		// recording onto four hooves and it costs the flash of one recording.
		if (!uploading_ || n < 5) { SendErr(ERR_PROTOCOL); break; }
		uint8_t dm = p[1], dv = p[2], sm = p[3], sv = p[4];
		if (dm >= kNumModes || dv >= kNumVariants ||
		    sm >= kNumModes || sv >= kNumVariants) { SendErr(ERR_BAD_SLOT); break; }
		if (hdr_.size[sm][sv] == 0) { SendErr(ERR_PROTOCOL); break; }

		// Read the source BEFORE any clearing. When the alias points at a slot in
		// the SAME mode — one recording mapped onto two hooves, the obvious case —
		// clearing the destination's mode would wipe the source first and the
		// copy would then write zeros. Silently, because nothing checks a slot
		// against itself.
		uint32_t srcOff = hdr_.offset[sm][sv];
		uint32_t srcSz  = hdr_.size[sm][sv];
		bool     srcTouched = touched_[sm][sv];

		if (!modeCleared_[dm])
		{
			modeCleared_[dm] = true;
			for (int v = 0; v < kNumVariants; v++)
			{
				hdr_.offset[dm][v] = 0;
				hdr_.size[dm][v]   = 0;
				touched_[dm][v]    = false;
			}
		}
		hdr_.offset[dm][dv] = srcOff;
		hdr_.size[dm][dv]   = srcSz;
		touched_[dm][dv]    = srcTouched;
		SendAck(8, hdr_.size[dm][dv]);
		break;
	}

	case MSG_UP_DROP:
	{
		// Empty a mode as part of the CURRENT session, staged like every other
		// change and committed at UP_END. MSG_CLEARMODE does the same thing but
		// commits and reboots immediately, which would abort a sync half way
		// through - so this exists for "the page emptied a mode the card holds".
		if (!uploading_ || n < 2) { SendErr(ERR_PROTOCOL); break; }
		uint8_t m = p[1];
		if (m >= kNumModes) { SendErr(ERR_BAD_SLOT); break; }
		modeCleared_[m] = true;
		for (int v = 0; v < kNumVariants; v++)
		{
			hdr_.offset[m][v] = 0;
			hdr_.size[m][v]   = 0;
			touched_[m][v]    = false;
		}
		SendAck(9, 0);
		break;
	}

	case MSG_UP_BAKED:
	{
		// Point a slot at a BUILT-IN recording: payload mode, variant, srcMode,
		// srcVariant. Costs no flash and needs no audio sent, so the stock horse
		// hoof can be Geese 1 without re-uploading it.
		if (!uploading_ || n < 5) { SendErr(ERR_PROTOCOL); break; }
		uint8_t dm = p[1], dv = p[2], sm = p[3], sv = p[4];
		if (dm >= kNumModes || dv >= kNumVariants ||
		    sm >= kNumModes || sv >= kNumVariants) { SendErr(ERR_BAD_SLOT); break; }

		if (!modeCleared_[dm])
		{
			modeCleared_[dm] = true;
			for (int v = 0; v < kNumVariants; v++)
			{
				hdr_.offset[dm][v] = 0;
				hdr_.size[dm][v]   = 0;
				touched_[dm][v]    = false;
			}
		}
		hdr_.offset[dm][dv] = sm;                    // the baked MODE
		hdr_.size[dm][dv]   = kBakedFlag | sv;       // flag + the baked VARIANT
		// NOT touched_: the commit pass rebases staging offsets into flash
		// offsets, and this offset is a mode index, not an offset at all.
		touched_[dm][dv]    = false;
		SendAck(10, 0);
		break;
	}

	case MSG_UP_KEEP:
	{
		// Keep audio that is ALREADY in the user region: payload mode, variant,
		// then 3 septets of its offset.
		//
		// A sync is authoritative per mode, so it clears each mode it touches -
		// which would delete audio the page is not re-sending simply because the
		// page was reloaded and no longer has the file. This re-points the slot
		// at bytes that are already there, so reloading the editor and adding one
		// more sample does not destroy the previous four.
		if (!uploading_ || n < 6) { SendErr(ERR_PROTOCOL); break; }
		uint8_t dm = p[1], dv = p[2];
		if (dm >= kNumModes || dv >= kNumVariants) { SendErr(ERR_BAD_SLOT); break; }
		uint32_t off = (static_cast<uint32_t>(p[3]) << 14)
		             | (static_cast<uint32_t>(p[4]) << 7)
		             |  static_cast<uint32_t>(p[5]);

		// Find that offset in the CURRENT on-flash header to recover its size —
		// the page knows where the audio is, not how long it is.
		uint32_t sz = 0;
		const UserSampleHeader *cur = UserHeader();
		if (cur->magic == kUserMagic && cur->version == kUserVersion)
			for (int m = 0; m < kNumModes && !sz; m++)
				for (int v = 0; v < kNumVariants; v++)
					if (cur->offset[m][v] == off && cur->size[m][v] > 0 &&
					    !(cur->size[m][v] & kBakedFlag))
						{ sz = cur->size[m][v]; break; }
		if (!sz) { SendErr(ERR_PROTOCOL); break; }

		if (!modeCleared_[dm])
		{
			modeCleared_[dm] = true;
			for (int v = 0; v < kNumVariants; v++)
			{
				hdr_.offset[dm][v] = 0;
				hdr_.size[dm][v]   = 0;
				touched_[dm][v]    = false;
			}
		}
		hdr_.offset[dm][dv] = off;    // already a flash offset
		hdr_.size[dm][dv]   = sz;
		touched_[dm][dv]    = false;  // so the commit rebase leaves it alone
		SendAck(11, sz);
		break;
	}

	case MSG_UP_CHUNK:
	{
		if (!uploading_) { SendErr(ERR_PROTOCOL); break; }
		uint8_t dec[512];
		uint32_t got = Decode7bit(p + 1, n - 1, dec, sizeof(dec));
		if (bufLen_ + got > sizeof(buf_)) { SendErr(ERR_TOO_BIG); break; }

		// Straight into RAM. Flash is untouched until UP_END, because writing it
		// takes USB down with it.
		memcpy(buf_ + bufLen_, dec, got);
		bufLen_ += got;
		slotLen_ += got;
		WebUI::stage = 6;
		SendAck(2, bufLen_);
		break;
	}

	case MSG_UP_SLOTEND:
		if (!uploading_) { SendErr(ERR_PROTOCOL); break; }
		// Record where this slot landed in the staging buffer; the offsets are
		// rewritten to flash offsets when the buffer is committed.
		hdr_.offset[slotMode_][slotVariant_] = slotStart_;
		hdr_.size[slotMode_][slotVariant_] = slotLen_;
		touched_[slotMode_][slotVariant_] = true;
		SendAck(3, slotLen_);
		break;

	case MSG_UP_END:
	{
		if (!uploading_) { SendErr(ERR_PROTOCOL); break; }
		uploading_ = false;

		// Drop baked references in modes that ended up with no uploaded audio.
		//
		// Such a slot is asking to play a built-in recording, which is exactly
		// what an EMPTY slot already does - so the reference buys nothing, and a
		// wrong one is worse than nothing: a cross-mode reference makes a mode
		// nobody touched play another mode's recordings, and because the editor
		// seeds its mapping from the card, it survives every reload. That is how
		// Cicadas came to play hooves.
		//
		// Checked here rather than per message because only the finished header
		// shows whether a mode has real audio: the references can arrive before
		// the uploads they sit alongside.
		for (int m = 0; m < kNumModes; m++)
		{
			bool haveAudio = false;
			for (int v = 0; v < kNumVariants; v++)
				if (hdr_.size[m][v] != 0 && !(hdr_.size[m][v] & kBakedFlag))
					{ haveAudio = true; break; }
			if (haveAudio) continue;
			for (int v = 0; v < kNumVariants; v++)
				if (hdr_.size[m][v] & kBakedFlag)
				{
					hdr_.offset[m][v] = 0;
					hdr_.size[m][v]   = 0;
				}
		}

		// Ack and push it out BEFORE any of this touches flash. Once the write
		// starts, USB is gone -- TinyUSB's stack and its USBCTRL_IRQ handler live
		// in flash -- so this is the last chance to say anything to the browser.
		// It is an "about to commit" ack, not a "committed" one, and the browser
		// treats the disconnect that follows as success.
		SendAck(4, bufLen_);
		FlushUsb(150);

		// Everything below runs with the card silent and USB dead.
		EnterUploadMode();
		WriteStagedBuffer();
		WebUI::stage = 7;

		watchdog_reboot(0, 0, 0);
		break;
	}

#ifdef BIO_PROFILE
	// Only exists in profile builds, so the released firmware is byte-identical
	// to one built without the profiler at all. The browser treats no reply as
	// "this card has no profiler".
	case MSG_PROF_GET:
	{
		// Exact cycle counts beat asking someone to decode blinking LEDs.
		// Each value is 3 septets (21 bits), plenty for a 4000-cycle budget.
		uint8_t p2[2 + kNumProf * 3 + 6];
		uint32_t o = 0;
		p2[o++] = MSG_PROF;
		p2[o++] = static_cast<uint8_t>(kNumProf);
		for (int i = 0; i < kNumProf; i++)
		{
			uint32_t v = gProf[i].peak;
			p2[o++] = static_cast<uint8_t>((v >> 14) & 0x7F);
			p2[o++] = static_cast<uint8_t>((v >> 7)  & 0x7F);
			p2[o++] = static_cast<uint8_t>( v        & 0x7F);
		}
		uint32_t ov = gProf[0].overruns;
		p2[o++] = static_cast<uint8_t>((ov >> 14) & 0x7F);
		p2[o++] = static_cast<uint8_t>((ov >> 7)  & 0x7F);
		p2[o++] = static_cast<uint8_t>( ov        & 0x7F);

		// Core 1's worst backlog: how many control ticks it owed at once. 1 is
		// healthy; a steady 2+ means it cannot keep up with the 1.5kHz grid and
		// the physics are running in slow motion.
		uint32_t bl = gXC.maxBacklog;
		p2[o++] = static_cast<uint8_t>((bl >> 14) & 0x7F);
		p2[o++] = static_cast<uint8_t>((bl >> 7)  & 0x7F);
		p2[o++] = static_cast<uint8_t>( bl        & 0x7F);
		Send(p2, o);
		gXC.maxBacklog = 0;
		ProfileReset();          // so each query reports a fresh window
		break;
	}
#endif

	case MSG_ERASE:
		// Wipe just the header sector: the audio stays but is unreferenced, so
		// the card falls straight back to its baked samples. This is also how
		// space is reclaimed -- uploads append, so re-uploading the same slot
		// repeatedly eventually fills the region, and this resets it to empty.
		//
		// Stops audio and reboots exactly like an upload: this erases flash, so
		// it cannot run while the engine is live.
		EnterUploadMode();
		EraseRegion(kUserRegionOff, kFlashSector);
		SendAck(5, 0);
		FlushUsb(200);
		watchdog_reboot(0, 0, 0);
		break;

	case MSG_SLOTS:
	{
		// One septet per mode: bit v set means slot v holds user audio. Lets the
		// browser show what is actually ON THE CARD rather than only what is
		// staged in the page, which is the difference between "these two geese
		// are loaded" and "these two geese are what the card will play".
		// TWO septets per mode, not one: SysEx data bytes carry 7 bits and there
		// are 8 variants, so a single byte would silently drop slot 8.
		static_assert(kNumVariants <= 14, "two septets per mode covers 14 slots");
		uint8_t sl[2 + kNumModes * 3];
		uint32_t o = 0;
		sl[o++] = MSG_SLOTS;
		sl[o++] = static_cast<uint8_t>(kNumModes);
		const UserSampleHeader *h = UserHeader();
		bool have = HaveUserSamples();
		for (int m = 0; m < kNumModes; m++)
		{
			uint32_t bits = 0;
			if (have)
				for (int v = 0; v < kNumVariants; v++)
					if (h->size[m][v] > 0) bits |= (1u << v);
			sl[o++] = static_cast<uint8_t>( bits       & 0x7F);
			sl[o++] = static_cast<uint8_t>((bits >> 7) & 0x7F);

			// How many DISTINCT baked recordings this mode actually has. Slots
			// are padded by repetition when a mode ships fewer than eight -
			// Meteors has five - so "8 variants" was a lie the page had no way
			// of knowing about.
			// Compared by OFFSET rather than by resolving each slot: the
			// resolver is an inline in a header, and 64 instantiations of it
			// here grew the image past the user region and tripped checksize.
			int distinct = 0;
			if (kHaveSamples)
				for (int v = 0; v < kNumVariants; v++)
				{
					bool dup = false;
					for (int k = 0; k < v && !dup; k++)
						if (kModeSampleOff[m][k] == kModeSampleOff[m][v]) dup = true;
					if (!dup) distinct++;
				}
			sl[o++] = static_cast<uint8_t>(distinct);
		}
		Send(sl, o);
		break;
	}

	case MSG_SLOTINFO:
	{
		// Per-slot OFFSET and SIZE for one mode, so the browser can show what is
		// on the card after a reboot rather than only what this page session
		// happened to load.
		//
		// Offset is the useful part: two slots sharing one offset are sharing one
		// recording, which is how the mapping UI can redraw "file 2 is used by
		// Horses 1 and Horses 3" without the page having to remember it.
		//
		// One mode per request: eight slots x two 3-septet fields is 48 bytes,
		// and Send() has a 64-byte buffer.
		// payload: mode, then which HALF of the slots (0 or 1).
		//
		// Four slots per reply, not eight. USB-MIDI carries 3 data bytes per
		// 4-byte packet, so all eight needed 72 FIFO bytes against a TX buffer
		// that was 64 - the reply could never be sent whole. Splitting it keeps
		// each message comfortably inside even the default buffer.
		if (n < 3) { SendErr(ERR_PROTOCOL); break; }
		uint8_t m = p[1];
		uint8_t half = p[2] ? 1 : 0;
		if (m >= kNumModes) { SendErr(ERR_BAD_SLOT); break; }
		constexpr int kPerMsg = kNumVariants / 2;
		static_assert(kPerMsg * 2 == kNumVariants,
		              "kNumVariants must be even to split the reply in half");
		const int v0 = half * kPerMsg;

		uint8_t sd[4 + kPerMsg * 6];
		sd[0] = MSG_SLOTDET;
		sd[1] = m;
		sd[2] = half;
		sd[3] = static_cast<uint8_t>(kPerMsg);
		const UserSampleHeader *h = UserHeader();
		bool have = HaveUserSamples();
		// Indexed rather than run through a counter, so the bound is obvious to
		// the compiler as well as to the reader — with a running uint32_t it
		// could not prove the writes stayed inside sd[] and warned on every one.
		for (int i = 0; i < kPerMsg; i++)
		{
			int v = v0 + i;
			uint32_t off = have ? h->offset[m][v] : 0;
			uint32_t sz  = have ? h->size[m][v]   : 0;

			// A BAKED slot carries the flag in bit 31 and a mode/variant rather
			// than an offset and a length. Reporting it raw truncated to 21
			// septets, so the flag vanished and the browser saw a 0 KB
			// "recording" whose offset was really a mode index - five phantom
			// USER entries, and then a keep request for an offset that does not
			// exist, which is the error 3.
			//
			// Reported as offset 0x1FFFFF (unreachable: the region is 0xFF000)
			// with the mode and variant in the size field, so the browser can
			// tell the two apart.
			if (sz & kBakedFlag)
			{
				off = 0x1FFFFFu;
				sz  = (static_cast<uint32_t>(h->offset[m][v]) << 8)
				    | (sz & 0xFFu);
			}
			else if (sz == 0) off = 0;
			uint8_t *q = &sd[4 + i * 6];
			q[0] = static_cast<uint8_t>((off >> 14) & 0x7F);
			q[1] = static_cast<uint8_t>((off >> 7)  & 0x7F);
			q[2] = static_cast<uint8_t>( off        & 0x7F);
			q[3] = static_cast<uint8_t>((sz  >> 14) & 0x7F);
			q[4] = static_cast<uint8_t>((sz  >> 7)  & 0x7F);
			q[5] = static_cast<uint8_t>( sz         & 0x7F);
		}
		Send(sd, sizeof(sd));
		break;
	}

	case MSG_CLEARMODE:
	{
		// Revert ONE mode to its baked recordings, leaving every other mode's
		// uploads alone. Only the header changes: the audio stays in flash but
		// becomes unreferenced, and ResolveSample falls back per-slot, so this
		// costs one sector write rather than a rewrite of the whole region.
		if (n < 2) { SendErr(ERR_PROTOCOL); break; }
		uint8_t m = p[1];
		if (m >= kNumModes) { SendErr(ERR_BAD_SLOT); break; }
		if (!HaveUserSamples()) { SendAck(7, 0); break; }

		EnterUploadMode();

		memcpy(&hdr_, UserHeader(), sizeof(hdr_));
		for (int v = 0; v < kNumVariants; v++)
		{
			hdr_.offset[m][v] = 0;
			hdr_.size[m][v]   = 0;
		}
		// totalBytes is the append high-water mark, deliberately NOT reduced:
		// the freed audio sits in the middle of the region, so pulling the mark
		// back would let the next upload overwrite a mode that is still in use.
		// "Revert to built-in" is what reclaims the space.
		CommitHeader();

		SendAck(7, 0);
		FlushUsb(150);
		watchdog_reboot(0, 0, 0);
		break;
	}

	case MSG_PLAY:
		// Leave USB mode and go back to being an instrument. No flash is touched
		// here, so there is nothing to park for -- just ack, get the reply out
		// while USB still exists, and reboot. Holding the switch again does the
		// same thing from the card itself.
		SendAck(6, 0);
		FlushUsb(150);
		watchdog_reboot(0, 0, 0);
		break;

	default:
		break;
	}
}

} // namespace bio
