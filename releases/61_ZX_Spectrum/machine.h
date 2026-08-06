// machine.h — the Z80 + Spectrum runner (core 1 side).
//
// Uses the superzazu/z80 core (MIT): INSTRUCTION-STEPPED with exact T-state
// counting (z80_step executes one whole instruction and adds its cycles to
// z->cyc). This is ~3-4x faster than a cycle-stepped core — necessary to run
// the Spectrum at real time on the RP2040 — while keeping exact instruction
// timing (the ULA/beeper stay synced at instruction granularity, which is fine
// for the border/beeper/50Hz interrupt).
#pragma once
// z80.h is a C header with no extern "C" guard of its own; wrap it so C++ TUs
// (webui.cpp, machine.cpp) all see C linkage and link against the C core.
extern "C" {
#include "z80.h"          // superzazu core (vendor/sz80/ on the include path)
}
#include "spectrum.h"

namespace zx {

class Machine
{
public:
	void Attach(Spectrum *spec);   // bind to a Spectrum and power-on the CPU
	void Reset();                  // reset CPU + machine
	void Run(uint32_t tstates);    // emulate ~tstates T-states, handling frame INT

	z80      *Cpu()  { return &cpu_; }   // for the snapshot loader (register restore)
	Spectrum *Spec() { return spec_; }

private:
	z80       cpu_{};
	Spectrum *spec_ = nullptr;
	uint32_t  frameT_ = 0;         // T-states into the current 50Hz frame
};

// Load a snapshot into the machine. Defined in snapshot.cpp. Call after
// Attach()/Reset(); restores registers + RAM + paging.
//   LoadSnapshot  — auto-detects .sna vs .z80 by size, dispatches accordingly.
//   LoadZ80/LoadSNA — the specific loaders.
bool LoadSnapshot(Machine &m, const uint8_t *data, uint32_t len);
bool LoadZ80(Machine &m, const uint8_t *data, uint32_t len);
bool LoadSNA(Machine &m, const uint8_t *data, uint32_t len);

} // namespace zx
