// Ouroboros — a tape loop, eating its own tail.
//
// The buffer IS the tape: a fixed ring of "cells" that one head assembly
// travels around. As the head passes each cell it reads what came around from
// the last rotation (that's the echo), then records input + feedback over it.
// There is no separate "delay time" — the delay is one rotation of the loop,
// so tape length and tape speed both set it, exactly like a real spliced reel:
//
//   Length (X) short, feedback low   -> slapback echo
//   Length long, feedback high       -> sound-on-sound / Frippertronics
//   Speed (Y) turned while running   -> the whole tape Doppler-shifts
//   Freeze (switch up)               -> record head lifts; the loop just spins
//   Reverse (switch down, momentary) -> tape runs backwards (playback only)
//
// Tape cells tick at 24kHz when Y is centred, so slow tape really is darker
// (fewer cells per second of audio), and the feedback low-pass runs at cell
// rate, so repeats darken faster on slow tape too. A fixed, gentle wow wobbles
// the playback head. Companion prototype for a physical 1/4" tape loop rig.
//
// I/O:
//   Audio In 1    input
//   Audio Out 1   wet — the playback head
//   Audio Out 2   dry — input thru (mix them yourself)
//   CV In 1       tape speed, 1V/oct, adds to Y
//   CV In 2       feedback, adds to the main knob
//   Pulse In 1    freeze gate (high = freeze)
//   Pulse In 2    reverse gate (high = reverse)
//   Pulse Out 1   splice tick — a 10ms pulse each time the splice passes
//   Pulse Out 2   high while frozen
//   CV Out 1      envelope follower of the wet output (0..+6V)
//   CV Out 2      tape position, a 0..+6V ramp once per rotation
//   Main knob     feedback, 0..~112% — the top of the knob runs away
//   Knob X        tape length, ~100ms..~4s at centre speed (square law)
//   Knob Y        tape speed, +/-1 octave varispeed (centre = normal)
//   Switch        Up = freeze, Middle = echo, Down (momentary) = reverse
//   LEDs          a dot chases round the panel once per rotation

#include "ComputerCard.h"
#include <cmath>

class Ouroboros : public ComputerCard
{
	// --- the tape ---
	// 96K cells of int16 = 192KB, most of the RP2040's RAM. At the centre-speed
	// cell rate of 24kHz that is ~4.1s of tape; an octave down, ~8s; at the CV
	// extreme (2 octaves down), ~16s at 3kHz bandwidth — proper murky tape.
	static constexpr int CELLS = 96 * 1024;
	static constexpr int LMIN  = 2400;              // ~100ms minimum loop

	// --- tape speed ---
	// The head advances `inc` cells per 48kHz tick, Q16. Centre = 0.5 (24kHz
	// cell rate). Knob gives +/-1 octave, CV extends the total to +/-2.
	static constexpr int32_t INC_CENTRE = 32768;

	// --- pitch units: 1024 = one octave (CV In +/-2048 == +/-6V, so 1V/oct = *3)
	static constexpr int32_t POCT = 1024;
	static constexpr int32_t PMAX = 2 * POCT;

	static constexpr int LUTBITS = 10;
	static constexpr int LUTSIZE = 1 << LUTBITS;

	static constexpr int CTRL_DECIM = 16;
	static constexpr int SPLICE_TICKS = 480;        // 10ms pulse at 48kHz

	// Wow: a triangle wobble on the playback head, +/-16 cells at ~0.7Hz —
	// a couple of cents of pitch drift, always on. It's a tape machine.
	static constexpr uint32_t WOW_RATE = 61;        // phase/tick, 2^22 period

	int16_t buf[CELLS];           // the tape
	int32_t pow2Tab[LUTSIZE];     // Q16: 65536 * 2^(i/1024), one octave

	int wcell;                    // the cell under the head
	int32_t frac;                 // Q16 position within that cell [0, 65536)
	int32_t L, Ltarget;           // loop length in cells (slewed)
	int32_t incQ16;               // head speed, cells per tick
	int32_t fbQ12;                // feedback, 4096 == 1.0
	int32_t fbLP;                 // one-pole low-pass state in the record path
	int32_t inNow;                // current input, seen by the record head
	uint32_t wowPh;
	bool frozen, rev;
	int spliceTimer;
	int32_t envWet;
	int ctrl;

	// Same knee as Escher: hard clipping inside a feedback loop locks into a
	// square, so the loop gets a quadratic shoulder into the +/-2048 rail.
	static int32_t __not_in_flash_func(softClip)(int32_t x)
	{
		if (x >= 3072)  return 2048;
		if (x <= -3072) return -2048;
		int32_t a = x < 0 ? -x : x;
		if (a <= 1024) return x;
		int32_t d = a - 1024;
		int32_t y = a - ((d * d) >> 12);
		return x < 0 ? -y : y;
	}

	// The record head passing over cell c: play back last rotation's content,
	// darken it, add the input, saturate, and lay it back down on the tape.
	void __not_in_flash_func(recordCell)(int c)
	{
		int32_t old = buf[c];
		fbLP += ((old - fbLP) * 3) >> 2;        // ~5kHz at 24k cell rate; slower
		                                        // tape darkens faster, like tape
		buf[c] = (int16_t)softClip(inNow + ((fbQ12 * fbLP) >> 12));
	}

