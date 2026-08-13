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

	// AY live-mangling accumulators. Start from "no effect" each sample; a jack
	// mapped to an AY target overrides these. These are evaluated and published
	// unconditionally, regardless of the current machine mode: core 1 gates on
	// MODE_AY in Machine::Run, so it costs nothing here and means a mapping sent
	// while paused (before a tune has been loaded and the mode switched to
	// MODE_AY) still takes effect the moment playback starts.
	uint8_t  ayDuty[3] = { 128, 128, 128 };
	int16_t  ayEnv = 0, ayNoise = 0;
	uint8_t  ayMute = 0;

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

		// AY targets act on the sound chip, not the keyboard, so a single mapping
		// table serves both modes. Core 1 applies these only in MODE_AY, which is
		// what makes them inert during ZX games.
		if (m.kind == TGT_AY_DUTY || m.kind == TGT_AY_ENV ||
		    m.kind == TGT_AY_NOISE || m.kind == TGT_AY_MUTE)
		{
			// Continuous value centred at 128; depth scales the mangle amount.
			int32_t dev = (int32_t)srcValue[s] - 128;         // -128..+127
			int32_t scaled = (dev * m.depth) / 255;           // depth 0..255
			switch (m.kind)
			{
			case TGT_AY_DUTY: {
				// Fold around centre: 128 (50% square) thinning toward a narrow
				// pulse in EITHER CV direction. A square's timbre is symmetric
				// about 50% — 25% and 75% sound identical — so mapping the CV
				// straight onto 1..255 spends half its travel repeating the same
				// sweep. Folding makes every step change the tone.
				int32_t d = 128 - (scaled < 0 ? -scaled : scaled);
				if (d < 4) d = 4;                             // <1.5% aliases to silence
				if (d > 128) d = 128;
				uint8_t ch = m.arg < 3 ? m.arg : 0;
				ayDuty[ch] = (uint8_t)d;
				break;
			}
			case TGT_AY_ENV:
				// Signed exponent, not an offset: core 1 scales the tune's own
				// envelope period by 2^(envMod/64), giving the same ~4-octave
				// sweep whatever period the tune set. A fixed offset was huge on
				// short periods (instant clamp) and inaudible on long ones.
				ayEnv = (int16_t)scaled;                      // -128..+127 -> ∓2 oct
				break;
			case TGT_AY_NOISE:
				ayNoise = (int16_t)(scaled >> 3);             // ~±15 across the 5-bit period
				break;
			case TGT_AY_MUTE: {
				bool active = srcActive[s] ^ m.invert;        // gate mutes while held
				if (active && m.arg < 3) ayMute |= (1u << m.arg);
				break;
			}
			default: break;
			}
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

	// Publish the AY mangling controls for core 1's AY::Render. Published in every
	// mode (core 1 reads them only in MODE_AY) so the values are already correct
	// when a tune starts, rather than needing a fresh Apply() after the mode flips.
	spec.xc.ayDuty[0] = ayDuty[0];
	spec.xc.ayDuty[1] = ayDuty[1];
	spec.xc.ayDuty[2] = ayDuty[2];
	spec.xc.ayEnvMod   = ayEnv;
	spec.xc.ayNoiseMod = ayNoise;
	spec.xc.ayMuteMask = ayMute;
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
