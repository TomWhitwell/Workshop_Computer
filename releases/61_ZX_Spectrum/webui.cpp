// webui.cpp — USB-MIDI/SysEx Web UI transport + protocol implementation.

#include "webui.h"
#include "machine.h"   // Machine + z80 (for ApplyDecoded)
#include "tusb.h"
#include <cstdlib>
#include <cstring>

namespace zx {

// --- 7-bit codec ------------------------------------------------------------
// Encode groups of 7 bytes into 8 septets: first septet holds the 7 high bits
// (bit i = high bit of source byte i), followed by the 7 low-7-bit values.
uint32_t Encode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax)
{
	uint32_t di = 0;
	for (uint32_t i = 0; i < srcLen; i += 7)
	{
		uint32_t n = (srcLen - i < 7) ? (srcLen - i) : 7;
		if (di + 1 + n > dstMax) break;
		uint8_t high = 0;
		for (uint32_t j = 0; j < n; j++)
			if (src[i + j] & 0x80) high |= (1 << j);
		dst[di++] = high;
		for (uint32_t j = 0; j < n; j++)
			dst[di++] = src[i + j] & 0x7F;
	}
	return di;
}

uint32_t Decode7bit(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstMax)
{
	uint32_t di = 0;
	uint32_t i = 0;
	while (i < srcLen)
	{
		uint8_t high = src[i++];
		for (uint32_t j = 0; j < 7 && i < srcLen && di < dstMax; j++)
		{
			uint8_t v = src[i++] & 0x7F;
			if (high & (1 << j)) v |= 0x80;
			dst[di++] = v;
		}
	}
	return di;
}

// --- USB send ---------------------------------------------------------------
// The TX FIFO is CFG_TUD_MIDI_TX_BUFSIZE (64 bytes at full speed, which is what
// the RP2040 is), and tud_midi_stream_write expands each 3 SysEx bytes into a
// 4-byte USB-MIDI event packet — so it holds only ~48 bytes of message. Anything
// longer must be drained mid-write by tud_task(). Call it on EVERY iteration,
// not just when a write returns 0: a partial write leaves a full FIFO, and
// looping straight back into another write without draining just spins.
static void MidiWriteBlocking(const uint8_t *data, uint32_t size)
{
	uint32_t sent = 0;
	while (sent < size)
	{
		sent += tud_midi_stream_write(0, data + sent, size - sent);
		tud_task();
	}
}

// Emit F0 <manuf> <payload> F7 as ONE stream write. Writing the header, body and
// footer as three separate calls let the FIFO flush between them, so the host saw
// the message split across several WebMIDI events — and a continuation fragment
// carries no 0xF0 for the browser to recognise.
void WebUI::SendSysEx(const uint8_t *data, uint32_t size)
{
	uint8_t frame[2 + kTxMsgMax + 1];
	if (size > kTxMsgMax) return;            // caller bug; never silently truncate
	frame[0] = 0xF0;
	frame[1] = ZX_MANUFACTURER_ID;
	memcpy(frame + 2, data, size);
	frame[2 + size] = 0xF7;
	MidiWriteBlocking(frame, 2 + size + 1);
}

// --- Init / task ------------------------------------------------------------
void WebUI::Init(Spectrum *spec, Mapper *mapper)
{
	spec_ = spec;
	mapper_ = mapper;
	// No large staging buffer: the browser decodes snapshots and streams raw RAM
	// pages that we write straight into spec->mem.ram, plus a tiny state block.
	msgLen_ = 0;
	inSysex_ = false;
	snapReady_ = false;
	tusb_init();
}

