#include "modes/tapescrub.h"
#include "pico.h"

namespace lp {

namespace {

// Green (Q16) -> read offset in Q16 samples. Stops one short of the end so the
// interpolation partner never wraps from the newest sample to the oldest.
inline int32_t HeadTarget(int32_t ug, uint32_t len)
{
	return (ug >> 8) * (static_cast<int32_t>(len) - 2)
	     + (((ug & 0xFF) * (static_cast<int32_t>(len) - 2)) >> 8);
}

} // namespace

void TapeScrubMode::OnEnter()
{
	svf_.Reset();
	armed_ = false;
}

void TapeScrubMode::OnDownPress()
{
	// Deliberately does NOT start recording. StartRecord() zeroes the write
	// head, so starting here would overwrite the front of the existing take
	// every time someone tapped to change behaviour. ControlTick starts it
	// once the press has outlived kTapTicks, by which point it is certainly
	// a record and not a tap.
	armed_ = true;
}

void TapeScrubMode::OnDownRelease(int ticks)
{
	armed_ = false;
	if (ticks < kTapTicks)
	{
		act_ = static_cast<Act>((static_cast<int>(act_) + 1) % static_cast<int>(Act::kCount));
		actChanged_ = true;
		return;
	}
	if (gTape.Recording()) gTape.StopRecord();
}

void TapeScrubMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	if (armed_ && c.downHeld && c.downTicks == kTapTicks)
	{
		gTape.StartRecord();
		out.ledFlash = 6;     // all six: the tape is rolling from here
	}

	if (actChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(static_cast<int>(act_) + 1);
		actChanged_ = false;
	}

	svf_.Set(f.ur, f.ub);
	blend_.Set(c.main);

	const uint32_t len = gTape.Length();
	if (!gTape.HasAudio() || gTape.Recording()) return;

	if (act_ == Act::Slice)
	{
		sliceLen_ = len / kSlices;
		if (sliceLen_ < 2) sliceLen_ = 2;
		// A quarter-slice of hysteresis and a 30ms dwell, so a hand resting on
		// a boundary does not machine-gun between two slices.
		int32_t band = 65536 / kSlices;
		int32_t lo = slice_ * band - band / 4;
		int32_t hi = (slice_ + 1) * band + band / 4;
		int cand = (f.ug >= lo && f.ug < hi) ? slice_
		                                     : clamp_i32(f.ug / band, 0, kSlices - 1);
		if (cand == slice_) { pending_ = -1; dwell_ = 0; }
		else if (cand != pending_) { pending_ = cand; dwell_ = 0; }
		else if (++dwell_ >= kSliceDwell)
		{
			slice_ = cand;
			pending_ = -1;
			dwell_ = 0;
		}
	}
	else if (act_ == Act::Sweep)
	{
		// Green sets the loop length: the whole take down to a sixty-fourth,
		// exponentially, so the shrink feels even all the way down.
		int32_t octQ12 = -(((65535 - f.ug) * 6 * 4096) >> 16);
		loopEnd_ = static_cast<uint32_t>(
			clamp_i32(pow2_scale(static_cast<int32_t>(len), octQ12), 256,
			          static_cast<int32_t>(len)));
	}
}

void __not_in_flash_func(TapeScrubMode::AudioTick)(const SensorFrame &f, const Inputs &in, EngineOut &out)
{
	int32_t sig;
	if (gTape.Recording())
	{
		gTape.Write(in.audio1);
		sig = in.audio1;                       // monitor what is going down
		headQ8_ = 0;
	}
	else if (gTape.HasAudio())
	{
		const uint32_t len = gTape.Length();
		if (act_ == Act::Scrub)
		{
			// Inertia on the head: Green arrives in ADC-sized steps, which
			// would click without it. ~2.7ms.
			headQ8_ = slew(headQ8_, HeadTarget(f.ug, len), 7);
			sig = gTape.Read(static_cast<uint32_t>(headQ8_ >> 8), (headQ8_ & 0xFF) << 8);
		}
		else
		{
			// Both of these free-run at normal speed; only their loop points
			// differ. Jumps go through Head, which crossfades them.
			if (act_ == Act::Slice)
			{
				uint32_t start = static_cast<uint32_t>(slice_) * sliceLen_;
				if (head_.idx < start || head_.idx >= start + sliceLen_) head_.JumpTo(start);
			}
			else if (head_.idx >= loopEnd_)
			{
				head_.JumpTo(0);
			}
			sig = gTape.ReadHead(head_, kQ16One);
		}
	}
	else
	{
		sig = in.audio1;
	}

	svf_.Process(sig);
	int16_t s = clamp12(blend_.Render(svf_));
	out.audio1 = s;
	out.audio2 = s;
}

} // namespace lp
