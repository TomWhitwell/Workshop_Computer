#include "modes/mirror.h"
#include "pico.h"

namespace lp {

void MirrorMode::OnEnter()
{
	gateR_.on = gateB_.on = false;
	frozen_ = false;
}

void MirrorMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	frozen_ = c.downHeld;
	int32_t threshold = c.main << 4;
	out.pulse1 = gateR_.Update(f.ur, threshold);
	out.pulse2 = gateB_.Update(f.ub, threshold);
}

void __not_in_flash_func(MirrorMode::AudioTick)(const SensorFrame &f, const Inputs &, EngineOut &out)
{
	if (frozen_) return;   // out still holds the last values written
	out.cv2    = q16_to_cv5v(f.ur);
	out.audio1 = q16_to_audio5v(f.ug);
	out.audio2 = q16_to_audio5v(f.ub);
}

} // namespace lp
