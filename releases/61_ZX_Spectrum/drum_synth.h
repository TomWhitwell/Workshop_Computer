// Synthesized drum data harvested from the Beepola engine sources.
// See reference/MusicBox.asm and reference/Savage.asm.
#pragma once
#include <cstdint>

// Music Box KICK envelope: period values the kick oscillator walks through
// (rising period = falling pitch → the classic short beeper "boom").
static const uint8_t kKickEnvelope[15] = {
	0x0C, 0x0D, 0x0E, 0x0F, 0x0F, 0x10, 0x11, 0x12,
	0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
};

// Savage LFSR noise-drum seed/mask pairs (BC in the original: B=seed, C=mask).
// The colour of the noise comes from the AND-mask applied to the LFSR feedback.
static const uint8_t kSavageNoise[5][2] = {
	{0xE0, 0x1F}, {0xC0, 0xA1}, {0x20, 0x0F}, {0xCF, 0x3F}, {0x28, 0x3F},
};
