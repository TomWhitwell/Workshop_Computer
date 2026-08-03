// spectrum.h — ZX Spectrum 128K machine model for the Workshop Computer card.
//
// This header is the CONTRACT for the whole emulator: the memory map, the ULA /
// AY / keyboard state, and the cross-core interface between the Z80 (core 1) and
// the 48kHz ComputerCard I/O loop (core 0). Implementation lives in the .cpp
// files; the Z80 core itself plugs in via the MemRead/MemWrite/PortRead/PortWrite
// callbacks below.
//
// Design notes:
//   * 128K model: 8 x 16KB RAM pages + 2 x 16KB ROM (128 editor + 48 BASIC).
//   * The Z80 sees a flat 64KB space through four 16KB "slots"; slot contents are
//     switched by the 0x7FFD paging port. We resolve slot->page once per page
//     change, not per access, so reads/writes are a pointer add.
//   * Everything the two cores share lives in `CrossCore`, one field per writer,
//     `volatile`, single-writer — matching the lock-free approach in the repo's
//     second_core example.
//
// Timing target: 128K Spectrum = 3.5469 MHz, 70908 T-states/frame, 50.01 Hz.

#pragma once
#include <cstdint>

namespace zx {

// ---------------------------------------------------------------------------
// Timing constants (128K model)
// ---------------------------------------------------------------------------
constexpr uint32_t kCpuHz          = 3546900;   // 128K Z80 clock
constexpr uint32_t kTStatesPerFrame = 70908;    // per 50Hz frame (128K)
constexpr uint32_t kFrameHz        = 50;        // nominal
// 48K machine timing (real 48K runs slightly slower than the 128K):
constexpr uint32_t kCpuHz48         = 3500000;  // 48K Z80 clock (3.5 MHz)
constexpr uint32_t kTStatesPerFrame48 = 69888;  // per 50Hz frame (48K)

// ---------------------------------------------------------------------------
// Memory: 128KB RAM (8 pages) + 32KB ROM (2 pages), 16KB each.
// ---------------------------------------------------------------------------
constexpr uint32_t kPageSize = 0x4000;          // 16KB
constexpr uint32_t kNumRamPages = 8;
constexpr uint32_t kNumRomPages = 2;

struct Memory
{
	uint8_t ram[kNumRamPages][kPageSize];
	const uint8_t *rom[kNumRomPages];            // point at embedded ROM arrays

	// Four Z80 slots (0000-3FFF, 4000-7FFF, 8000-BFFF, C000-FFFF).
	// slot[0] is ROM; slots 1-3 are RAM. Resolved from the paging latch.
	const uint8_t *readSlot[4];                  // where reads come from
	uint8_t       *writeSlot[4];                 // where writes go (nullptr = ROM/RO)

	uint8_t pagingLatch = 0;                     // last value written to 0x7FFD
	bool    pagingLocked = false;                // bit 5 of 0x7FFD

	void Reset();                                // set 128K power-on paging
	void SetPaging(uint8_t value);               // apply a 0x7FFD write
	uint8_t ScreenPage() const;                  // 5 or 7 (shadow screen, bit 3)

	inline uint8_t Read(uint16_t a) const { return readSlot[a >> 14][a & 0x3FFF]; }
	inline void Write(uint16_t a, uint8_t v)
	{
		uint8_t *s = writeSlot[a >> 14];
		if (s) s[a & 0x3FFF] = v;                // writes to ROM slot are dropped
	}
};

// ---------------------------------------------------------------------------
// Keyboard: 8 half-rows x 5 bits, active-low. Read via IN A,(0xFE) with the
// row selected by the high address byte (A8..A15 = row select, active low).
// The mapping engine (core 0) sets/clears bits here; the ULA read consults it.
// ---------------------------------------------------------------------------
struct Keyboard
{
	// rows[r] bit b = 0 means that key is pressed. Power-on = 0xFF (all up).
	volatile uint8_t rows[8];
	void Reset() { for (int i = 0; i < 8; i++) rows[i] = 0x1F; }

	// ULA read for a given high address byte (the active-low row select).
	uint8_t Read(uint8_t highAddr) const;
};

// A single Spectrum key identified by (row, bitmask) in the matrix.
struct Key { uint8_t row; uint8_t bit; };
// Named keys (filled in spectrum.cpp) — used by the mapping engine + Web UI.
extern const Key kKeyTable[]; // indexed by an enum shared with the Web UI

// ---------------------------------------------------------------------------
// AY-3-8912: 16 registers, 3 square channels + noise + envelope.
// Rendered to a single mono sample on demand (core 0 pulls the latest value).
// ---------------------------------------------------------------------------
struct AY
{
	uint8_t reg[16];
	uint8_t selected = 0;

	// Generator state. The AY is clocked at CPU/2 (~1.7734MHz on the 128K); the
	// tone/noise/envelope dividers count in AY cycles. We accumulate emulated
	// T-states and step the generators in AY-clock units.
	uint32_t toneCounter[3];   // per-channel tone divider counters
	uint8_t  toneOut[3];       // per-channel square state (0/1)
	uint32_t noiseCounter;     // noise divider counter
	uint8_t  noiseOut;         // current noise bit
	uint32_t noiseRng;         // 17-bit LFSR
	uint32_t envCounter;       // envelope divider counter
	uint8_t  envStep;          // 0..31 position in the envelope
	uint8_t  envVol;           // current envelope volume 0..15
	bool     envHold;          // envelope finished holding
	uint32_t tstateFrac;       // leftover CPU T-states (for the /2 -> AY clock)

