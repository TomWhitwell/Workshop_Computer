// webui.h — USB-MIDI/SysEx sample upload for NIBBLE-KO.
//
// The card enumerates as a USB-MIDI device; a browser talks to it over
// WebMIDI SysEx to assign each of the 12 voice slots a synth voice or an
// uploaded sample. Drop a WAV in the browser, it arrives as 8-bit mono 48kHz
// and is written into the user flash region — no toolchain, no reflash.
//
// SysEx payloads must be 7-bit (every byte 0-127), so audio is 7-bit encoded:
// 7 source bytes become 8 septets, the 8th carrying the high bits. Same
// scheme WorkshopBio and WorkshopZX use.
//
// Adapted from WorkshopBio/webui.h. The message set is close to identical —
// upload/erase/slot-query are the same shape — with the mode+variant slot
// addressing collapsed to a single voice index (0..kNumVoices-1), and a new
// MSG_SET_SOURCE for choosing synth vs. sample per slot.
//
// Both things this header once waited on are settled:
//   1. PCM playback exists and is hardware-tested — TriggerVoice resolves
//      each hit through ResolveSample(), so an upload has somewhere to land.
//   2. MSG_SET_SOURCE's payload is [voice, bank], one byte each, with
//      kSourceSynth (0x7F) meaning "synthesise this slot". It writes
//      gVoiceSample[] in samplestore.h.
//
// IMPORTANT: writing flash while ComputerCard runs
// WILL HANG THE CARD unless all five steps in docs/LESSONS.md §"If you let
// users upload samples" are followed — raise-and-wait for core 0 to park,
// disable DMA_IRQ_0 + PWM_IRQ_WRAP + USBCTRL_IRQ, prime the boot2 RAM copy,
// wrap writes in __not_in_flash_func, and buffer the whole upload in RAM
// first. WorkshopBio/webui.cpp is the worked reference for all five.

#pragma once
#include <stdint.h>
#include "nibbleko.h"
#include "samplestore.h"

namespace nko {

// SysEx message IDs (first payload byte). fw = firmware, ui = browser.
enum : uint8_t {
	MSG_HELLO       = 0x01,  // ui->fw: announce presence; fw replies MSG_INFO
	MSG_INFO        = 0x02,  // fw->ui: version, capacity, what is loaded
	MSG_UP_BEGIN    = 0x10,  // ui->fw: start upload session (appends; keeps playing)
	MSG_UP_SLOT     = 0x11,  // ui->fw: begin a LIBRARY ENTRY; payload entry
	MSG_UP_CHUNK    = 0x12,  // ui->fw: 7-bit-encoded audio for the current entry
	MSG_UP_SLOTEND  = 0x13,  // ui->fw: this entry is complete
	MSG_UP_END      = 0x14,  // ui->fw: whole upload done -> commit, reboot
	MSG_UP_ACK      = 0x15,  // fw->ui: ready for more / progress
	MSG_UP_ERR      = 0x16,  // fw->ui: something went wrong; payload = code
	MSG_UP_KEEP     = 0x1A,  // ui->fw: keep a library entry already in flash
	MSG_ERASE       = 0x20,  // ui->fw: forget ALL user samples, revert to baked
	MSG_PLAY        = 0x21,  // ui->fw: leave USB mode and reboot into playing
	MSG_SLOTS       = 0x23,  // fw->ui: what every voice is currently playing
	MSG_LIBRARY     = 0x24,  // ui->fw: request the library; fw replies MSG_LIBDET
	MSG_LIBDET      = 0x25,  // fw->ui: one entry's size and name
	MSG_SET_SOURCE  = 0x26,  // ui->fw: point a voice at any sound.
	                          // Payload [voice, src], where src is a baked
	                          // index, kUserSourceBase+n for a user entry, or
	                          // kWireSynth for "synthesise it". Takes effect
	                          // immediately; MSG_SAVE_MAP makes it permanent.
	MSG_SAVE_MAP    = 0x27,  // ui->fw: commit the slot map to flash, no reboot
	MSG_NAME        = 0x28,  // ui->fw: name a library entry; payload entry+ASCII
	MSG_DELETE      = 0x29,  // ui->fw: drop ONE library entry, commit, reboot
	MSG_PROF_GET    = 0x30,  // ui->fw: send the timing peaks (profile builds)
	MSG_PROF        = 0x31,  // fw->ui: peak cycles per bucket + overrun count
	MSG_PAT_GET     = 0x40,  // ui->fw: dump a pattern slot; payload [slot].
	                          // Replies with a burst of MSG_PAT_DATA.
	MSG_PAT_DATA    = 0x41,  // fw->ui: one chunk of a pattern slot; see
	                          // SendNextPatternChunk() for the framing.
	MSG_PAT_SET     = 0x42,  // ui->fw: write a pattern slot. RAM only, like
	                          // MSG_SET_SOURCE — no flash, no reboot.
};

// Manufacturer ID 0x7D = "prototyping / private use". Same as the sibling cards.
constexpr uint8_t kManufacturerId = 0x7D;

/// Largest single upload, set by the RAM staging buffer rather than by the
/// user flash region. Writing flash takes USB down with it, so a transfer
/// has to be received in FULL before any of it is committed.
///
/// 160KB, the same figure WorkshopBio ships on identical hardware (RP2040,
/// 256KB RAM) — so this is a proven budget rather than an estimate. It only
/// fits because USB is MODAL: nothing instantiates WebUI until switch+B+D
/// enters Mode::WebUi, and by then the card has stopped playing. Watch
/// --print-memory-usage after any change here; the card's own state is only
/// ~38KB, so this buffer is by far the largest thing in RAM.
constexpr uint32_t kUploadMax = 160u * 1024u;

// Error codes carried by MSG_UP_ERR.
enum : uint8_t {
	ERR_NONE = 0, ERR_TOO_BIG = 1, ERR_BAD_SLOT = 2, ERR_PROTOCOL = 3,
};

/// What this firmware can do, advertised in MSG_INFO's last byte.
///
/// This exists because an unknown SysEx message is answered with SILENCE
/// (HandleSysex's `default: break;`), which the browser cannot tell apart
/// from a card that failed to reply — so a page talking to firmware older
/// than itself reports "the card did not respond" and looks like a bug in
/// working code. It cost a bench session before this was added.
///
/// Set a bit when a new message group lands, and have the page check it
/// before offering the control. Seven bits are available before this needs
/// a second byte; add that at the END of MSG_INFO, never in the middle.
enum : uint8_t {
	kFeaturePatterns = 1u << 0,   ///< MSG_PAT_GET / MSG_PAT_DATA / MSG_PAT_SET
};

constexpr uint8_t kFeatureBits = kFeaturePatterns;

/// MSG_SET_SOURCE's "this slot is synthesised" sentinel, ON THE WIRE.
///
/// 0x7F, NOT 0xFF, and the distinction is not cosmetic: every byte inside a
/// SysEx message must be 7-bit (0x00-0x7F), so 0xFF cannot travel at all — it
/// would be read as a status byte and truncate the message.
///
/// The firmware translates this to samplestore.h's kSourceSynth (-1), which
/// is what actually gets stored. Same concept, two encodings, two names on
/// purpose — see that constant's comment.
constexpr uint8_t kWireSynth = 0x7F;

/// 7-bit encode/decode. Returns bytes written.
/// Encoded length = ceil(srcLen/7)*8; decoded length <= (srcLen/8)*7.
uint32_t Encode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax);
uint32_t Decode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax);

