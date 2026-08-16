// webui.cpp — USB-MIDI SysEx sample upload.
//
// Ported from WorkshopBio/webui.cpp, which is the hardware-proven reference
// for every hazardous part of this: the core-park handshake, the three
// interrupts that must be masked, priming the boot2 RAM copy, wrapping the
// writes in __not_in_flash_func, and buffering the whole upload in RAM before
// any of it is committed. Read docs/LESSONS.md §"If you let users upload
// samples" before changing anything below the codec — this is the one place on
// this platform where skipping a step HANGS THE CHIP rather than producing a
// wrong answer.
//
// WHAT IS DIFFERENT FROM THE REFERENCE. BioMimicry addresses slots as
// mode x variant; NIBBLE-KO has twelve flat voice slots, so every doubly
// nested loop there becomes a single loop here. That also removes a whole
// class of bug the reference had to grow defences against: it clears a MODE
// when any slot in it is uploaded, so a cross-mode baked reference could make
// an untouched mode play another mode's recordings. With flat slots there is
// no mode to clear, so uploading one voice simply cannot disturb another, and
// modeCleared_/kBakedFlag have no counterpart here.
//
// The other difference is how the card gets here: BioMimicry enumerates after
// the switch is held for two seconds, NIBBLE-KO after the switch+B+D combo
// sets WebUI::usbMode (see main.cpp's Action::EnterWebUi). Same modal design —
// TinyUSB is not initialised, and the card does not appear on USB at all,
// until the player asks for it.

#include "webui.h"
#include "fastmath.h"        // kQ16One, for Progress()
#include <string.h>
#include "tusb.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "hardware/dma.h"

namespace nko {

volatile bool WebUI::usbMode = false;
volatile bool WebUI::uploadMode = false;
volatile bool WebUI::core0Parked = false;
volatile uint8_t WebUI::stage = 0;

// ---------------------------------------------------------------------------
// 7-bit encoding — pure data transform, no hardware dependency, ported
// unchanged from WorkshopBio/webui.cpp.
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

/// Stop the audio engine for the duration of a flash write.
///
/// Core 0 cannot survive XIP going down: ComputerCard's DMA callback dispatches
/// through a vtable in flash, so it faults before any software guard could run.
/// So core 0 parks itself inside its own RAM-resident ProcessSample() (see
/// main.cpp) and says so via core0Parked; only then is writing safe.
///
/// calibstore.h does a smaller version of this for the saved calibration and
/// its header explains why it needs less: no USB in that path. Here there is,
/// so all of it applies.
static void EnterUploadMode()
{
	if (WebUI::uploadMode) return;

	// Raise the flag FIRST, then wait for the acknowledgement. ProcessSample
	// only runs when DMA_IRQ_0 fires, so masking before the flag is seen would
	// guarantee core 0 never parks. At 48kHz the next callback is 21us away.
	WebUI::uploadMode = true;
	WebUI::stage = 1;

	absolute_time_t deadline = make_timeout_time_ms(250);
	while (!WebUI::core0Parked && !time_reached(deadline)) tight_loop_contents();

	WebUI::stage = 2;

	// Silence every flash-resident interrupt handler. DMA_IRQ_0 and
	// PWM_IRQ_WRAP are the audio path (ComputerCard runs a SECOND ISR for CV
	// out); USBCTRL_IRQ is TinyUSB's, whose handler is in flash like the rest
	// of the stack. Nothing can talk to the host after this line, which is why
	// the whole upload is buffered in RAM and only committed here.
	irq_set_enabled(DMA_IRQ_0, false);
	irq_set_enabled(PWM_IRQ_WRAP, false);
	irq_set_enabled(USBCTRL_IRQ, false);

	// Prime the SDK's boot2 RAM copy while XIP is definitely still up.
	// flash_range_erase/program restore XIP afterwards by calling a RAM copy of
	// boot2, which flash_init_boot2_copyout() populates lazily by reading FROM
	// XIP the first time any flash function runs. A zero-length erase reaches
	// that initialiser and does nothing else, so the copy is taken here rather
	// than during the first real write.
	flash_range_erase(kUserRegionOff, 0);
	WebUI::stage = 3;
}

/// Keep servicing USB for `ms` so queued replies actually reach the host.
/// tud_midi_stream_write only fills a buffer; tud_task() moves it onto the
/// wire. Anything that replies and then reboots must pump in between.
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
	uint64_t p = static_cast<uint64_t>(bufLen_) * kQ16One / expected_;
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
		// in the low nibble says how many of the three data bytes are real.
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

