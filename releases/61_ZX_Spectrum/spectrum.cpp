// spectrum.cpp — 128K memory map, paging (0x7FFD), ULA port (0xFE), keyboard
// matrix read, and AY port routing. This is the "bus" the Z80 core drives.

#include "spectrum.h"
#include "mapping.h"    // kSourcePort / SRC_COUNT for the TGT_PORT input ports
#include "rom128_0.h"   // rom128_0[]  — 128K editor ROM (bank 0)
#include "rom128_1.h"   // rom128_1[]  — 48 BASIC ROM   (bank 1)

namespace zx {

// ---------------------------------------------------------------------------
// Memory / paging
// ---------------------------------------------------------------------------
void Memory::Reset()
{
	rom[0] = rom128_0;   // 128K editor
	rom[1] = rom128_1;   // 48 BASIC
	pagingLatch = 0;
	pagingLocked = false;
	SetPaging(0);        // power-on: ROM0 in, RAM5/RAM2/RAM0, screen = page 5
}

// Apply a write to the 0x7FFD paging latch.
//   bits 0-2 : RAM page mapped into slot 3 (0xC000-0xFFFF)
//   bit  3   : screen select (0 = page 5, 1 = page 7 "shadow")
//   bit  4   : ROM select (0 = 128 editor, 1 = 48 BASIC)
//   bit  5   : paging disable (locks until reset)
void Memory::SetPaging(uint8_t value)
{
	if (pagingLocked) return;
	pagingLatch = value;
	pagingLocked = value & 0x20;

	const uint8_t romSel  = (value >> 4) & 1;
	const uint8_t highRam = value & 0x07;

	// Fixed layout of the 128K:
	//   slot 0 (0000): selected ROM (read-only)
	//   slot 1 (4000): RAM page 5
	//   slot 2 (8000): RAM page 2
	//   slot 3 (C000): RAM page (bits 0-2)
	readSlot[0] = rom[romSel];        writeSlot[0] = nullptr;
	readSlot[1] = ram[5];             writeSlot[1] = ram[5];
	readSlot[2] = ram[2];             writeSlot[2] = ram[2];
	readSlot[3] = ram[highRam];       writeSlot[3] = ram[highRam];
}

uint8_t Memory::ScreenPage() const
{
	return (pagingLatch & 0x08) ? 7 : 5;
}

// ---------------------------------------------------------------------------
// Keyboard matrix read (ULA 0xFE, active-low row select in the high addr byte)
// ---------------------------------------------------------------------------
uint8_t Keyboard::Read(uint8_t highAddr) const
{
	// Each 0 bit in highAddr selects a half-row; combine (AND) the selected
	// rows' key bits. Bits 0-4 = keys (active low), upper bits set later by ULA.
	uint8_t result = 0x1F;
	for (int r = 0; r < 8; r++)
		if (!(highAddr & (1 << r)))
			result &= rows[r];
	return result;
}

// The Spectrum keyboard matrix: row (selected by address line A8+r low) and the
// bit within the 5-bit half-row. Order matches the Web UI's key enum.
const Key kKeyTable[] = {
	// row 0: CAPS SHIFT, Z, X, C, V
	{0,0x01},{0,0x02},{0,0x04},{0,0x08},{0,0x10},
	// row 1: A, S, D, F, G
	{1,0x01},{1,0x02},{1,0x04},{1,0x08},{1,0x10},
	// row 2: Q, W, E, R, T
	{2,0x01},{2,0x02},{2,0x04},{2,0x08},{2,0x10},
	// row 3: 1, 2, 3, 4, 5
	{3,0x01},{3,0x02},{3,0x04},{3,0x08},{3,0x10},
	// row 4: 0, 9, 8, 7, 6
	{4,0x01},{4,0x02},{4,0x04},{4,0x08},{4,0x10},
	// row 5: P, O, I, U, Y
	{5,0x01},{5,0x02},{5,0x04},{5,0x08},{5,0x10},
	// row 6: ENTER, L, K, J, H
	{6,0x01},{6,0x02},{6,0x04},{6,0x08},{6,0x10},
	// row 7: SPACE, SYM SHIFT, M, N, B
	{7,0x01},{7,0x02},{7,0x04},{7,0x08},{7,0x10},
};

// ---------------------------------------------------------------------------
// Spectrum: wiring the parts + the Z80 bus callbacks
// ---------------------------------------------------------------------------
void Spectrum::Reset()
{
	mem.Reset();
	kbd.Reset();
	ay.Reset();
	xc.beeper = xc.mic = false;
	xc.border = 0;
	xc.aySample = 0;
	xc.earIn = false;
	xc.kempston = 0;
	xc.knobY = 0;
	// Idle at 0xFF, not 0x00: an unmapped source's port must look like the
	// floating bus (which idles high), or a program polling it waits forever for
	// a bit that never goes high. 0x00 is the Kempston convention and is wrong
	// here. Mapper::Apply overwrites this for sources actually mapped to a port.
	for (int i = 0; i < 8; i++) xc.portVal[i] = 0xFF;
	xc.probeAddr = 0x4000;   // default probe = start of screen memory (16384)
	xc.probeVal = 0;
	xc.sampleCount = 0;
	xc.audioReady = false;   // core 0 mutes audio until core 1 signals ready
	xc.speedPct = 100;   // default real-time until core 0 reads Knob Main
	xc.mode = MODE_128K; // default; load path sets 48K/AY
	xc.tsPerFrame  = kTStatesPerFrame;      // 128K timing by default
	xc.tsPerSample = kCpuHz / 48000;
	// AY live-mangling controls default to "no effect" (duty 128 = 50% square,
	// no period bend, nothing muted) so playback is untouched until CV is patched.
	xc.ayDuty[0] = xc.ayDuty[1] = xc.ayDuty[2] = 128;
	xc.ayEnvMod = 0;
	xc.ayNoiseMod = 0;
	xc.ayMuteMask = 0;
	frameTStates = 0;
	intPending = false;
}

// Set the machine mode and its emulated timing together. 48K uses real 48K
// timing (3.5MHz / 69888 T/frame); 128K and AY use 128K timing.
void Spectrum::SetMode(uint8_t m)
{
	xc.mode = m;
	if (m == MODE_48K)
	{
		xc.tsPerFrame  = kTStatesPerFrame48;
		xc.tsPerSample = kCpuHz48 / 48000;
	}
	else
	{
		xc.tsPerFrame  = kTStatesPerFrame;
		xc.tsPerSample = kCpuHz / 48000;
	}
}

void Spectrum::EmbedRoms(const uint8_t *rom128, const uint8_t *rom48)
{
	// Allow overriding the default embedded ROMs (e.g. a 48K mode later).
	mem.rom[0] = rom128;
	mem.rom[1] = rom48;
}

uint8_t Spectrum::MemRead(uint16_t addr)  { return mem.Read(addr); }
void    Spectrum::MemWrite(uint16_t addr, uint8_t val) { mem.Write(addr, val); }

uint8_t Spectrum::PortRead(uint16_t port)
{
	// ULA responds on even ports (A0 = 0).
	if ((port & 0x0001) == 0)
	{
		uint8_t v = kbd.Read(port >> 8); // keyboard bits 0-4
		// bit 6 = EAR (tape in); bit 7 set; bit 5 unused (1).
		v |= 0xA0;
		if (xc.earIn) v |= 0x40;
		return v;
	}
	// AY register read: port 0xFFFD (A15=1, A14=1).
	if ((port & 0xC000) == 0xC000)
		return ay.Read(ay.selected);
	// Knob Y peripheral: read the knob (0-255) at port 0x5F (low byte 0x5F).
	// 0x5F is odd (not ULA), A5=1 (not Kempston), not A15/A14 (not AY) — free.
	if ((port & 0x00FF) == 0x5F)
		return xc.knobY;
	// Per-source input ports (TGT_PORT): a CV/Audio/etc. value at its fixed port.
	// These low bytes have bit0/bit1/bit5 set (see kSourcePort), so they can never
	// collide with the ULA, Kempston or 128K paging decodes. Sources not mapped to
	// a port read 0xFF here, matching the floating bus a real machine would show.
	{
		uint8_t low = port & 0x00FF;
		for (int s = 0; s < SRC_COUNT; s++)
			if (low == kSourcePort[s])
				return xc.portVal[s];
	}
	// Kempston joystick at ports where A5 low (commonly 0x1F). 000FUDLR.
	// (Checked after AY so 0xFFFD isn't misread as Kempston.)
	if ((port & 0x0020) == 0)
		return xc.kempston;
	return 0xFF; // floating bus (simplified)
}

void Spectrum::PortWrite(uint16_t port, uint8_t val)
{
	// ULA (even ports): border (0-2), MIC (3), beeper (4).
	if ((port & 0x0001) == 0)
	{
		xc.border = val & 0x07;
		xc.mic    = val & 0x08;
		xc.beeper = val & 0x10;
	}
	// 128K paging latch 0x7FFD: decoded as A15=0 AND A1=0 (0xxx...xx0x).
	if ((port & 0x8002) == 0x0000)
		mem.SetPaging(val);
	// AY register select 0xFFFD (A15=1,A14=1) / data write 0xBFFD (A15=1,A14=0).
	if ((port & 0xC000) == 0xC000)
		ay.selected = val & 0x0F;
	else if ((port & 0xC000) == 0x8000)
		ay.Write(ay.selected, val);
}

void Spectrum::AddTStates(uint32_t t)
{
	frameTStates += t;
	if (frameTStates >= kTStatesPerFrame)
	{
		frameTStates -= kTStatesPerFrame;
		intPending = true; // frame interrupt (held ~32 T-states on real HW)
	}
}

} // namespace zx
