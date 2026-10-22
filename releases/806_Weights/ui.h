// Weights: the 8mu's pages, fader pickup and LEDs, with no hardware
// in it, so it can be tested on a PC (test/engine_test.cpp).  The web editor
// (web/index.html) does the same for an 8mu plugged into the computer.

#ifndef WEIGHTS_UI_H
#define WEIGHTS_UI_H

#include "weights.h"

namespace weights
{

class EightMuUi
{
public:
	// The page the faders edit (see weights.h): 0-3 the first pages (NOTES,
	// OCTAVES, GATES, RATCHETS on buttons A-D), 4-7 their second pages,
	// voice 2's, 8-11 the envelope pages (ATTACK, DECAY, PLUCK, DEPTH) and
	// 12-15 voice 2's envelope pages
	int page = Notes;

	// Control ticks (1.5kHz) left of showing the page's probabilities on the
	// LEDs, after a fader or button is touched: 3 seconds
	static constexpr int32_t kControlRate = 1500;
	static constexpr int32_t kShowTicks = 3 * kControlRate;
	int32_t showProbs = 0;

	static constexpr int32_t kFull = 4095;
	static constexpr int32_t kDim = 640;   // voice 2, on a page it shares

	// A button press: the button's first page, or, if one of its pages is
	// already showing, its next one, round its four
	static int NextPage(int pg, int button)
	{
		return ButtonOf(pg) == button ? (pg + 4) % kPages : button;
	}

	// Forget where the faders were (on connecting), so none edits anything
	// until it reaches its stored value
	void Reset()
	{
		for (int i = 0; i < kFaders; i++) latched[i] = false;
		for (int b = 0; b < 4; b++) prevButton[b] = false;
		lastValid = false;
	}

	void SetPage(int pg)
	{
		if (pg == page) return;
		page = pg;
		for (int i = 0; i < kFaders; i++) latched[i] = false;
	}

	bool Latched(int i) const {return latched[i];}

	// At control rate: the 8mu's faders (0-127) and buttons
	void Update(Engine &e, const int32_t fader[kFaders], const bool button[4])
	{
		for (int b = 0; b < 4; b++)
		{
			if (button[b] && !prevButton[b])
			{
				SetPage(NextPage(page, b));
				showProbs = kShowTicks;
			}
			prevButton[b] = button[b];
		}

		for (int i = 0; i < kFaders; i++)
		{
			int32_t f = fader[i];
			volatile uint8_t &stored = e.faders[page][i];
			// Any movement counts as a touch, picked up or not
			if (!lastValid) touchRef[i] = f;
			else if (f - touchRef[i] >= 2 || touchRef[i] - f >= 2)
			{
				touchRef[i] = f;
				showProbs = kShowTicks;
			}
			// Pickup: a fader takes over its value once it is moved to (or
			// across) the value already stored, so changing page never
			// changes the probabilities by accident
			if (!latched[i])
			{
				int32_t d = f - stored;
				bool near = d >= -3 && d <= 3;
				bool crossed = lastValid && ((last[i] - stored < 0) != (d < 0));
				if (near || crossed) latched[i] = true;
			}
			if (latched[i]) stored = uint8_t(f);
			last[i] = f;
		}
		lastValid = true;
		if (showProbs > 0) showProbs--;
	}

	// How long an LED flash lasts for a hit: an eighth of a step, but at
	// least 20ms (so the 8mu shows it) and no more than 200ms
	static int32_t FlashLength(int32_t stepLen)
	{
		int32_t f = stepLen / 8;
		if (f < 960) f = 960;
		if (f > 9600) f = 9600;
		if (f > stepLen / 6) f = stepLen / 6;
		return f;
	}

	// The 8mu's LEDs, 0-4095 each.  For 3 seconds after a touch: the page's
	// probabilities.  After that: what is playing.
	void Leds(const Engine &e, int32_t led[kFaders]) const
	{
		int kind = KindOf(page), v = VoiceOfPage(page);
		for (int i = 0; i < kFaders; i++) led[i] = 0;

		if (showProbs > 0)
		{
			// Brightness is the probability, the likeliest fully lit.  A
			// NOTES, OCTAVES, GATES or RATCHETS page all at zero makes every
			// choice equally likely.  A second page all at zero is unused
			// (voice 2 shares the first), and an envelope page all at zero
			// is off: both dark.
			uint8_t active = kind == Notes ? ActiveNoteFaders(e.CurrentScale()) : 0xFF;
			bool uniform = v == 0 && kind < kPairs;
			int32_t most = 0;
			for (int i = 0; i < kFaders; i++)
				if ((active >> i) & 1 && Weight(e.faders[page][i]) > most) most = Weight(e.faders[page][i]);
			int32_t flash12 = 12 * FlashLength(e.stepLen);
			int32_t at12 = 12 * (e.sinceStep < e.stepLen ? e.sinceStep : e.stepLen);
			for (int i = 0; i < kFaders; i++)
			{
				if (!((active >> i) & 1)) continue;
				int32_t b = most ? (Weight(e.faders[page][i]) * kFull) / most : (uniform ? kFull : 0);
				// Ratchets flash at their own speed, in time with the steps
				if (kind == Ratchets)
				{
					// Compared in twelfths of a step, to avoid dividing (on
					// this processor a division is slow); a step is under
					// 2^27 samples, so these stay within 32 bits
					int sp = SpeedOf(i);
					bool on = false;
					for (int k = 0; k < kHits[sp]; k++)
					{
						int32_t d = at12 - e.stepLen * kHitTwelfths[sp][k];
						if (d >= 0 && d < flash12) on = true;
					}
					if (!on) b = 0;
				}
				led[i] = b;
			}
			return;
		}

		// What's playing: this page's voice brightly, and on a first page
		// that voice 2 shares, voice 2 dimly as well
		if (v == 0 && e.Shared(kind)) Show(e, 1, kind, kDim, led);
		Show(e, v, kind, kFull, led);
	}

private:
	bool latched[kFaders] = {};
	int32_t last[kFaders] = {};
	int32_t touchRef[kFaders] = {};
	bool prevButton[4] = {};
	bool lastValid = false;

	static void Light(int32_t led[kFaders], int i, int32_t b)
	{
		if (i >= 0 && i < kFaders && b > led[i]) led[i] = b;
	}

	static void Show(const Engine &e, int v, int kind, int32_t b, int32_t led[kFaders])
	{
		const Engine::Voice &vc = e.voice[v];
		switch (kind)
		{
		case Notes:
			Light(led, vc.noteFader, b);
			break;
		case Octaves:
			Light(led, vc.octaveFader, b);
			break;
		case Gates:
			// A bar up to the gate length: a rest lights the first LED
			// dimly, a tied note all eight
			if (vc.gateFader == kRestFader) Light(led, 0, b == kFull ? kDim : kDim / 2);
			else for (int i = 0; i <= vc.gateFader; i++) Light(led, i, b);
			break;
		case Ratchets:
			// The ratchet in use flashes at every hit
			if (vc.sinceHit < FlashLength(e.stepLen)) Light(led, vc.ratchetFader, b);
			break;
		default:
			// The envelope's deviation (or depth) in use; dark when the
			// page is off
			Light(led, vc.dev[kind - kPairs], b);
			break;
		}
	}
};

} // namespace weights

#endif