	// Push anything queued by a reply above, OUTSIDE the packet loop where
	// re-entering tud_task() is safe. Without this an ack can sit in the FIFO
	// while the browser — which waits for one before sending the next chunk —
	// times out.
	if (txPending_)
	{
		txPending_ = false;
		tud_task();
	}

	// One library entry per pass, for the same reason: the whole burst does
	// not fit the TX FIFO, and it cannot be pumped from inside HandleSysex().
	// Here it is safe — the packet loop above has finished, so there is no
	// rx_ state to corrupt.
	//
	// The tud_task() after it is what actually puts the entry on the wire.
	// Send() only fills the FIFO and sets txPending_, which is checked ABOVE
	// this block — so without pumping here the last entry, and the terminator
	// the browser waits for, would sit in the buffer until some unrelated
	// message came in. That is exactly what made the library read time out and
	// come back empty.
	if (libSending_)
	{
		SendNextLibraryEntry();
		txPending_ = false;
		tud_task();
	}
}

/// Send ONE MSG_LIBDET, or the terminator once the library is exhausted.
///
/// Driven from Task() rather than from the MSG_LIBRARY handler — see the
/// comment there. Sending one per pass keeps the TX FIFO from overflowing
/// however many entries the card holds.
void __not_in_flash_func(WebUI::SendNextLibraryEntry)()
{
	const UserSampleHeader *h = UserHeader();
	const bool have = HaveUserSamples();

	while (libCursor_ < kMaxUserSamples)
	{
		const int e = libCursor_++;
		if (!have || h->size[e] == 0) continue;

		uint8_t d[5 + kNameLen];
		uint32_t o = 0;
		d[o++] = MSG_LIBDET;
		d[o++] = static_cast<uint8_t>(e);
		d[o++] = static_cast<uint8_t>((h->size[e] >> 14) & 0x7F);
		d[o++] = static_cast<uint8_t>((h->size[e] >> 7)  & 0x7F);
		d[o++] = static_cast<uint8_t>( h->size[e]        & 0x7F);
		// Name, ASCII, NUL-padded. Masked to 7 bits because SysEx cannot carry
		// anything wider — a name is only ever typed into a browser, so this
		// costs nothing real.
		for (int i = 0; i < kNameLen; i++)
			d[o++] = static_cast<uint8_t>(h->name[e][i] & 0x7F);
		Send(d, o);
		return;
	}

	// Exhausted: terminate the burst so the browser stops waiting.
	uint8_t end[2] = { MSG_LIBDET, 0x7F };
	Send(end, 2);
	libSending_ = false;
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

	// Do NOT call tud_task() here to push it out. Send() is reached from inside
	// Task()'s packet loop via HandleSysex(), so a nested tud_task() re-enters
	// that loop and corrupts rx_/rxLen_ half way through parsing the very
	// message being replied to. Task() flushes on the way out instead.
	txPending_ = true;
}

void __not_in_flash_func(WebUI::SendAck)(uint8_t code, uint32_t value)
{
	uint8_t p[5] = { MSG_UP_ACK, code,
	                 static_cast<uint8_t>((value >> 14) & 0x7F),
	                 static_cast<uint8_t>((value >> 7)  & 0x7F),
	                 static_cast<uint8_t>( value        & 0x7F) };
	Send(p, 5);
}

void __not_in_flash_func(WebUI::SendErr)(uint8_t code)
{
	uint8_t p[2] = { MSG_UP_ERR, code };
	Send(p, 2);
	uploading_ = false;
}

void __not_in_flash_func(WebUI::SeedHeaderFromFlash)()
{
	const UserSampleHeader *cur = UserHeader();
	if (cur->magic == kUserMagic && cur->version == kUserVersion)
	{
		memcpy(&hdr_, cur, sizeof(hdr_));
		baseOff_ = cur->totalBytes;
	}
	else
	{
		// No valid directory yet — start blank, but seed the slot map from
		// what the card is playing RIGHT NOW rather than zeroing it. Zero is a
		// real source value (baked entry 0, the kick), so a memset would
		// silently point all twelve voices at the kick.
		memset(&hdr_, 0, sizeof(hdr_));
		for (int v = 0; v < kNumVoices; v++) hdr_.slotSource[v] = gVoiceSample[v];
		baseOff_ = 0;
	}
}

