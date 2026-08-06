// webui.h — USB-MIDI/SysEx Web UI transport + protocol for ZX.
//
// The card enumerates as a USB-MIDI device; a browser (WebMIDI) talks to it over
// SysEx. This lets you upload .z80 snapshots and edit the input mapping live,
// with no app install and no reflashing.
//
// SysEx payloads are 7-bit (each byte 0-127). Binary data (a snapshot, the raw
// mapping table) is 7-bit encoded: groups of 7 source bytes become 8 septets,
// where the 8th carries the high bits. Decoding reverses this.
//
// Runs on core 1 (alongside the emulator). USB is polled between emulation
// batches; when the machine is PAUSED (switch Up) core 1 gives USB its full
// attention, which is the intended time to upload/remap.

#pragma once
#include <cstdint>
#include "spectrum.h"
#include "mapping.h"

namespace zx {

// SysEx message IDs (first payload byte). fw = firmware, ui = browser.
enum : uint8_t {
	MSG_HELLO         = 0x01, // ui->fw: announce; fw replies MSG_INFO
	MSG_INFO          = 0x02, // fw->ui: firmware version + capabilities
	MSG_SNAP_BEGIN    = 0x10, // ui->fw: start upload; carries total length (enc)
	MSG_SNAP_CHUNK    = 0x11, // ui->fw: a chunk of 7-bit-encoded snapshot bytes
	MSG_SNAP_END      = 0x12, // ui->fw: upload complete -> load+run
	MSG_SNAP_ACK      = 0x13, // fw->ui: chunk received / status
	MSG_MAP_GET       = 0x20, // ui->fw: please send the current mapping
	MSG_MAP_REPORT    = 0x21, // fw->ui: the current mapping table
	MSG_MAP_SET       = 0x22, // ui->fw: set the mapping table
	MSG_KEY           = 0x40, // ui->fw: keyboard passthrough. payload: keyIndex,down
	MSG_KEY_RESET     = 0x41, // ui->fw: release all passthrough keys
	MSG_SET_PROBE     = 0x60, // ui->fw: CV2 memory-probe address (3 septets = 16-bit)
	// Decoded-snapshot load: the BROWSER parses/decompresses the .z80/.sna and
	// sends already-decoded state, so the firmware just stores it (no staging /
	// decompression / RAM pressure on the RP2040).
	MSG_LOAD_BEGIN    = 0x50, // ui->fw: start; payload = decoded register/state block
	MSG_LOAD_PAGE     = 0x51, // ui->fw: page# + offset + 7bit-encoded raw bytes -> bank
	MSG_LOAD_END      = 0x52, // ui->fw: done -> apply on resume
	MSG_STATUS        = 0x30, // fw->ui: running/paused, current activity
};

// Manufacturer ID 0x7D = "prototyping / private use" (same as the repo example).
constexpr uint8_t ZX_MANUFACTURER_ID = 0x7D;

// 7-bit encode/decode. Return bytes written. dst must be large enough:
// encoded length = ceil(srcLen/7)*8; decoded length <= (srcLen/8)*7.
uint32_t Encode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax);
uint32_t Decode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax);

// The Web UI handler. Owns the USB init, the SysEx parser, and the staging
// buffer for an in-progress snapshot upload. Lives on core 1.
class WebUI {
public:
	void Init(Spectrum *spec, Mapper *mapper);  // call once on core 1
	void Task();                                // call frequently on core 1

	// True when a complete decoded snapshot has arrived. The emulation loop, on
	// the pause->run transition, calls ApplyDecoded(machine) then ClearSnapshot().
	bool SnapshotReady() const { return snapReady_; }
	void ClearSnapshot() { snapReady_ = false; }
	// Apply the browser-decoded state (registers + border + paging) to the CPU.
	// RAM banks were written directly as pages arrived, so this just sets state.
	// Takes a Machine* as void* to avoid a header dependency cycle.
	void ApplyDecoded(void *machinePtr);

private:
	void SendSysEx(const uint8_t *data, uint32_t size);
	void OnSysEx(const uint8_t *data, uint32_t size); // decoded payload (no F0/id/F7)
	void ParseStream(const uint8_t *buf, uint32_t n);

	Spectrum *spec_ = nullptr;
	Mapper   *mapper_ = nullptr;

	// SysEx reassembly
	static constexpr uint32_t kRxBuf = 128;
	static constexpr uint32_t kMsgBuf = 512;   // one decoded SysEx message
	// Largest message we SEND is MSG_MAP_REPORT: 2 + SRC_COUNT*4 = 30 bytes.
	// Outgoing messages are framed into one buffer so the host receives each as a
	// single SysEx rather than header/body/footer fragments.
	static constexpr uint32_t kTxMsgMax = 64;
	uint8_t  msg_[kMsgBuf];
	uint32_t msgLen_ = 0;
	bool     inSysex_ = false;

	// Browser-decoded snapshot: the register/state block (the browser sends the
	// already-parsed machine state), captured on MSG_LOAD_BEGIN. RAM banks are
	// written directly into the Spectrum RAM as MSG_LOAD_PAGE messages arrive, so
	// there is NO large staging buffer on the RP2040 at all.
	uint8_t   decodedState_[32];       // regs/border/paging block from the browser
	uint32_t  decodedStateLen_ = 0;
	bool      snapReady_ = false;
};

} // namespace zx