// Apply the browser-decoded machine state to the CPU. RAM banks were already
// written directly by MSG_LOAD_PAGE. State block layout (little-endian words):
//   [0] A   [1] F   [2] C [3] B   [4] E [5] D   [6] L [7] H
//   [8] PClo [9] PChi   [10] SPlo [11] SPhi   [12] I   [13] R
//   [14] IM&flags (bit0-1=IM, bit2=IFF1, bit3=IFF2)   [15] border
//   [16] 0x7FFD paging   [17] is128 (1/0)
//   [18] A' [19] F' [20] C' [21] B' [22] E' [23] D' [24] L' [25] H'
//   [26] IXlo [27] IXhi [28] IYlo [29] IYhi
void WebUI::ApplyDecoded(void *machinePtr)
{
	Machine *m = static_cast<Machine *>(machinePtr);
	z80 *z = m->Cpu();
	const uint8_t *s = decodedState_;
	if (decodedStateLen_ < 30) return;

	// Clear the CPU's INTERNAL state (iff_delay, int_pending, mem_ptr, prefix/
	// step, interrupt latches) without wiping RAM — the browser already filled
	// the RAM banks. Without this, residual state from a previous program (e.g.
	// the AY/PT3 player's IM2 interrupts) survives and corrupts the new snapshot,
	// so a .z80 loaded after an .ay/.pt3 wouldn't run until a hardware reset.
	// z80_init NULLs the bus callbacks, so save + restore them around it.
	auto rb = z->read_byte; auto wb = z->write_byte;
	auto pin = z->port_in;  auto pout = z->port_out; void *ud = z->userdata;
	z80_init(z);
	z->read_byte = rb; z->write_byte = wb;
	z->port_in = pin;  z->port_out = pout; z->userdata = ud;

	z->a = s[0];
	auto setF = [&](uint8_t f){
		z->sf=(f>>7)&1; z->zf=(f>>6)&1; z->yf=(f>>5)&1; z->hf=(f>>4)&1;
		z->xf=(f>>3)&1; z->pf=(f>>2)&1; z->nf=(f>>1)&1; z->cf=f&1; };
	setF(s[1]);
	z->c=s[2]; z->b=s[3]; z->e=s[4]; z->d=s[5]; z->l=s[6]; z->h=s[7];
	z->pc = s[8] | (s[9] << 8);
	z->sp = s[10] | (s[11] << 8);
	z->i = s[12]; z->r = s[13];
	uint8_t imf = s[14];
	z->interrupt_mode = imf & 3;
	z->iff1 = (imf >> 2) & 1;
	z->iff2 = (imf >> 3) & 1;
	spec_->xc.border = s[15] & 7;
	spec_->mem.SetPaging(s[16]);
	// s[17] mode byte: 0 = 48K, 1 = 128K, 2 = AY file. AY uses the 48K ROM
	// (paging 0x10 already selects the 48 BASIC ROM, which has the IM1 0x38
	// handler the .ay player relies on).
	spec_->SetMode((s[17] == 2) ? MODE_AY : (s[17] == 1) ? MODE_128K : MODE_48K);
	z->a_=s[18]; z->f_=s[19]; z->c_=s[20]; z->b_=s[21];
	z->e_=s[22]; z->d_=s[23]; z->l_=s[24]; z->h_=s[25];
	z->ix = s[26] | (s[27] << 8);
	z->iy = s[28] | (s[29] << 8);

	// Silence the AY when loading a new program, so a previous tune (or the AY
	// player) doesn't keep buzzing under a snapshot that doesn't use the AY.
	spec_->ay.Reset();
}

void WebUI::Task()
{
	tud_task();
	uint8_t rx[kRxBuf];
	while (tud_midi_available())
	{
		uint32_t n = tud_midi_stream_read(rx, sizeof(rx));
		if (n > 0) ParseStream(rx, n);
	}
}

// SysEx framing FSM (same shape as the repo's web_interface example).
void WebUI::ParseStream(const uint8_t *buf, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
	{
		uint8_t b = buf[i];
		if (!inSysex_)
		{
			if (b == 0xF0) { inSysex_ = true; msgLen_ = 0; }
		}
		else
		{
			if (b == 0xF7)
			{
				// msg_[0] = manufacturer id, msg_[1..] = payload
				if (msgLen_ >= 1 && msg_[0] == ZX_MANUFACTURER_ID)
					OnSysEx(msg_ + 1, msgLen_ - 1);
				inSysex_ = false;
				msgLen_ = 0;
			}
			else if (msgLen_ < kMsgBuf)
			{
				msg_[msgLen_++] = b;
			}
		}
	}
}