/// Write the header sector, then reboot. There is no way to do this and keep
/// playing, and the attempt is what hung the card.
///
/// THE MISTAKE, RECORDED SO IT IS NOT REPEATED. This function used to leave
/// USBCTRL_IRQ enabled, on the reasoning that "the USB handler is not reached
/// during the erase, because tud_task() is not called until XIP is back".
/// That is false, and obviously so in hindsight: USBCTRL_IRQ is an INTERRUPT.
/// It fires whenever the host sends a packet, which is entirely outside this
/// code's control — and TinyUSB's handler lives in flash, so a packet
/// arriving mid-erase is a hard fault. On the bench that read as "saving the
/// kit hung the Workshop Computer", with the header half written.
///
/// docs/LESSONS.md §"If you let users upload samples" says to disable all
/// THREE interrupts and names USBCTRL_IRQ explicitly. It was right.
///
/// Once USB is masked TinyUSB's state is inconsistent — the host has seen a
/// device stop responding mid-transfer — so a reboot is the honest recovery
/// rather than a convenience. That is why every flash write on this card ends
/// in watchdog_reboot(), and why the browser is told to expect it.
void __not_in_flash_func(WebUI::CommitHeaderAndReboot)()
{
	EnterUploadMode();     // parks core 0, masks all three, primes boot2
	CommitHeader();
	watchdog_reboot(0, 0, 0);
	for (;;) tight_loop_contents();   // unreachable; the watchdog has it
}

void __not_in_flash_func(WebUI::FlushPage)()
{
	if (pageFill_ == 0) return;
	memset(page_ + pageFill_, 0, 256 - pageFill_);
	ProgramPage(pageAddr_, page_);
	pageAddr_ += 256;
	pageFill_ = 0;
}

/// Commit the RAM-staged upload to flash. Runs only after EnterUploadMode(),
/// with audio stopped and USB already dead — nothing here can report progress,
/// so WebUI::stage on the LEDs is the only visible signal.
void __not_in_flash_func(WebUI::WriteStagedBuffer)()
{
	// Where the new audio lands: appended after whatever is already stored and
	// rounded UP to a sector, so the erase cannot clip the tail of a recording
	// this upload never touched.
	uint32_t base = (baseOff_ + kFlashSector - 1u) & ~(kFlashSector - 1u);
	uint32_t dst  = kUserDataOff + base;

	uint32_t end = (dst + bufLen_ + kFlashSector - 1u) & ~(kFlashSector - 1u);
	if (end > dst) EraseRegion(dst, end - dst);
	WebUI::stage = 5;

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

	// Staging-buffer offsets become flash offsets now the base is known. Only
	// entries this session actually STAGED audio for get rebased — an entry
	// kept from a previous session (MSG_UP_KEEP) already holds a real flash
	// offset and must be left alone.
	for (int e = 0; e < kMaxUserSamples; e++)
		if (hdr_.size[e] > 0 && touched_[e])
			hdr_.offset[e] += base;

	// An upload saves the ASSIGNMENT too. The browser normally points a slot
	// at the entry it just uploaded, and a card that came back up holding the
	// audio but not the assignment would look like the upload half worked.
	for (int v = 0; v < kNumVoices; v++)
		hdr_.slotSource[v] = gVoiceSample[v];

	writeOff_ = base + bufLen_;
	CommitHeader();
}

