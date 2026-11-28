#pragma once
#include <cstdint>
#include "Config.h"
#include "Panel.h"

namespace ab
{

// The six LEDs show the music as it plays, on the side each loop is panned
// to:
//
//   0 1   piano: a strike lights its side, then fades over about 3s
//   2 3   choir: brightness follows each note's swell and fade
//   4 5   CV Out 1 (left), CV Out 2's distance from 0V (right)
//
// A centred loop lights both LEDs of its pair at half brightness.
//
// Some moments take the LEDs over for a while:
//   - Z moved: the new mode's row (top for up, middle for middle) lights
//     fully for 0.8s.
//   - a knob waiting to take over: its mode's row pulses slowly.
//   - new piece: all six sweep top to bottom once, ~0.5s.
class Leds
{
public:
	// A piano strike. shareL/R: how much of the loop is on each side, Q12
	// (power shares: 2048 each at the centre).
	void Strike(int32_t shareL, int32_t shareR)
	{
		int32_t l = shareL << 4, r = shareR << 4; // Q16
		piano_[0] = l > piano_[0] ? l : piano_[0];
		piano_[1] = r > piano_[1] ? r : piano_[1];
	}

	void NewPiece() { sweep_ = kSweepBlocks; }

	// Once a block. choir[2]: the choir's loudness on each side, 0..4095.
	// cvMv: the two CV outs.
	void Update(const Panel &panel, const int32_t choir[2], const int32_t cvMv[2], int32_t led[6])
	{
		for (int i = 0; i < 2; i++)
			piano_[i] = (int32_t)(((int64_t)piano_[i] * kPianoLedFadeQ16) >> 16);
		phase_ = phase_ + 1 >= kWaitPulseBlocks ? 0 : phase_ + 1;

		int32_t row[3][2];
		row[0][0] = piano_[0] >> 4;
		row[0][1] = piano_[1] >> 4;
		row[1][0] = choir[0];
		row[1][1] = choir[1];
		row[2][0] = (cvMv[0] * 4095) / 5000;
		int32_t a = cvMv[1] < 0 ? -cvMv[1] : cvMv[1];
		row[2][1] = (a * 4095) / 5000;

		int r = panel.Mode() == kSwitchUp ? 0 : 1; // the current mode's row
		if (panel.Waiting())
		{
			// A slow triangle, 1.2s round.
			int32_t half = kWaitPulseBlocks / 2;
			int32_t t = phase_ < half ? phase_ : kWaitPulseBlocks - phase_;
			row[r][0] = row[r][1] = (t * 4095) / half;
		}
		if (panel.ModeFlash() > 0)
			row[r][0] = row[r][1] = 4095;
		if (sweep_ > 0)
		{
			// Top, middle, bottom, a third of the sweep each.
			int lit = 2 - (sweep_ - 1) * 3 / kSweepBlocks;
			for (int i = 0; i < 3; i++)
				row[i][0] = row[i][1] = i == lit ? 4095 : 0;
			sweep_--;
		}

		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 2; j++)
				led[i * 2 + j] = row[i][j] < 0 ? 0 : (row[i][j] > 4095 ? 4095 : row[i][j]);
	}

private:
	int32_t piano_[2] = {0, 0}; // Q16
	int32_t phase_ = 0;
	int32_t sweep_ = 0;
};

} // namespace ab