/// Pattern-slot access for the WebUI, defined in main.cpp.
///
/// Free functions rather than a Looper handle, for the same reason
/// gVoiceSample/SetVoiceSample are: the Looper is a private member of the card
/// object, webui.cpp runs on core 1, and this is the whole of the surface it
/// needs.
///
/// CROSS-CORE, and the safety argument is NOT "core 0 has stopped" — it no
/// longer does. The loop keeps playing in WebUi mode so that re-assigning a
/// sound can be judged by ear (see main.cpp's SetMode). What makes this safe
/// is narrower: the pattern SLOT arrays are written only by StorePattern(),
/// which is reachable only from a pad gesture, and the pads are inert in
/// WebUi mode (FireCombo ignores every combo there). So core 0 plays FROM
/// events_ while these touch pattern_[], and the two never overlap.
///
/// If a future change lets anything else write a slot while connected — a
/// timer, a CV trigger, a new gesture — this becomes a genuine torn read and
/// needs real synchronisation, not a comment.
///
/// `out` must have room for Looper::kMaxEvents events. Returns false for a bad
/// slot; an empty slot is a success with *count == 0.
bool WebGetPattern(int slot, void *out, uint16_t *count, uint16_t *knobCount);
bool WebSetPattern(int slot, const void *in, uint16_t count, uint16_t knobCount);

/// How many events a slot can hold, and the wire size of one — so webui.cpp can
/// size its buffers without including looper.h (which drags in drums.h and the
/// whole audio engine for two numbers).
constexpr uint32_t kPatMaxEvents  = 512;
constexpr uint32_t kPatEventBytes = 4;
constexpr uint8_t  kPatSlots      = 3;

/// USB-MIDI transport + upload state machine.
///
/// Ported from WorkshopBio::WebUI, whose mode*variant slot addressing becomes
/// a single flat loop over kNumVoices here.
class WebUI
{
public:
	void Init();
	/// Poll USB. Cheap when idle; call from the audio callback.
	void Task();

