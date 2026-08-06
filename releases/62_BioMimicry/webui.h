// webui.h — USB-MIDI/SysEx sample upload for BioMimicry.
//
// The card enumerates as a USB-MIDI device; interface.html talks to it over
// WebMIDI SysEx. Drop WAVs in the browser, they arrive as 8-bit mono 48kHz and
// are written into the user flash region — no toolchain, no reflash.
//
// SysEx payloads must be 7-bit (every byte 0-127), so audio is 7-bit encoded:
// 7 source bytes become 8 septets, the 8th carrying the high bits. Same scheme
// WorkshopZX uses for snapshots.
//
// IMPORTANT: writing flash on the RP2040 stalls XIP, so any code still running
// from flash halts mid-erase. Rather than make the whole audio path
// RAM-resident, the card enters an UPLOAD MODE for the duration: the ecosystem
// stops, audio is muted and the LEDs show progress. Uploading is a setup
// activity, not a performance one.

#pragma once
#include <stdint.h>
#include "biomimicry.h"
#include "samplestore.h"

namespace bio {

// SysEx message IDs (first payload byte). fw = firmware, ui = browser.
enum : uint8_t {
	MSG_HELLO      = 0x01,  // ui->fw: announce presence; fw replies MSG_INFO
	MSG_INFO       = 0x02,  // fw->ui: version, capacity, what is loaded
	MSG_UP_BEGIN   = 0x10,  // ui->fw: start upload session (stops audio; appends)
	MSG_UP_SLOT    = 0x11,  // ui->fw: begin a mode+variant; payload mode,variant,len
	MSG_UP_CHUNK   = 0x12,  // ui->fw: 7-bit-encoded audio for the current slot
	MSG_UP_SLOTEND = 0x13,  // ui->fw: this slot is complete
	MSG_UP_ALIAS   = 0x17,  // ui->fw: point a slot at audio already sent
	MSG_UP_DROP    = 0x18,  // ui->fw: empty a mode, staged (no reboot)
	MSG_UP_BAKED   = 0x19,  // ui->fw: point a slot at a BUILT-IN recording
	MSG_UP_KEEP    = 0x1A,  // ui->fw: keep audio already in the user region
	MSG_UP_END     = 0x14,  // ui->fw: whole upload done -> commit header, resume
	MSG_UP_ACK     = 0x15,  // fw->ui: ready for more / progress
	MSG_UP_ERR     = 0x16,  // fw->ui: something went wrong; payload = code
	MSG_ERASE      = 0x20,  // ui->fw: forget user samples, revert to baked
	MSG_PLAY       = 0x21,  // ui->fw: leave USB mode and reboot into playing
	MSG_CLEARMODE  = 0x22,  // ui->fw: revert ONE mode to its baked recordings
	MSG_SLOTS      = 0x23,  // fw->ui: which slots currently hold user audio
	MSG_SLOTINFO   = 0x24,  // ui->fw: detail for one mode; fw replies MSG_SLOTDET
	MSG_SLOTDET    = 0x25,  // fw->ui: per-slot offset+size for one mode
	MSG_PROF_GET   = 0x30,  // ui->fw: send the timing peaks (profile builds)
	MSG_PROF       = 0x31,  // fw->ui: peak cycles per bucket + overrun count
};

// Manufacturer ID 0x7D = "prototyping / private use".
constexpr uint8_t kManufacturerId = 0x7D;

/// Largest single upload, set by the RAM staging buffer rather than by the 1MB
/// flash region. Writing flash takes USB down with it — TinyUSB's stack and its
/// interrupt handler both live there — so a transfer has to be received in FULL
/// before any of it is committed.
///
/// 160KB of the RP2040's 264KB. This buffer alone is ~74% of the card's total
/// RAM use; everything else fits in 54KB. The practical ceiling is around 192KB,
/// which would leave under 11KB spare, so this keeps ~43KB of margin for stack
/// and any future growth.
///
/// It caps one PASS, not the card: uploads append (see baseOff_), so the full
/// 1020KB region is reachable in successive passes. Removing the cap properly
/// means committing per SLOT at MSG_UP_SLOTEND and letting USB drop and return
/// between slots — a protocol change, not a constant.
constexpr uint32_t kUploadMax = 160u * 1024u;

// Error codes carried by MSG_UP_ERR.
enum : uint8_t {
	ERR_NONE = 0, ERR_TOO_BIG = 1, ERR_BAD_SLOT = 2, ERR_PROTOCOL = 3,
};

/// 7-bit encode/decode. Returns bytes written.
/// Encoded length = ceil(srcLen/7)*8; decoded length <= (srcLen/8)*7.
uint32_t Encode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax);
uint32_t Decode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax);

