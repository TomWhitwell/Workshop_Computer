// machine.cpp — drives the superzazu/z80 core (MIT, instruction-stepped) against
// the Spectrum bus in spectrum.cpp. Runs on core 1.
//
// z80_step() executes one whole instruction and adds its exact T-states to
// z->cyc. We step until we've run the requested number of T-states, servicing
// the 50Hz frame interrupt at frame boundaries. Memory/IO go through the four
// bus callbacks below, which reach zx::Spectrum via z->userdata. Port decode
// uses z->port16 (our MOD adding the full 16-bit port address), because the
// Spectrum decodes A8-A15 for the keyboard rows, paging and AY.

extern "C" {
#include "z80.h"          // superzazu core (vendor/sz80/ on the include path)
}
#include "spectrum.h"
#include "machine.h"

namespace zx {

// --- Bus callbacks (C ABI, reached from the C core) -------------------------
static uint8_t MemReadCB(void *ud, uint16_t addr)
{
	return static_cast<Spectrum *>(ud)->MemRead(addr);
}
static void MemWriteCB(void *ud, uint16_t addr, uint8_t val)
{
	static_cast<Spectrum *>(ud)->MemWrite(addr, val);
}
static uint8_t PortInCB(z80 *z, uint8_t /*lowport*/)
{
	// Use the full 16-bit port captured by our MOD, not just the low byte.
	return static_cast<Spectrum *>(z->userdata)->PortRead(z->port16);
}
static void PortOutCB(z80 *z, uint8_t /*lowport*/, uint8_t val)
{
	static_cast<Spectrum *>(z->userdata)->PortWrite(z->port16, val);
}

// ----------------------------------------------------------------------------
void Machine::Attach(Spectrum *spec)
{
	spec_ = spec;
	z80_init(&cpu_);
	cpu_.userdata   = spec;
	cpu_.read_byte  = MemReadCB;
	cpu_.write_byte = MemWriteCB;
	cpu_.port_in    = PortInCB;
	cpu_.port_out   = PortOutCB;
	frameT_ = 0;
}

void Machine::Reset()
{
	// Re-init CPU (clears registers, PC=0) but keep the bus bindings.
	void *ud            = cpu_.userdata;
	auto rb = cpu_.read_byte; auto wb = cpu_.write_byte;
	auto pin = cpu_.port_in;  auto pout = cpu_.port_out;
	z80_init(&cpu_);
	cpu_.userdata = ud;
	cpu_.read_byte = rb; cpu_.write_byte = wb;
	cpu_.port_in = pin;  cpu_.port_out = pout;

	spec_->Reset();
	frameT_ = 0;
}

// Run approximately `tstates` T-states. Instruction-stepped: each z80_step runs
// one instruction (several T-states), so we overshoot slightly and carry the
// remainder via the cyc counter. Frame interrupt (IM1, vector 0xFF) is raised at
// each 50Hz frame boundary.
void Machine::Run(uint32_t tstates)
{
	z80 *z = &cpu_;
	unsigned long target = z->cyc + tstates;

	while (z->cyc < target)
	{
		unsigned long before = z->cyc;
		z80_step(z);
		uint32_t used = uint32_t(z->cyc - before);

		frameT_ += used;
		// Per-mode frame length (48K = 69888, 128K = 70908) so a 48K program's
		// interrupt tempo is correct, not 128K's.
		uint32_t framelen = spec_->xc.tsPerFrame;
		if (frameT_ >= framelen)
		{
			frameT_ -= framelen;
			z80_gen_int(z, 0xFF); // maskable frame interrupt, IM1 vector
		}
	}

	// Advance the AY by this batch's worth of T-states and publish the current
	// mixed sample for core 0 to send to Audio Out 2. A batch is ~one 48kHz
	// sample of emulated time, so this produces AY audio at ~48kHz.
	spec_->xc.aySample = spec_->ay.Render(tstates);

	// Sample the CV2 memory-probe byte here (core 1 owns the memory), for core 0
	// to output on CV2. Reading via MemRead honours the current paging.
	spec_->xc.probeVal = spec_->MemRead(spec_->xc.probeAddr);
}

} // namespace zx