	void __not_in_flash_func(updateControls)()
	{
		Switch sw = SwitchVal();
		frozen = (sw == Up)   || (Connected(Pulse1) && PulseIn(0));
		rev    = (sw == Down) || (Connected(Pulse2) && PulseIn(1));

		// ---- tape speed ----
		int32_t p = (KnobVal(Y) - 2048) >> 1;   // +/-1024 = +/-1 octave
		if (Connected(CV1)) p += CVIn(0) * 3;   // 1V/oct
		if (p > PMAX)  p = PMAX;
		if (p < -PMAX) p = -PMAX;
		int oct = p >> LUTBITS;                 // arithmetic: -2..+2
		int32_t r = pow2Tab[p & (POCT - 1)] >> 1;          // Q16 * 0.5 base
		incQ16 = (oct >= 0) ? (r << oct) : (r >> -oct);    // 8192..131072

		// ---- tape length ----
		// Square law: the bottom half of the knob covers slapback territory.
		int32_t k = KnobVal(X);
		Ltarget = LMIN + ((((k * k) >> 12) * 5990) >> 8);  // 2400..~98200
		L += (Ltarget - L) >> 5;                // ~10ms glide; the splice moves,
		if (L < LMIN) L = LMIN;                 // it doesn't teleport
		if (wcell >= L) { wcell = 0; spliceTimer = SPLICE_TICKS; }

		// ---- feedback ----
		int32_t fb = (KnobVal(Main) * 9) >> 3;  // 0..4606 (~112%)
		if (Connected(CV2)) fb += CVIn(1);
		if (fb < 0)    fb = 0;
		if (fb > 4606) fb = 4606;
		fbQ12 = fb;

		// ---- position telemetry: ramp CV and the chasing LED ----
		// Two hardware divides at 3kHz control rate — nowhere near the ISR budget.
		CVOut(1, (int16_t)((wcell << 11) / L));
		static const uint8_t ring[6] = {0, 1, 3, 5, 4, 2};  // clockwise round the panel
		int idx = ring[(wcell * 6) / L];
		int32_t bright = 512 + (envWet << 1);
		if (bright > 4095) bright = 4095;
		for (int i = 0; i < 6; i++) LedBrightness(i, i == idx ? bright : 0);
	}

public:
	Ouroboros()
	{
		for (int i = 0; i < LUTSIZE; i++)
			pow2Tab[i] = (int32_t)lroundf(65536.0f * powf(2.0f, (float)i / LUTSIZE));
		for (int i = 0; i < CELLS; i++) buf[i] = 0;
		wcell = 0;
		frac = 0;
		L = Ltarget = 24000;
		incQ16 = INC_CENTRE;
		fbQ12 = 0;
		fbLP = 0;
		inNow = 0;
		wowPh = 0;
		frozen = rev = false;
		spliceTimer = 0;
		envWet = 0;
		ctrl = 0;
	}

	virtual void __not_in_flash_func(ProcessSample)()
	{
		if (--ctrl <= 0) { ctrl = CTRL_DECIM; updateControls(); }

		inNow = Connected(Audio1) ? AudioIn(0) : 0;

		// ---- move the tape ----
		// The head crosses at most two cell boundaries per tick (inc <= 2.0).
		// Recording happens per cell crossed — each cell of tape is written
		// exactly once per rotation. Frozen or reversed, the head still moves;
		// it just doesn't record. (A real record head does nothing useful in
		// reverse either.)
		if (!rev)
		{
			frac += incQ16;
			while (frac >= 65536)
			{
				frac -= 65536;
				if (++wcell >= L) { wcell = 0; spliceTimer = SPLICE_TICKS; }
				if (!frozen) recordCell(wcell);
			}
		}
		else
		{
			frac -= incQ16;
			while (frac < 0)
			{
				frac += 65536;
				if (--wcell < 0) { wcell = L - 1; spliceTimer = SPLICE_TICKS; }
			}
		}

		// ---- the playback head ----
		// One cell ahead of the record head (i.e. the oldest content on the
		// tape, one rotation away), plus the wow wobble; linearly interpolated.
		uint32_t wt = wowPh & 0x3FFFFF;
		if (wt >= (1u << 21)) wt = (1u << 22) - wt;
		int32_t wow = (int32_t)wt - (1 << 20);       // +/-16 cells, Q16
		wowPh += WOW_RATE;

		int32_t t = frac + 65536 + wow;
		int32_t q = t >> 16;                         // may be negative; >> floors
		int32_t f = t & 0xFFFF;                      // so this stays in [0,65535]
		int i0 = wcell + q;
		while (i0 >= L) i0 -= L;
		while (i0 < 0)  i0 += L;
		int i1 = (i0 + 1 == L) ? 0 : i0 + 1;
		int32_t wet = (buf[i0] * (65536 - f) + buf[i1] * f) >> 16;

		if (wet > 2047)  wet = 2047;
		if (wet < -2048) wet = -2048;
		AudioOut(0, (int16_t)wet);
		AudioOut(1, (int16_t)inNow);

		if (spliceTimer) spliceTimer--;
		PulseOut(0, spliceTimer > 0);
		PulseOut(1, frozen);

		// Envelope of the wet out: fast attack, slow release.
		int32_t aw = wet < 0 ? -wet : wet;
		envWet += (aw > envWet) ? ((aw - envWet) >> 2) : ((aw - envWet) >> 9);
		CVOut(0, (int16_t)(envWet > 2047 ? 2047 : envWet));
	}
};

// The hardware entry point. Skipped by the host simulator (sim/), which supplies
// its own main() and a mock ComputerCard.
#ifndef COMPUTERCARD_HOST_SIM
int main()
{
	set_sys_clock_khz(192000, true);

	// static: the 192KB tape belongs in .bss, not on main()'s stack.
	static Ouroboros ouroboros;
	ouroboros.EnableNormalisationProbe();
	ouroboros.Run();
}
#endif
