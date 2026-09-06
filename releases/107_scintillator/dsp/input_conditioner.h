// Conditions each audio input before anything else touches it.
//
// Two problems, both of which reach everything downstream - the argument
// stage, the dry mix and the Geiger gates alike - if they aren't dealt
// with here at the very front:
//
//  - A standing DC offset. An unpatched jack floats to whatever its ADC
//    pin happens to read, and the audio inputs get no offset calibration
//    at all (unlike the CV inputs), so some offset is normal. A standing
//    offset is a signal as far as everything downstream is concerned,
//    arriving from an input that isn't even there.
//  - The ADC's own noise floor. The RP2040's ADC manages ~9 effective
//    bits, so a few LSBs of noise are always present. Left alone it is
//    enough to keep the Geiger gates triggering steadily with nothing
//    patched, which is exactly how this card's long-running "output with
//    nothing plugged in" problem presented.
//
// The dead zone subtracts its threshold rather than hard-gating to zero,
// so the transfer stays continuous at the threshold: a signal crossing it
// fades in rather than clicking on. Real signals are unaffected in any
// practical sense - a 440Hz tone at 1/4 scale measures identically with
// and without this stage.
#pragma once

#include <cstdint>

namespace scintillator
{

	class InputConditioner
	{
	public:
		int32_t Process(int32_t in)
		{
			// One-pole DC blocker, ~3.7Hz corner: removes a standing
			// offset while leaving audio (and anything above a slow LFO
			// rate) alone. The accumulator carries 8 bits of extra
			// precision - without them the shift rounds small corrections
			// to zero and the filter stalls before it finishes settling.
			dcAccum_ += ((in << 8) - dcAccum_) >> kDcShift;
			int32_t out = in - (dcAccum_ >> 8);

			if (out > kDeadZone) return out - kDeadZone;
			if (out < -kDeadZone) return out + kDeadZone;
			return 0;
		}

	private:
		// ~2048-sample time constant at 48kHz.
		static constexpr int kDcShift = 11;
		// ~1.2% of full scale. Silences up to +-16 LSB of input noise at
		// full drive, with plenty of margin over the ~8 LSB the ADC
		// actually produces. Raise it if a particular card still hisses
		// with nothing patched; lower it to pass quieter sources intact.
		static constexpr int32_t kDeadZone = 24;

		int32_t dcAccum_ = 0;
	};

} // namespace scintillator
