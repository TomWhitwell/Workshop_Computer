#include "modes/barcode.h"
#include "pico.h"

namespace lp {

namespace {

constexpr int32_t kDeadband = 60;   // knob units either side of noon = exactly 1x

int32_t BipolarSpeedQ16(int32_t knob)
{
	int32_t d = knob - 2048;
	if (d > -kDeadband && d < kDeadband) return kQ16One;
	d += (d > 0) ? -kDeadband : kDeadband;   // continuous at the dead-zone edge
	// +/-3 octaves across each half of the travel.
	return pow2_scale(kQ16One, d * 12288 / (2048 - kDeadband));
}

/// One brightness from the three gels: what a black-and-white code actually
/// varies, with three channels' worth of signal and their noise averaged
/// down. Control rate only — a fixed-point third would overflow int32 at
/// full scale, and the divider is free here.
inline int32_t Luma(const SensorFrame &f)
{
	return (f.ur + f.ug + f.ub) / 3;
}

} // namespace

void BarcodeMode::OnDownPress()
{
	recording_ = true;
	analysing_ = false;
	recLen_ = 0;
	recMin_ = 255;
	recMax_ = 0;
}

void BarcodeMode::OnDownRelease(int ticks)
{
	if (!recording_) return;
	recording_ = false;

	if (ticks < kTapTicks)
	{
		// A tap, not a swipe. The take went into the spare buffer, so dropping
		// it leaves the playing loop untouched.
		kit_ = static_cast<Kit>((static_cast<int>(kit_) + 1) % static_cast<int>(Kit::kCount));
		perc_.SetKit(kit_);
		kitChanged_ = true;
		return;
	}

	if (recLen_ < kMinLen) return;         // a slip: keep whatever was playing
	StartAnalysis();
}

void BarcodeMode::StartAnalysis()
{
	int range = recMax_ - recMin_;
	if (range < 16) return;                // no bars in it, so no take

	int mid = (recMin_ + recMax_) / 2;
	lo_ = static_cast<uint8_t>(mid - range / 8);
	hi_ = static_cast<uint8_t>(mid + range / 8);
	dark_ = Sample(Spare(), 0) < mid;
	cursor_ = 1;
	elemStart_ = 0;
	count_ = 0;
	analysing_ = true;
}

/// Cut the take into runs of black and white. Spread over control ticks:
/// one pass over 4096 samples inside a single ProcessSample would overrun
/// the 20.8us budget many times over.
void BarcodeMode::AnalyseChunk()
{
	const int take = Spare();
	uint32_t end = cursor_ + kAnalyseChunk;
	if (end > recLen_) end = recLen_;

	for (; cursor_ < end; cursor_++)
	{
		uint8_t v = Sample(take, cursor_);
		bool flip = dark_ ? (v > hi_) : (v < lo_);
		if (!flip) continue;

		uint32_t width = cursor_ - elemStart_;
		if (width < kMinElemWidth) continue;   // a flicker, not an element

		if (count_ < kMaxElems)
		{
			elems_[take][count_].start = static_cast<uint16_t>(elemStart_);
			elems_[take][count_].width = static_cast<uint16_t>(width);
			elems_[take][count_].dark  = dark_ ? 1 : 0;
			count_++;
		}
		elemStart_ = cursor_;
		dark_ = !dark_;
	}

	if (cursor_ >= recLen_) FinishAnalysis();
}

void BarcodeMode::FinishAnalysis()
{
	const int take = Spare();
	analysing_ = false;

	// The run the swipe ended in.
	if (count_ < kMaxElems && recLen_ > elemStart_)
	{
		elems_[take][count_].start = static_cast<uint16_t>(elemStart_);
		elems_[take][count_].width = static_cast<uint16_t>(recLen_ - elemStart_);
		elems_[take][count_].dark  = dark_ ? 1 : 0;
		count_++;
	}

	if (count_ < 2) return;                // not a code: keep the old take

	len_[take] = recLen_;
	elemCount_[take] = count_;
	// The elements tile the take, so the average width is just the average.
	meanWidth_[take] = static_cast<uint16_t>(recLen_ / count_);
	if (meanWidth_[take] == 0) meanWidth_[take] = 1;

	active_ = take;
	hasTake_ = true;
	pos_ = 0;
	elemIdx_ = 0;
}

void BarcodeMode::ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out)
{
	int32_t luma = Luma(f);

	if (recording_)
	{
		if (recLen_ < static_cast<uint32_t>(kMaxLen))
		{
			uint8_t v = static_cast<uint8_t>(luma >> 8);
			rec_[Spare()][recLen_++] = v;
			if (v < recMin_) recMin_ = v;
			if (v > recMax_) recMax_ = v;
		}
		// A full buffer just stops taking samples; the take is what fitted.
	}
	else if (analysing_)
	{
		AnalyseChunk();
	}

	// Keep playing until the press is definitely a record, so a tap to change
	// the voice does not put a 171ms hole in the outputs.
	if ((recording_ && recLen_ >= static_cast<uint32_t>(kTapTicks)) || !hasTake_)
	{
		// Mirror what the wand sees, so a swipe can be aimed before recording.
		// Both CV-carrying channels show brightness here: with a voice running
		// CV Out 2 reads target_[1], and while aiming there is no width to show.
		target_[0] = luma;
		target_[1] = luma;
		target_[2] = 0;
		trig_[0] = trig_[1] = 0;
	}
	else
	{
		const int take = active_;
		const uint32_t len = len_[take];

		uint32_t step = static_cast<uint32_t>(BipolarSpeedQ16(c.main));
		pos_ += step;
		if ((pos_ >> 16) >= len)
		{
			pos_ -= len << 16;
			elemIdx_ = 0;
			trig_[0] = kTrigTicks;
			if (elems_[take][0].width > meanWidth_[take]) trig_[1] = kTrigTicks;
			hitPend_ = true;
			hitDark_ = elems_[take][0].dark != 0;
		}

		uint32_t i = pos_ >> 16;
		// Advance past every element the step crossed, firing each one: at 8x
		// a narrow bar can go by in a single tick.
		while (elemIdx_ + 1 < elemCount_[take] && elems_[take][elemIdx_ + 1].start <= i)
		{
			elemIdx_++;
			trig_[0] = kTrigTicks;
			if (elems_[take][elemIdx_].width > meanWidth_[take]) trig_[1] = kTrigTicks;
			// One hit per tick, last edge wins: at 8x several narrow bars can
			// pass in one tick, and three hits 600us apart is a buzz, not a beat.
			hitPend_ = true;
			hitDark_ = elems_[take][elemIdx_].dark != 0;
		}

		const Elem &e = elems_[take][elemIdx_];
		uint32_t i1 = (i + 1 >= len) ? 0 : i + 1;
		int32_t a = Sample(take, i) * 257;
		int32_t b = Sample(take, i1) * 257;
		int32_t mu = static_cast<int32_t>((pos_ & 0xFFFF) >> 8);
		target_[0] = a + (((b - a) * mu) >> 8);
		// Average width reads half scale, so wide and narrow sit either side.
		target_[1] = clamp_i32((e.width * 32768) / meanWidth_[take], 0, 65535);
		target_[2] = e.dark ? 65535 : 0;
		curDark_ = e.dark != 0;
	}

	if (hitPend_)
	{
		perc_.Trigger(hitDark_, target_[1]);
		hitPend_ = false;
	}
	perc_.SetTone(target_[0], target_[1], curDark_);

	if (kitChanged_)
	{
		out.ledFlash = static_cast<uint8_t>(static_cast<int>(kit_) + 1);
		kitChanged_ = false;
	}

	out.pulse1 = trig_[0] > 0;
	out.pulse2 = trig_[1] > 0;
	if (trig_[0] > 0) trig_[0]--;
	if (trig_[1] > 0) trig_[1]--;
}

void __not_in_flash_func(BarcodeMode::AudioTick)(const SensorFrame &, const Inputs &, EngineOut &out)
{
	// Smooth the 1.5kHz steps out of the control-rate targets. The black/white
	// gate gets a faster ramp so its edges stay square.
	smooth_[0] = slew(smooth_[0], target_[0], 4);
	smooth_[1] = slew(smooth_[1], target_[1], 4);
	smooth_[2] = slew(smooth_[2], target_[2], 2);

	if (kit_ == Kit::Off)
	{
		out.cv2    = q16_to_cv5v(smooth_[0]);    // the scanned brightness
		out.audio1 = q16_to_audio5v(smooth_[1]); // element width
	}
	else
	{
		out.cv2    = q16_to_cv5v(smooth_[1]);    // width moves here...
		out.audio1 = clamp12(perc_.Render());    // ...so the voice can have this
	}
	out.audio2 = q16_to_audio5v(smooth_[2]);
}

} // namespace lp