/// USB-MIDI transport + upload state machine.
class WebUI
{
public:
	void Init();
	/// Poll USB. Cheap when idle; call from the audio callback.
	void Task();

	/// True while an upload is in progress — the card mutes and stops running
	/// the ecosystem, because flash writes stall execution anyway.
	bool Uploading() const { return uploading_; }

	/// Set by core 0 when the momentary switch has been held long enough to hand
	/// the card over to USB. Core 1 watches it, initialises TinyUSB the first
	/// time it goes true, and then services USB forever.
	///
	/// Leaving reboots the card — hold the switch again, or press "Back to
	/// playing" in the web UI. USB is not running at all until this is set: no
	/// enumeration, no flash-resident USBCTRL_IRQ, and no USB stack competing
	/// with the audio path while the card is being played.
	static volatile bool usbMode;

	/// True once an upload has begun. The audio interrupt is disabled for the
	/// whole upload and the card reboots when it ends, so this is a one-way
	/// latch: the card is an upload appliance until it restarts.
	///
	/// This replaces a `flashBusy` flag that core 0 spun on from inside its DMA
	/// interrupt handler. That deadlocked every time — see EnterUploadMode().
	static volatile bool uploadMode;

	/// Set by core 0 once it has stopped inside its RAM-resident handler and is
	/// spinning. Core 1 waits for this before touching flash — without it, core 0
	/// may still be executing ComputerCard's flash-resident callback when XIP
	/// drops, which hard-faults the chip.
	static volatile bool core0Parked;

	/// How far the upload got, shown on the LEDs as a binary count while
	/// uploadMode is set. USB cannot report a hang that stops USB, so the card
	/// has to signal out of band: 1 entered upload mode, 2 core 0 parked,
	/// 3 boot2 primed, 4 header read, 5 erase done, 6 first chunk written,
	/// 7 header committed. Whatever number is frozen on the LEDs is the last
	/// step that completed.
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

	bool     uploading_ = false;
	uint8_t  slotMode_ = 0, slotVariant_ = 0;
	uint32_t writeOff_ = 0;     // next byte offset within the data area
	uint32_t expected_ = 0;     // total bytes announced for this session

	// Flash writes must be whole 256-byte pages, so bytes are staged here.
	uint8_t  page_[256];
	uint32_t pageFill_ = 0;
	uint32_t pageAddr_ = 0;

	// The whole upload is staged in RAM before ANY flash is touched, because
	// writing flash kills USB: TinyUSB's device stack and its USBCTRL_IRQ
	// handler both live in flash, so the card cannot receive the next chunk
	// while committing the last one. 128KB holds the largest baked recording
	// (meteors_5, 116KB) with room to spare and leaves ~70KB of the RP2040's
	// 264KB for everything else. This is the real cap on one upload now --
	// several small samples still fit in a single pass, a full library does not.
	uint8_t  buf_[kUploadMax];
	uint32_t bufLen_ = 0;
	uint32_t baseOff_ = 0;      // append point from the existing header
	uint32_t slotStart_ = 0;    // where the current slot began in buf_
	uint32_t slotLen_ = 0;
	bool     touched_[kNumModes][kNumVariants] = {};
	// Which modes this session has already wiped. The first slot sent for a mode
	// clears the rest of it, so uploading two geese leaves two rather than two
	// plus six survivors from an earlier upload.
	bool     modeCleared_[kNumModes] = {};

	// The header being built up as slots arrive; written last so a half-finished
	// upload never leaves a valid-looking directory behind.
	UserSampleHeader hdr_;

	uint8_t  rx_[1024];
	uint32_t rxLen_ = 0;
	bool     inSysex_ = false;
	// Set by Send(); Task() pushes the FIFO once it is out of its packet loop,
	// where re-entering tud_task() cannot corrupt a half-parsed message.
	bool     txPending_ = false;
};

} // namespace bio