	/// True while an upload is in progress — the card mutes and stops running
	/// the kit, because flash writes stall execution anyway.
	bool Uploading() const { return uploading_; }

	/// Set by core 0 when the momentary switch has been held long enough to
	/// hand the card over to USB. Core 1 (if this card ends up needing one —
	/// see nibbleko.h and NIBBLE's "why there is no core 1" reasoning, which
	/// stops applying the moment USB is added) watches it, initialises
	/// TinyUSB the first time it goes true, and then services USB forever.
	static volatile bool usbMode;

	/// True once an upload has begun. One-way latch: the card is an upload
	/// appliance until it restarts.
	static volatile bool uploadMode;

	/// Set by core 0 once it has stopped inside its RAM-resident handler and
	/// is spinning. The flash-writing side must wait for this before touching
	/// flash — see the file header's five-step protocol.
	static volatile bool core0Parked;

	// NOTE: there was briefly a `pauseAudio` here, for flash writes that were
	// meant to resume rather than reboot. It could not work — see
	// CommitHeaderAndReboot(). Every flash write masks USB, so every flash
	// write ends in a reboot.

	/// How far the upload got, shown on the LEDs as a binary count while
	/// uploadMode is set. See WorkshopBio/webui.h for the stage numbering this
	/// is expected to mirror.
	static volatile uint8_t stage;

	/// 0..kQ16One, for the LED progress display.
	int32_t Progress() const;

private:
	void HandleSysex(const uint8_t *msg, uint32_t len);
	void Send(const uint8_t *payload, uint32_t len);
	void SendAck(uint8_t code, uint32_t value);
	void SendErr(uint8_t code);
	void FlushPage();          // commit the 256-byte staging page to flash
	void CommitHeader();
	void WriteStagedBuffer();  // erase+program the RAM buffer, then the header

	/// Copy the on-flash directory into hdr_ so an edit starts from what the
	/// card actually holds. Every handler that rewrites the header calls this
	/// first — without it, changing one entry would blank all the others.
	void SeedHeaderFromFlash();

	/// Write the header sector and reboot. Used by save/rename/delete, which
	/// touch no audio but still cannot keep USB up across a flash erase — see
	/// the definition for why the "live" version of this hung the card.
	void CommitHeaderAndReboot();

	/// Send one MSG_LIBDET per call, driven from Task(). The library reply is
	/// a burst too big for the TX FIFO, and it cannot be pumped from inside
	/// HandleSysex() without corrupting the parser — see MSG_LIBRARY.
	void SendNextLibraryEntry();

	/// Send one MSG_PAT_DATA chunk per call, driven from Task() for exactly
	/// the same reason as SendNextLibraryEntry(): a full slot is 2KB against a
	/// 256-byte TX FIFO, and tud_task() must not be called from inside
	/// HandleSysex(). See MSG_PAT_GET.
	void SendNextPatternChunk();

	bool     uploading_ = false;
	uint8_t  slotEntry_ = 0;    // library entry currently being received
	uint32_t writeOff_ = 0;     // next byte offset within the data area
	uint32_t expected_ = 0;     // total bytes announced for this session

	// Flash writes must be whole 256-byte pages, so bytes are staged here.
	uint8_t  page_[256];
	uint32_t pageFill_ = 0;
	uint32_t pageAddr_ = 0;

	// The whole upload is staged in RAM before ANY flash is touched — see the
	// file header.
	uint8_t  buf_[kUploadMax];
	uint32_t bufLen_ = 0;
	uint32_t baseOff_ = 0;      // append point from the existing header
	uint32_t slotStart_ = 0;    // where the current entry began in buf_
	uint32_t slotLen_ = 0;
	// Which entries this session STAGED audio for, so the commit knows whose
	// offsets are buffer-relative and need rebasing into flash offsets.
	bool     touched_[kMaxUserSamples] = {};

	// The header being built up as slots arrive; written last so a
	// half-finished upload never leaves a valid-looking directory behind.
	UserSampleHeader hdr_;

	uint8_t  rx_[1024];
	uint32_t rxLen_ = 0;
	bool     inSysex_ = false;
	bool     txPending_ = false;

	// The library reply, sent one entry per Task() pass.
	uint8_t  libCursor_ = 0;
	bool     libSending_ = false;

	// The pattern dump, sent one chunk per Task() pass. patBuf_ holds the slot
	// copied out at MSG_PAT_GET time, so the reply is a snapshot rather than a
	// live read spread over many passes.
	uint8_t  patBuf_[kPatMaxEvents * kPatEventBytes];
	uint32_t patBytes_ = 0;     // how much of patBuf_ is real
	uint32_t patCursor_ = 0;    // next byte to send
	uint16_t patKnobCount_ = 0;
	uint8_t  patSlot_ = 0;
	bool     patSending_ = false;
};

} // namespace nko
