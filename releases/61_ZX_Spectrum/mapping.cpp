// mapping.cpp — input-mapping engine implementation.

#include "mapping.h"

namespace zx {

void Mapper::LoadDefaults()
{
	for (int i = 0; i < SRC_COUNT; i++)
		table_[i] = { TGT_NONE, 0, 0, false };

	// Starting patch (all keys, held-while-high; Kempston comes later):
	//   Pulse In 1  -> ENTER
	//   Pulse In 2  -> SPACE
	//   CV In 1     -> Q
	//   CV In 2     -> A
	//   Audio In 1  -> O
	//   Audio In 2  -> P
	//   Switch Down -> ENTER   (momentary hand-tap)
	// CV/Audio inputs are bipolar 12-bit (-2048..2047); threshold 512 is a bit
	// above centre so a quiet/centred signal doesn't trigger.
	table_[SRC_PULSE1] = { TGT_KEY, KEY_ENTER, 0, false };
	table_[SRC_PULSE2] = { TGT_KEY, KEY_SPACE, 0, false };
	table_[SRC_CV1]    = { TGT_KEY, KEY_Q, 1024, false };
	table_[SRC_CV2]    = { TGT_KEY, KEY_A, 1024, false };
	table_[SRC_AUDIO1] = { TGT_KEY, KEY_O, 1024, false };
	table_[SRC_AUDIO2] = { TGT_KEY, KEY_P, 1024, false };
	table_[SRC_SWITCH] = { TGT_KEY, KEY_ENTER, 0, false };
}

void Mapper::Apply(const bool srcActive[SRC_COUNT], const uint8_t srcValue[SRC_COUNT], Spectrum &spec)
{
	// Rebuild the keyboard matrix from "all up" each sample, then press the keys
	// whose sources are active. (Rebuilding avoids stuck keys.)
	uint8_t rows[8] = { 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F };
	uint8_t kemp = 0;

	for (int s = 0; s < SRC_COUNT; s++)
	{
		const Mapping &m = table_[s];
		if (m.kind == TGT_NONE) continue;

		// TGT_PORT is a continuous value (not a gate): publish the source's 0-255
		// value to its fixed Z80 port, ignoring active/threshold.
		if (m.kind == TGT_PORT)
		{
			spec.xc.portVal[s] = srcValue[s];
			continue;
		}

		bool active = srcActive[s] ^ m.invert;
		if (!active) continue;

		if (m.kind == TGT_KEY && m.arg < KEY_COUNT)
		{
			const Key &k = kKeyTable[m.arg];
			rows[k.row] &= ~k.bit;         // active-low: 0 = pressed
		}
		else if (m.kind == TGT_KEMPSTON)
		{
			kemp |= m.arg;
		}
	}

	// OR in any keyboard-passthrough keys (laptop keys via the Web UI), so jack
	// mappings and typed keys coexist.
	for (int idx = 0; idx < KEY_COUNT; idx++)
	{
		if (passthrough_[idx >> 5] & (1u << (idx & 31)))
		{
			const Key &k = kKeyTable[idx];
			rows[k.row] &= ~k.bit;
		}
	}

	// Publish to the shared state core 1 reads.
	for (int i = 0; i < 8; i++) spec.kbd.rows[i] = rows[i];
	spec.xc.kempston = kemp;
}

void Mapper::PassthroughKey(uint8_t keyIndex, bool down)
{
	if (keyIndex >= KEY_COUNT) return;
	uint32_t mask = 1u << (keyIndex & 31);
	if (down) passthrough_[keyIndex >> 5] |= mask;
	else      passthrough_[keyIndex >> 5] &= ~mask;
}

void Mapper::PassthroughReset()
{
	passthrough_[0] = 0;
	passthrough_[1] = 0;
}

} // namespace zx