	void Reset();
	void Write(uint8_t r, uint8_t v);
	uint8_t Read(uint8_t r) const { return reg[r & 15]; }
	// Advance the generators by `tstates` of emulated CPU time and return the
	// current mixed output as a signed 12-bit sample for AudioOut.
	int16_t Render(uint32_t tstates);
};

// ---------------------------------------------------------------------------
// CrossCore: the ONLY state shared between core 1 (Z80/ULA/AY) and core 0
// (48kHz I/O). One writer per field. Core 0 reads out*/matrix is written by
// core 0; core 1 reads them. Kept deliberately tiny.
// ---------------------------------------------------------------------------
struct CrossCore
{
	// --- written by core 1 (emulator), read by core 0 (audio out) ---
	volatile bool     beeper;      // ULA 0xFE bit 4
	volatile bool     mic;         // ULA 0xFE bit 3
	volatile uint8_t  border;      // ULA 0xFE bits 0-2
	volatile int16_t  aySample;    // latest AY mix (signed 12-bit)

	// --- written by core 0 (mapping engine), read by core 1 (ULA/port in) ---
	// (Keyboard matrix lives in Keyboard::rows, also volatile.)
	volatile bool     earIn;       // tape EAR bit (from Audio In) — stretch
	volatile uint8_t  kempston;    // Kempston joystick byte (port 0x1F): 000FUDLR
	volatile uint8_t  knobY;       // Knob Y as 0-255, readable by the Z80 at port 0x5F
	// Per-source input values (0-255) exposed to the Z80 at fixed ports when a
	// source is mapped to TGT_PORT. Index = Source; 0xFF low-byte port per source
	// (see kSourcePort in mapping.h). Written by core 0, read by core 1 PortRead.
	volatile uint8_t  portVal[8];  // SRC_COUNT entries (pad to 8)

	// --- CV2 memory probe: core 0 sets the address, core 1 samples the byte ---
	volatile uint16_t probeAddr;   // RAM address to read (default 0x4000)
	volatile uint8_t  probeVal;    // latest byte at probeAddr (core 1 -> core 0 CV2)

	// --- time reference, written by core 0, read by core 1 for pacing ---
	volatile uint32_t sampleCount; // ++ every 48kHz sample

	// --- startup gate: core 1 sets true once the emulator is set up + a program
	// loaded, so core 0 can mute the audio outputs until then (kills a boot click
	// from the transient / the loaded program's first ULA writes). ---
	volatile bool audioReady;

	// --- control, written by core 0, consumed by core 1 ---
	volatile bool resetRequest;    // request snapshot reload (currently unbound)
	volatile bool paused;          // core 0 sets when Switch == Up (freeze)
	volatile uint16_t speedPct;    // emulation speed %, set from Knob Main. 100 =
	                               // real time (a centre deadzone holds exactly 100).
	// Per-mode emulated timing (set by the load path): 128K vs real 48K differ
	// slightly, so a 48K tune plays at true 48K pitch/tempo, not 128K.
	volatile uint32_t tsPerFrame;  // T-states per 50Hz frame (frame-INT boundary)
	volatile uint32_t tsPerSample; // T-states per 48kHz audio sample (pacing)

	// --- diagnostics, written by core 1, read by core 0 for LED heartbeat ---
	volatile uint32_t emuAlive;    // ++ each core-1 emulation batch (proves it runs)
	volatile uint32_t maxElapsed;  // worst-case samples/batch backlog (perf meter):
	                               // ~1 = keeping up; near the 96 cap = far too slow

	// --- machine mode, written by core 1 (load path), read by core 0 (LEDs) ---
	volatile uint8_t  mode;        // 0 = 128K, 1 = 48K, 2 = AY file (see MachineMode)
};

enum MachineMode : uint8_t { MODE_128K = 0, MODE_48K = 1, MODE_AY = 2 };

// ---------------------------------------------------------------------------
// The machine: owns memory, ULA/keyboard/AY state, and the Z80 hooks.
// The Z80 core (z80.h) is given pointers to Read/Write/In/Out on this.
// ---------------------------------------------------------------------------
struct Spectrum
{
	Memory    mem;
	Keyboard  kbd;
	AY        ay;
	CrossCore xc;

	uint32_t frameTStates = 0;     // T-states into the current frame
	bool     intPending = false;   // frame interrupt latch

	void Reset();
	void SetMode(uint8_t m);   // set mode + its emulated timing (48K vs 128K)
	void EmbedRoms(const uint8_t *rom128, const uint8_t *rom48);

	// Z80 bus callbacks (signatures the z80 core expects).
	uint8_t  MemRead(uint16_t addr);
	void     MemWrite(uint16_t addr, uint8_t val);
	uint8_t  PortRead(uint16_t port);
	void     PortWrite(uint16_t port, uint8_t val);

	// Advance ULA/interrupt bookkeeping by `t` T-states (called from the Z80
	// step loop). Raises intPending at frame boundaries.
	void AddTStates(uint32_t t);
};

} // namespace zx
