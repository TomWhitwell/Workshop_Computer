// gba.h — the few hardware conveniences more than one module needs.
//
// Kept deliberately small: each module still defines the registers it owns next to the code that
// uses them, as this payload always has. What lives here is what has to be spelled identically
// everywhere.
#pragma once

#include <stdint.h>

#define R16(off) (*(volatile uint16_t *)(0x04000000 + (off)))
#define R32(off) (*(volatile uint32_t *)(0x04000000 + (off)))

// ---- IWRAM placement --------------------------------------------------------------------------
// The image is linked for EWRAM, whose 16-bit bus costs a 32-bit ARM fetch three extra cycles.
// IWRAM is 32-bit and zero wait state, so the drawing primitives and the control tick live there:
// crt0.s copies the .iwram section in from its load address before main() and zeroes .iwram_bss.
//
// A plain BL reaches IWRAM from EWRAM (they are 16 MB apart and BL spans 32 MB), so no long_call.
// noinline because inlining an IWRAM function into an EWRAM caller would quietly run it from EWRAM.
//
// Three sections rather than one because GCC refuses to put code, read-only data and writable data
// in the same named section ("section type conflict").
#define IWRAM_CODE   __attribute__((section(".iwram"), noinline))
#define IWRAM_RODATA __attribute__((section(".iwram_rodata")))
#define IWRAM_BSS    __attribute__((section(".iwram_bss")))