// --- Protocol handlers ------------------------------------------------------
void WebUI::OnSysEx(const uint8_t *data, uint32_t size)
{
	if (size < 1) return;
	uint8_t id = data[0];

	switch (id)
	{
	case MSG_HELLO:
	{
		// Keep in sync with Version: in info.yaml — the Web UI shows this in its
		// connected banner, so a stale value here misreports the running firmware.
		uint8_t info[] = { MSG_INFO, 1, 2, 1 }; // version 1.2.1
		SendSysEx(info, sizeof(info));
		break;
	}

	case MSG_LOAD_BEGIN:
	{
		// The browser has decoded the snapshot; payload after id is the machine
		// state block (registers, border, paging, IM/IFF), 7-bit encoded.
		decodedStateLen_ = Decode7bit(data + 1, size - 1,
		                              decodedState_, sizeof(decodedState_));
		snapReady_ = false;
		uint8_t ack[] = { MSG_STATUS, 4 }; // 4 = ready for pages
		SendSysEx(ack, sizeof(ack));
		break;
	}

	case MSG_LOAD_PAGE:
	{
		// payload: id, bank(0..7), offsetHi, offsetLo (in 256-byte units? no —
		// full offset as 2 septets *128), then 7-bit-encoded raw bytes.
		// Layout: data[1]=bank, data[2..3]=offset (14-bit, 2 septets), data[4..]=enc.
		if (size < 4) break;
		uint8_t bank = data[1] & 0x07;
		uint32_t offset = (uint32_t(data[2]) << 7) | uint32_t(data[3]);
		uint8_t raw[kMsgBuf];
		uint32_t n = Decode7bit(data + 4, size - 4, raw, sizeof(raw));
		if (offset + n <= 0x4000)                 // stay within the 16KB bank
			memcpy(&spec_->mem.ram[bank][offset], raw, n);
		break;
	}

	case MSG_LOAD_END:
	{
		snapReady_ = true;   // apply state on the pause->run transition
		uint8_t st[] = { MSG_STATUS, 1 }; // 1 = loaded
		SendSysEx(st, sizeof(st));
		break;
	}

	case MSG_MAP_GET:
	{
		// Report the mapping table: for each of SRC_COUNT sources, 4 bytes:
		// kind, arg, threshold-high-nibble, depth7 (all 7-bit safe for SysEx).
		uint8_t rep[2 + SRC_COUNT * 4];
		uint32_t p = 0;
		rep[p++] = MSG_MAP_REPORT;
		rep[p++] = SRC_COUNT;
		for (int s = 0; s < SRC_COUNT; s++)
		{
			const Mapping &mp = mapper_->GetMapping((Source)s);
			rep[p++] = mp.kind & 0x7F;
			rep[p++] = mp.arg & 0x7F;
			rep[p++] = uint8_t((mp.threshold >> 4) & 0x7F); // coarse threshold
			rep[p++] = uint8_t((mp.depth >> 1) & 0x7F);     // AY mangle depth (7-bit)
		}
		SendSysEx(rep, p);
		break;
	}

	case MSG_MAP_SET:
	{
		// payload: id, count, then count*(kind,arg,threshold7[,depth7]) records.
		// The depth byte is optional (older UIs send 3-byte records) — detect the
		// record width from the payload length so the last record isn't misread.
		if (size < 2) break;
		uint8_t count = data[1];
		uint32_t p = 2;
		uint32_t recBytes = count ? (size - 2) / count : 0;
		bool hasDepth = (recBytes >= 4);
		// Need a whole record left: 3 bytes, or 4 when a depth byte is present.
		uint32_t need = hasDepth ? 4 : 3;
		for (int s = 0; s < count && s < SRC_COUNT && p + need <= size; s++)
		{
			Mapping mp;
			mp.kind      = (TargetKind)data[p++];
			mp.arg       = data[p++];
			mp.threshold = int16_t(data[p++]) << 4;
			mp.invert    = false;
			// 4th byte (when present) = depth (0..127 -> 0..254); else full.
			mp.depth     = hasDepth ? uint8_t(data[p++] << 1) : 255;
			mapper_->SetMapping((Source)s, mp);
		}
		uint8_t st[] = { MSG_STATUS, 2 }; // 2 = mapping updated
		SendSysEx(st, sizeof(st));
		break;
	}

	case MSG_KEY:
		// payload: id, keyIndex, down(0/1) — keyboard passthrough.
		if (size >= 3) mapper_->PassthroughKey(data[1], data[2] != 0);
		break;

	case MSG_KEY_RESET:
		mapper_->PassthroughReset();
		break;

	case MSG_SET_PROBE:
		// payload: id + 3 septets = 16-bit CV2 memory-probe address.
		if (size >= 4)
			spec_->xc.probeAddr = uint16_t(data[1] | (data[2] << 7) | (data[3] << 14));
		break;

	default: break;
	}
}

} // namespace zx