void __not_in_flash_func(WebUI::CommitHeader)()
{
	FlushPage();
	hdr_.magic = kUserMagic;
	hdr_.version = kUserVersion;
	hdr_.totalBytes = writeOff_;

	// The header lives in its own sector and is written LAST: until the magic
	// lands, an interrupted upload falls back to baked samples rather than
	// pointing at half-written audio.
	static uint8_t sector[kUserHeaderLen];
	memset(sector, 0xFF, sizeof(sector));
	memcpy(sector, &hdr_, sizeof(hdr_));
	EraseRegion(kUserRegionOff, kUserHeaderLen);
	for (uint32_t i = 0; i < kUserHeaderLen; i += 256)
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
		// Version, capacity, and what is actually loaded, so the browser can
		// describe the card rather than guess.
		//
		// `used` is the APPEND WATERMARK, not the live byte count — uploads
		// append, so this is what decides whether another one fits. Deleting
		// an entry does not move it, which is why the Samples tab reports the
		// two separately (see `live` below): "3 samples totalling 40KB, but
		// 900KB of the region consumed" is a real state and the only honest
		// way to explain why a delete freed nothing.
		uint32_t used = 0;
		if (HaveUserSamples())
		{
			const UserSampleHeader *uh = UserHeader();
			used = uh->totalBytes;
			if (used > kUserDataLen) used = kUserDataLen;
		}
		const uint32_t live = UserBytesUsed();

		uint8_t info[18] = {
			MSG_INFO, kUserVersion,
			static_cast<uint8_t>(HaveUserSamples() ? 1 : 0),
			static_cast<uint8_t>(kHaveSamples ? 1 : 0),
			// The STAGING BUFFER size, not the flash region: one upload is
			// limited by the RAM it is buffered in. The browser must reject
			// oversized files against this, not against the 1MB region.
			static_cast<uint8_t>((kUploadMax >> 14) & 0x7F),
			static_cast<uint8_t>((kUploadMax >> 7)  & 0x7F),
			static_cast<uint8_t>( kUploadMax        & 0x7F),
			static_cast<uint8_t>(kNumVoices),
			// How much of the region is spoken for, and how big it is. Uploads
			// APPEND, so these are what tell the browser whether the card is
			// genuinely full or merely cannot take one huge file in one pass.
			static_cast<uint8_t>((used >> 14) & 0x7F),
			static_cast<uint8_t>((used >> 7)  & 0x7F),
			static_cast<uint8_t>( used        & 0x7F),
			static_cast<uint8_t>((kUserDataLen >> 14) & 0x7F),
			static_cast<uint8_t>((kUserDataLen >> 7)  & 0x7F),
			static_cast<uint8_t>( kUserDataLen        & 0x7F),
			// How many entries the library can hold, and how many live bytes
			// they actually occupy. `live` differs from `used` once anything
			// has been deleted — see the comment above.
			static_cast<uint8_t>(kMaxUserSamples),
			static_cast<uint8_t>((live >> 14) & 0x7F),
			static_cast<uint8_t>((live >> 7)  & 0x7F),
			static_cast<uint8_t>( live        & 0x7F) };
		Send(info, 18);
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
		if (expected_ > sizeof(buf_)) { SendErr(ERR_TOO_BIG); break; }

		// The card keeps playing through the whole transfer. Flash is not
		// touched until MSG_UP_END, because writing it means killing USB —
		// TinyUSB's stack and its ISR are all in flash, so the card cannot
		// receive the next chunk and write the last one at the same time.
		uploading_ = true;
		pageFill_ = 0;
		bufLen_ = 0;

		// Start from what is already on the card rather than a blank table, so
		// uploading one sample leaves the rest of the library alone.
		SeedHeaderFromFlash();
		memset(touched_, 0, sizeof(touched_));

		if (baseOff_ + expected_ > kUserDataLen) { SendErr(ERR_TOO_BIG); break; }

		WebUI::stage = 4;
		SendAck(0, 0);
		break;
	}

	case MSG_UP_SLOT:
	{
		// payload: LIBRARY ENTRY index — not a voice. An upload adds a sound
		// to the library; which pads play it is a separate question, answered
		// by MSG_SET_SOURCE.
		if (!uploading_ || n < 2) { SendErr(ERR_PROTOCOL); break; }
		slotEntry_ = p[1];
		if (slotEntry_ >= kMaxUserSamples) { SendErr(ERR_BAD_SLOT); break; }

		// Each entry starts page-aligned in the staging buffer so it can still
		// be programmed independently when the whole lot is committed.
		bufLen_ = (bufLen_ + 255u) & ~255u;
		slotStart_ = bufLen_;
		slotLen_ = 0;
		SendAck(1, bufLen_);
		break;
	}

	case MSG_UP_CHUNK:
	{
		if (!uploading_) { SendErr(ERR_PROTOCOL); break; }
		uint8_t dec[512];
		uint32_t got = Decode7bit(p + 1, n - 1, dec, sizeof(dec));
		if (bufLen_ + got > sizeof(buf_)) { SendErr(ERR_TOO_BIG); break; }

		// Straight into RAM. Flash stays untouched until UP_END.
		memcpy(buf_ + bufLen_, dec, got);
		bufLen_ += got;
		slotLen_ += got;
		WebUI::stage = 5;
		SendAck(2, bufLen_);
		break;
	}

	case MSG_UP_SLOTEND:
		if (!uploading_) { SendErr(ERR_PROTOCOL); break; }
		// Where this entry landed in the STAGING buffer; rewritten to a flash
		// offset when the buffer is committed.
		hdr_.offset[slotEntry_] = slotStart_;
		hdr_.size[slotEntry_]   = slotLen_;
		touched_[slotEntry_]    = true;
		SendAck(3, slotLen_);
		break;

	// NOTE: v1 had MSG_UP_ALIAS here, to point a second pad at audio already
	// sent. The library model makes it unnecessary — two voices name the same
	// entry and that IS the sharing, at no cost and with nothing to keep in
	// step. MSG_UP_DROP is likewise gone, replaced by MSG_DELETE, which is
	// about the library rather than about one pad.

	case MSG_UP_KEEP:
	{
		// Keep a library entry that is ALREADY in flash: payload entry index.
		//
		// SeedHeaderFromFlash() already copies every entry forward, so this is
		// only needed by a browser that wants to be explicit about what it is
		// preserving. It re-asserts the existing (offset,size) and marks the
		// entry untouched so the commit rebase leaves the flash offset alone.
		if (!uploading_ || n < 2) { SendErr(ERR_PROTOCOL); break; }
		uint8_t e = p[1];
		if (e >= kMaxUserSamples) { SendErr(ERR_BAD_SLOT); break; }

		const UserSampleHeader *cur = UserHeader();
		if (cur->magic != kUserMagic || cur->version != kUserVersion ||
		    cur->size[e] == 0) { SendErr(ERR_PROTOCOL); break; }

		hdr_.offset[e] = cur->offset[e];   // already a flash offset
		hdr_.size[e]   = cur->size[e];
		touched_[e]    = false;            // so the rebase skips it
		SendAck(11, hdr_.size[e]);
		break;
	}

	case MSG_UP_END:
	{
		if (!uploading_) { SendErr(ERR_PROTOCOL); break; }
		uploading_ = false;

		// Ack and push it out BEFORE touching flash. Once the write starts USB
		// is gone, so this is the last chance to say anything. It is an "about
		// to commit" ack, not a "committed" one — the browser treats the
		// disconnect that follows as success.
		SendAck(4, bufLen_);
		FlushUsb(150);

		// Everything below runs with the card silent and USB dead.
		EnterUploadMode();
		WriteStagedBuffer();
		WebUI::stage = 7;

		watchdog_reboot(0, 0, 0);
		break;
	}

	case MSG_SET_SOURCE:
	{
		// payload: voice, src — a baked index, kUserSourceBase+n for a user
		// library entry, or kWireSynth to synthesise the slot.
		//
		// RAM only, deliberately: no flash write means no EnterUploadMode and
		// no reboot, so the browser can flip a slot and hear it instantly.
		// MSG_SAVE_MAP is what makes it survive a power cycle.
		if (n < 3) { SendErr(ERR_PROTOCOL); break; }
		const int voice = p[1];
		const int src = (p[2] == kWireSynth) ? kSourceSynth : p[2];
		if (!SetVoiceSample(voice, src)) { SendErr(ERR_BAD_SLOT); break; }
		SendAck(12, static_cast<uint32_t>(p[2]));
		break;
	}

	case MSG_SAVE_MAP:
	{
		// Commit the slot map. Acks BEFORE the write, because the write masks
		// USB and nothing can be sent afterwards — the browser treats the
		// disconnect that follows as success.
		SeedHeaderFromFlash();
		for (int v = 0; v < kNumVoices; v++)
			hdr_.slotSource[v] = gVoiceSample[v];
		writeOff_ = hdr_.totalBytes;

		SendAck(13, 0);
		FlushUsb(150);
		CommitHeaderAndReboot();
		break;
	}

	case MSG_SLOTS:
	{
		// What every voice is currently playing: one byte each, in the same
		// encoding MSG_SET_SOURCE accepts. The browser mirrors this straight
		// into its dropdowns, so what the page shows is what the card will
		// actually play rather than a guess from the build-time defaults.
		uint8_t sl[1 + kNumVoices];
		uint32_t o = 0;
		sl[o++] = MSG_SLOTS;
		for (int v = 0; v < kNumVoices; v++)
			sl[o++] = (gVoiceSample[v] == kSourceSynth)
			        ? kWireSynth
			        : static_cast<uint8_t>(gVoiceSample[v] & 0x7F);
		Send(sl, o);
		break;
	}

	case MSG_LIBRARY:
	{
		// One MSG_LIBDET per FILLED entry, then a terminator with entry 0x7F.
		//
		// Sent as a burst rather than one-request-per-entry: 32 possible
		// entries would otherwise be 32 round trips, and the browser's ack
		// queue already copes with a burst (that is what it is for).
		//
		// DEFERRED, not sent from here, and that is the whole point.
		//
		// Two constraints pull in opposite directions. The TX FIFO is 256
		// bytes at full speed and a full library is 32 x 24 = 768, so the
		// replies cannot all be queued at once — tud_midi_stream_write
		// silently drops what will not fit. But tud_task() CANNOT be called
		// from here either: HandleSysex() runs inside Task()'s packet loop, so
		// a nested tud_task() re-enters that loop and corrupts rx_ half way
		// through parsing this very request. Send()'s comment says exactly
		// that, and an earlier version of this code called tud_task() anyway —
		// which is why an upload could land its audio and then report an empty
		// library.
		//
		// So the burst is handed to Task() instead: it remembers where it got
		// to and sends the next entry each time round, with a real tud_task()
		// in between. See libCursor_.
		libCursor_ = 0;
		libSending_ = true;
		break;
	}

	case MSG_NAME:
	{
		// Name a library entry: payload entry, then ASCII. Staged in RAM and
		// committed by the next MSG_SAVE_MAP or upload, so renaming several
		// entries costs one flash write rather than one each.
		if (n < 2) { SendErr(ERR_PROTOCOL); break; }
		const uint8_t e = p[1];
		if (e >= kMaxUserSamples) { SendErr(ERR_BAD_SLOT); break; }

		// DURING an upload, hdr_ is the session being built — do NOT reseed it
		// (that would discard the entries staged so far) and do NOT commit
		// (that would reboot the card half way through the transfer). The name
		// rides along and lands with everything else at MSG_UP_END.
		const bool inSession = uploading_;
		if (!inSession) SeedHeaderFromFlash();

		uint32_t i = 0;
		for (; i + 2 < n && i < kNameLen - 1u; i++)
			hdr_.name[e][i] = static_cast<char>(p[2 + i]);
		hdr_.name[e][i] = '\0';

		if (inSession) { SendAck(14, e); break; }

		// Standalone rename: commit now, because the user expects a rename to
		// stick and there is no later commit coming. Acks first — the write
		// takes USB down with it.
		writeOff_ = hdr_.totalBytes;
		SendAck(14, e);
		FlushUsb(150);
		CommitHeaderAndReboot();
		break;
	}

	case MSG_DELETE:
	{
		// Drop ONE library entry. The audio stays on the flash but becomes
		// unreferenced, so this reclaims the SLOT rather than the space —
		// uploads append, and only MSG_ERASE actually frees bytes.
		//
		// Any voice pointing at the deleted entry falls back to synth, here
		// rather than at play time: a slot naming a missing entry would go
		// silent, and a silent pad reads as a broken card.
		if (n < 2) { SendErr(ERR_PROTOCOL); break; }
		const uint8_t e = p[1];
		if (e >= kMaxUserSamples) { SendErr(ERR_BAD_SLOT); break; }
		if (!HaveUserSamples()) { SendAck(6, 0); break; }

		SeedHeaderFromFlash();
		hdr_.offset[e] = 0;
		hdr_.size[e]   = 0;
		memset(hdr_.name[e], 0, kNameLen);

		const int8_t gone = static_cast<int8_t>(kUserSourceBase + e);
		for (int v = 0; v < kNumVoices; v++)
		{
			if (gVoiceSample[v] == gone) gVoiceSample[v] = kSourceSynth;
			hdr_.slotSource[v] = gVoiceSample[v];
		}
		writeOff_ = hdr_.totalBytes;

		SendAck(6, 0);
		FlushUsb(150);
		CommitHeaderAndReboot();
		break;
	}

	case MSG_ERASE:
		// Wipe the header sector: every entry and the slot map go with it, so
		// the card falls straight back to its baked defaults. This is also the
		// only way to reclaim SPACE — uploads append, so re-uploading the same
		// sample repeatedly eventually fills the region.
		SendAck(5, 0);
		FlushUsb(200);
		EnterUploadMode();
		EraseRegion(kUserRegionOff, kUserHeaderLen);
		watchdog_reboot(0, 0, 0);
		break;

	case MSG_PLAY:
		// Leave USB mode and come back up playing. The reboot is what tears
		// TinyUSB down cleanly; calibration survives it, being in flash since
		// calibstore.h landed.
		SendAck(7, 0);
		FlushUsb(200);
		watchdog_reboot(0, 0, 0);
		break;

	default:
		break;
	}
}

} // namespace nko
