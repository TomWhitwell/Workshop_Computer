// OneBit — PinPulse synth for the Workshop Computer
//
// Emulates the classic Z80 / ZX-Spectrum "1-bit polyphonic music" tricks: a
// single 1-bit output is bitbanged very fast, and pseudo-polyphony is faked by
// interleaving pulse trains (PFM / "pin pulse") or by XOR-mixing square waves
// (pulse interleaving). Anything beyond a pure square comes from fast bitbanging
// plus downstream analogue physics — here the RC filtering on the jack outputs
// stands in for the ZX speaker cone's inertia.
//
// Because ComputerCard's ProcessSample() runs at 48kHz (one write/sample), each
// call runs an OVERSAMPLED inner loop of N ticks (~768kHz effective bitbang
// rate). From those N single bits we drive two output flavours so we can A/B on
// hardware:
//   * Pulse Out 1/2 = true 1-bit (last inner-tick bit)  — harsh, authentic
//   * Audio Out 1/2 = PCM density (fraction of HIGH ticks) — smoother monitor
//
//   CV In 1       : Voice 1 pitch, 1V/oct (summed with Knob X)
//   CV In 2       : Voice 2 pitch, 1V/oct (Switch Mid) — same scaling as CV In 1
//   Knob Main     : Engine select (7 bands) × note-decay time (short→long in band)
//   Knob X        : Root pitch (~C1..C6)
//   Knob Y        : (Switch Up) voice-2 interval — far CCW = solo; else
//                   unison / m3 / M3 / P5 / dom7 / octave above the root.
//                   (Switch Mid) per-engine timbre (Phaser detune / Savage skew).
//   Audio In 1    : Duty / pulse-width mod; also latched at each Pulse In 2 edge
//                   to pick the drum (low = kick … high = snare)
//   Audio In 2    : Duty / timbre mod CV (bipolar)
//   Switch Up     : Duophonic — voice 2 = CV In 1 root + Knob Y interval
//   Switch Mid    : Duophonic — voice 2 = CV In 2 (own 1V/oct); Knob Y = timbre
//   Switch Down   : Momentary — tap cycles the drum kit (Click/Tritone/PCM/Synth)
//   Pulse In 1    : Note gate — rising edge triggers; held high = sustain at
//                   full; falling edge releases into the decay (Knob Main sets time)
//   Pulse In 2    : Drum trigger — fires a drum (chosen by Audio In 1) on the drum lane
//   Pulse Out 1   : 1-bit tone engine output
//   Pulse Out 2   : 1-bit drum lane output
//   Audio Out 1   : PCM-density render of the tone lane (monitor)
//   Audio Out 2   : PCM-density render of the drum lane (monitor)
//
// Reference: utz, "How to Write a 1-Bit Music Routine" (1-bit forum);
// utz82/ZX-Spectrum-1-Bit-Routines; Troise, 1-bit music thesis. See README.md.

// ComputerCard's implementation is emitted once, in main.cpp. Here we only need
// the declarations, so suppress the impl to avoid duplicate definitions.
#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include "hardware/clocks.h"   // set_sys_clock_khz
#include "drum_samples.h" // PCM drum bank extracted from Shiru's Phaser1 drums
#include "drum_synth.h"   // MusicBox kick envelope + Savage LFSR noise seeds

// ---------------------------------------------------------------------------
// V/oct lookup table — 341 entries spanning one octave (Q-format phase
// increments for a 32-bit accumulator at 48kHz). Source: Utility Pair by
// Chris Johnson, as used across these cards. ExpVoct/SemitoneInc are the same
// tuning backbone as 91_chorgan so OneBit tracks in the same reference frame.
static const int32_t voct_vals[341] = {
	314964268, 315605144, 316247323, 316890810, 317535606, 318181713, 318829136,
	319477876, 320127936, 320779318, 321432026, 322086062, 322741429, 323398129,
	324056166, 324715542, 325376259, 326038320, 326701729, 327366488, 328032599,
	328700066, 329368890, 330039076, 330710625, 331383541, 332057826, 332733483,
	333410515, 334088924, 334768714, 335449887, 336132446, 336816394, 337501733,
	338188467, 338876599, 339566130, 340257065, 340949405, 341643155, 342338315,
	343034891, 343732883, 344432296, 345133132, 345835394, 346539085, 347244208,
	347950766, 348658761, 349368197, 350079076, 350791402, 351505177, 352220405,
	352937088, 353655229, 354374831, 355095898, 355818432, 356542436, 357267913,
	357994867, 358723299, 359453214, 360184614, 360917502, 361651881, 362387755,
	363125126, 363863998, 364604372, 365346254, 366089644, 366834548, 367580967,
	368328905, 369078365, 369829350, 370581862, 371335907, 372091485, 372848601,
	373607257, 374367457, 375129204, 375892500, 376657350, 377423757, 378191722,
	378961250, 379732344, 380505008, 381279243, 382055053, 382832443, 383611414,
	384391970, 385174114, 385957850, 386743180, 387530108, 388318638, 389108772,
	389900514, 390693867, 391488834, 392285418, 393083624, 393883453, 394684911,
	395487998, 396292720, 397099080, 397907080, 398716724, 399528016, 400340958,
	401155555, 401971809, 402789724, 403609303, 404430550, 405253468, 406078060,
	406904330, 407732282, 408561918, 409393242, 410226258, 411060969, 411897378,
	412735489, 413575305, 414416830, 415260068, 416105021, 416951694, 417800089,
	418650211, 419502062, 420355647, 421210969, 422068031, 422926837, 423787390,
	424649694, 425513753, 426379570, 427247149, 428116493, 428987606, 429860492,
	430735153, 431611595, 432489820, 433369831, 434251634, 435135230, 436020625,
	436907821, 437796822, 438687632, 439580255, 440474694, 441370953, 442269035,
	443168945, 444070686, 444974262, 445879677, 446786934, 447696036, 448606989,
	449519795, 450434459, 451350984, 452269373, 453189631, 454111762, 455035769,
	455961656, 456889428, 457819087, 458750637, 459684083, 460619429, 461556677,
	462495833, 463436900, 464379881, 465324781, 466271604, 467220353, 468171033,
	469123648, 470078200, 471034695, 471993136, 472953528, 473915873, 474880177,
	475846443, 476814674, 477784876, 478757053, 479731207, 480707343, 481685466,
	482665579, 483647686, 484631791, 485617899, 486606014, 487596139, 488588278,
	489582437, 490578618, 491576826, 492577066, 493579340, 494583654, 495590012,
	496598417, 497608874, 498621387, 499635961, 500652599, 501671305, 502692084,
	503714940, 504739878, 505766901, 506796014, 507827220, 508860525, 509895933,
	510933447, 511973073, 513014813, 514058674, 515104658, 516152771, 517203017,
	518255399, 519309923, 520366592, 521425412, 522486386, 523549519, 524614815,
	525682278, 526751914, 527823726, 528897719, 529973898, 531052266, 532132828,
	533215589, 534300553, 535387725, 536477109, 537568709, 538662531, 539758579,
	540856856, 541957368, 543060120, 544165115, 545272359, 546381856, 547493610,
	548607627, 549723910, 550842464, 551963295, 553086406, 554211802, 555339489,
	556469470, 557601750, 558736334, 559873227, 561012433, 562153957, 563297803,
	564443977, 565592484, 566743327, 567896512, 569052043, 570209926, 571370165,
	572532764, 573697729, 574865065, 576034775, 577206866, 578381342, 579558207,
	580737467, 581919127, 583103191, 584289664, 585478552, 586669858, 587863589,
	589059748, 590258342, 591459374, 592662850, 593868775, 595077154, 596287991,
	597501292, 598717062, 599935306, 601156029, 602379235, 603604930, 604833120,
	606063808, 607297001, 608532703, 609770919, 611011654, 612254915, 613500705,
	614749029, 615999894, 617253304, 618509265, 619767781, 621028858, 622292501,
	623558715, 624827505, 626098877, 627372836, 628649388
};

static int32_t ExpVoct(int32_t in)
{
	if (in > 4091) in = 4091;
	if (in < 0)    in = 0;
	int32_t oct = in / 341;
	int32_t sub = in % 341;
	return voct_vals[sub] >> (12 - oct);
}

// Semitone ratio table: Q16 fixed-point multipliers for 0..12 semitones.
// ratio[n] = 2^(n/12) * 65536
static const int32_t semitone_ratio_q16[13] = {
	65536,   // 0  unison
	69432,   // 1  min 2nd
	73561,   // 2  maj 2nd
	77936,   // 3  min 3rd
	82570,   // 4  maj 3rd
	87480,   // 5  perfect 4th
	92681,   // 6  tritone
	98193,   // 7  perfect 5th
	104032,  // 8  min 6th
	110218,  // 9  maj 6th
	116768,  // 10 min 7th
	123714,  // 11 maj 7th
	131072,  // 12 octave
};

// Scale root_inc by n semitones (0..24).
static int32_t SemitoneInc(int32_t root_inc, int n)
{
	if (n <= 0)  return root_inc;
	if (n <= 12) return int32_t((int64_t(root_inc) * semitone_ratio_q16[n]) >> 16);
	int32_t octave_up = root_inc << 1; // n=12 = ×2
	int rem = n - 12;
	return int32_t((int64_t(octave_up) * semitone_ratio_q16[rem]) >> 16);
}

// ---------------------------------------------------------------------------

// Tritone drum kit: 24 drums × 2 params, from Shiru's DRUM_SETTINGS table.
// Entries 0..13 are tone drums (step, xor), 14..23 are noise drums.
static const uint8_t kTritoneDrums[24][2] = {
	{0x01,0x01},{0x01,0x02},{0x01,0x04},{0x01,0x08},{0x01,0x20},{0x20,0x04},
	{0x40,0x04},{0x40,0x08},{0x04,0x80},{0x08,0x80},{0x10,0x80},{0x10,0x02},
	{0x20,0x02},{0x40,0x02},{0x16,0x01},{0x16,0x02},{0x16,0x04},{0x16,0x08},
	{0x16,0x10},{0x00,0x01},{0x00,0x02},{0x00,0x04},{0x00,0x08},{0x00,0x10},
};

class OneBit : public ComputerCard
{
public:
	OneBit()
	{
		for (int v = 0; v < kVoices; v++)
		{
			phase[v]  = uint32_t(v) * (0xFFFFFFFFu / kVoices);
			phaseB[v] = 0;
			inc[v]    = 0;
			incB[v]   = 0;
		}
		noiseReg     = 0x2174u; // utz's canonical noise seed (Part 11)
		cv1Smoothed  = 0;
		cv2Smoothed  = 0;
		ai1Smoothed  = 0;
		ai2Smoothed  = 0;
		sampleCount  = 0;

		drumActive = false;
		drumIdx    = 0;
		drumCtr    = 0;
		drumPhase  = 0;
		drumAcc    = 0;
		drumSampPos= 0;
		drumState  = false;
		drumPitch  = 0;

		kitIdx      = 0;      // current drum kit (cycled by switch tap)
		lastSwitch  = Middle;

		skewReg    = 0;       // Savage skew (duty XOR wander) state

		noteEnv    = 0;   // Qchan-style per-note decay (0 = silent)
	}

	// Tone engine, selected by Knob Main. A plain ROM-BEEP square plus faithful
	// ports of six Beepola beeper engines (kept duophonic — 2 voices — for a
	// modular voice, not the originals' 3–4 channels).
	//   Beep     : the plain ROM `BEEP` square — one hard 50% square per voice,
	//              no duty tricks. The clean reference tone.
	//   PlipPlop : Joffa Smith — twin nested down-counters, one osc two edges
	//   Tritone  : Shiru — pulse-interleave + per-voice duty + AND-mask volume
	//   Qchan    : Shiru — pin-pulse OR-mix + per-voice AND-mask volume + decay
	//   Phaser   : Shiru — two oscillators per voice XOR'd (Multiple/Detune/Phase)
	//   Savage   : Jason Brooke — pulse-interleave + Skew/XOR duty-mod timbre
	//   MusicBox : Mark Alexander — 2ch interleaved squares
	enum Engine { EngBeep, EngPlipPlop, EngTritone, EngQchan, EngPhaser,
	              EngSavage, EngMusicBox, EngCount };

	// Drum kit, cycled by a momentary Switch tap.
	//   Click   : PlipPlop/SpecialFX swept clicks
	//   Tritone : Shiru's 24-entry tone/noise kit
	//   PCM     : Phaser digital drum samples
	//   Synth   : synthesized drums (MusicBox kick+wave, MusicStudio drop,
	//             Savage noise, Huby slide) — laid out kick→snare across CV2
	enum Kit { KitClick, KitTritone, KitPCM, KitSynth, KitCount };

	// Number of bitbang ticks per 48kHz sample. ~768kHz effective rate.
	static constexpr int kN      = 16;
	static constexpr int kVoices = 2; // duophonic

	// Drum burst length in inner ticks. ~3000 ticks ≈ a short click/hit.
	static constexpr uint32_t kDrumTicks = 3000;

	// Qchan-style note decay: noteEnv is Q12 (0..4095), decays by env>>shift,
	// where the shift (decay time) is set by the Knob Main in-band position.

	// PCM drum playback step (Q16 samples-per-tick). The 1024-sample drums play
	// back over ~kDrumTicks ticks; kPcmStep sets the pitch/speed. Tune on HW.
	static constexpr uint32_t kPcmStep = 22000; // ~1024 samples over the burst

	// Number of drums available in each kit (Audio In 1 selects across this
	// range at the trigger instant, low = kick / first, high = snare / last).
	static int drumsInKit(Kit k)
	{
		switch (k)
		{
		case KitClick:   return 4;   // TOM1, TOM2, BASS, SNARE
		case KitTritone: return 24;  // Shiru's DRUM_SETTINGS
		case KitPCM:     return 8;   // Phaser digital samples
		default:         return 5;   // Synth: kick, drop, slide, noise, snare
		}
	}

	void __not_in_flash_func(ProcessSample)() override
	{
		// --- Smooth the CV-ish inputs (τ ≈ 64 samples) ---
		cv1Smoothed += (CVIn1()    - cv1Smoothed) >> 6;
		cv2Smoothed += (CVIn2()    - cv2Smoothed) >> 6;
		ai1Smoothed += (AudioIn1() - ai1Smoothed) >> 6;
		ai2Smoothed += (AudioIn2() - ai2Smoothed) >> 6;

		Switch sw = SwitchVal();

		// --- Voice 1 pitch: Knob X + CV In 1 (1V/oct) ---------------------
		// CV/voct convention (shared with the other cards): CV counts add 1:1
		// to the voct domain, where 341 counts = one octave (ExpVoct).
		static constexpr int32_t kPitchBase  = 900;  // ~C1-ish floor
		static constexpr int32_t kPitchRange = 2400; // ~6 octaves across knob
		int32_t voct1 = kPitchBase
		              + ((KnobVal(Knob::X) * kPitchRange) >> 12)
		              + cv1Smoothed;             // CV In 1 = 1V/oct
		if (voct1 < 0)    voct1 = 0;
		if (voct1 > 4095) voct1 = 4095;
		int32_t root_inc = ExpVoct(voct1);

		// --- Engine select + decay time (Knob Main) -----------------------
		// The knob is split into seven engine bands; within each band it sweeps
		// the note-decay time. The curve stays short at the band start then
		// climbs to long quickly (squared band position), so most of the
		// travel gives longer tails.
		int32_t km = KnobVal(Knob::Main);
		Engine engine = Engine((km * EngCount) >> 12);
		if (engine >= EngCount) engine = Engine(EngCount - 1);
		int32_t bandPos = (km * EngCount) - (int32_t(engine) << 12); // 0..4095 in-band
		// sqrt curve: leaves the short zone fast, so most of the band travel is
		// long decay. Newton isqrt of bandPos*16 → 0..255 (bounded iterations).
		int32_t x = bandPos * 16;
		int32_t root = x > 0 ? 256 : 0;
		for (int it = 0; it < 6 && root > 0; it++)
			root = (root + x / root) >> 1;
		// Decay shift 7 (short) .. 17 (long). Each +1 ~doubles the tail, so the
		// long end is ~4× longer than the old max of 15 (~680ms → ~2.7s).
		int decayShift = 7 + ((root * 10) >> 8);
		if (decayShift > 17) decayShift = 17;

		// --- Drum kit: momentary Switch DOWN tap cycles the kit -----------
		// Down is the spring-loaded position: each tap advances the kit. Up and
		// Middle are the stable mono/duo positions (below).
		if (sw == Switch::Down && lastSwitch != Switch::Down)
			kitIdx = (kitIdx + 1) % KitCount;
		lastSwitch = sw;
		Kit kit = Kit(kitIdx);

		// --- Voice 2 pitch: two duophonic flavours set by the Switch ------
		//   Up   : voice 2 = root (CV In 1 + Knob X) + Knob Y quantised interval.
		//          Both voices track CV In 1; Knob Y far-CCW = solo (voice 2 off).
		//   Mid  : voice 2 = CV In 2 as its own 1V/oct pitch (same scaling as
		//          CV In 1). Knob Y becomes a per-engine timbre param (timbreY).
		//   Down : momentary (kit cycle) — behaves like Up so voice 2 doesn't drop.
		static const int kIntervals[6] = { 0, 3, 4, 7, 10, 12 }; // uni,m3,M3,P5,dom7,oct
		int32_t ky = KnobVal(Knob::Y);
		int32_t timbreY = 0;      // Knob Y as a timbre parameter (Mid position)
		bool voice2On = true;
		inc[0] = root_inc;

		if (sw == Switch::Middle)
		{
			// Voice 2 from CV In 2 (own 1V/oct, Knob X base + CV In 2), scaled
			// identically to CV In 1 — CV In 1 and CV In 2 are the two note CVs.
			int32_t voct2 = kPitchBase
			              + ((KnobVal(Knob::X) * kPitchRange) >> 12)
			              + cv2Smoothed;
			if (voct2 < 0)    voct2 = 0;
			if (voct2 > 4095) voct2 = 4095;
			inc[1] = ExpVoct(voct2);
			timbreY = ky;         // Knob Y drives a per-engine timbre param here
		}
		else
		{
			// Up / Down: voice 2 = root + Knob Y quantised interval (CCW = solo).
			voice2On = (ky >= 341);
			if (!voice2On)
				inc[1] = 0;
			else
			{
				int32_t sel = ((ky - 341) * 6) / (4096 - 341); // 0..5
				if (sel < 0) sel = 0;
				if (sel > 5) sel = 5;
				inc[1] = SemitoneInc(root_inc, kIntervals[sel]);
			}
		}
		// Active voice count — engines loop over this so solo is truly 1 voice.
		const int nActive = voice2On ? 2 : 1;

		// --- Note trigger + Qchan-style decay -----------------------------
		// Pulse In 1 is a gate: the rising edge (re)triggers the envelope to
		// full; while the gate is HELD HIGH the envelope sustains at full; on
		// the falling edge (and after) it decays and thins the pulse width (a
		// 1-bit volume decay). Decay time is set by Knob Main.
		if (PulseIn1RisingEdge())
		{
			noteEnv = 4095;
			// PlipPlop: seed osc2 half a wrap ahead for the twin-edge offset.
			phase[0]  = 0;
			phaseB[0] = 0x80000000u;
		}
		if (PulseIn1())
		{
			noteEnv = 4095;        // gate held → sustain at full
		}
		else
		{
			noteEnv -= (noteEnv >> decayShift) + 1; // released → decay
			if (noteEnv < 0) noteEnv = 0;
		}

		// --- Duty / pulse width: base + Audio In 1 + Audio In 2, ×envelope ---
		int32_t dutyBase = 128                 // 50%-ish base
		                 + (ai1Smoothed >> 4)  // Audio In 1 duty mod
		                 + (ai2Smoothed >> 4); // Audio In 2 duty mod
		if (dutyBase < 8)   dutyBase = 8;
		if (dutyBase > 250) dutyBase = 250;
		int32_t duty = (dutyBase * noteEnv) >> 12; // envelope thins the pulse
		if (duty < 1) duty = 1;
		uint32_t dutyHi = uint32_t(duty);

		// Savage SKEW (Jason Brooke): the original XOR-wanders each channel's
		// PERIOD by a small amount each note-tick, gritting the tone while it
		// stays PITCHED. We apply it as a small PROPORTIONAL perturbation of the
		// phase increment (so the base pitch still tracks CV/Knob), toggling sign
		// at a musical rate. `savageBits` is a mask on the LOW bits of inc that
		// gets XOR-toggled — bounded so it grits but never swamps the pitch.
		// Depth (0..~7) sets how many low bits wander; Knob Y scales it in Mid.
		uint32_t savageMask = 0;
		bool savageOn = false;
		if (engine == EngSavage)
		{
			skewReg += 1;
			int32_t depth = 3 + (dutyBase >> 8) + (timbreY >> 9); // 3..~8 bits
			if (depth > 10) depth = 10;
			savageMask = (1u << depth) - 1;          // low-bit wander mask
			savageOn = (skewReg & 0x40) != 0;        // toggle at a musical rate
		}

		// Qchan volume mask (per voice): the envelope acts as a 0..16 AND-mask
		// "volume" like the VOLn self-mod bytes in the original.
		uint32_t qVol = uint32_t((noteEnv * 16) >> 12); // 0..16

		// Phaser: second oscillator per voice, detuned; the detune amount is the
		// timbre. In Switch-Mid, Knob Y (timbreY) widens the detune spread.
		for (int v = 0; v < kVoices; v++)
		{
			int32_t det = (inc[v] >> 9)
			            + (int32_t(uint32_t(dutyBase)) << 6)
			            + ((inc[v] >> 12) * (timbreY >> 6)); // + Knob Y (Mid)
			incB[v] = uint32_t(inc[v] + det);
		}

		// --- Drum lane: trigger on Pulse In 2, drum picked by Audio In 1 --
		// Audio In 1 is latched at the trigger instant to choose the drum, so it
		// keeps modulating the tone (duty) the rest of the time. Layout: low =
		// the first drum in the kit (the kick), high = the last (the snare),
		// mid accesses the voices in between. Just gating PU2 (nothing patched
		// to Audio In 1, ~0V) reliably gives the kick.
		if (PulseIn2RisingEdge())
		{
			int nDrums = drumsInKit(kit);
			int32_t sel = ai1Smoothed + 2048;        // 0..4095 (unipolar-ish)
			if (sel < 0)    sel = 0;
			if (sel > 4095) sel = 4095;
			drumIdx = (sel * nDrums) >> 12;
			if (drumIdx >= nDrums) drumIdx = nDrums - 1;
			drumActive = true;
			drumCtr    = kDrumTicks;
			drumPhase  = 0;
			drumAcc    = 0;
			drumSampPos= 0;
			drumState  = false;
			// Synth-drum oscillator: seed a starting period from the kick
			// envelope / pitch so kick booms and higher indices click/snare.
			drumPitch  = 0x02000000u + uint32_t(drumIdx) * 0x01000000u;
		}

		// --- Inner bitbang loop -------------------------------------------
		int sumA = 0, sumB = 0;
		bool bitA = false, bitB = false;

		for (int i = 0; i < kN; i++)
		{
			// Advance the shared noise PRNG once per tick (32-bit LFSR).
			noiseReg = (noiseReg >> 1) ^ (uint32_t(-(int32_t(noiseReg & 1u))) & 0xD0000001u);

			// ---- Tone lane (lane A) ----
			bool toneBit = false;
			switch (engine)
			{
			case EngBeep:
			{
				// Plain ROM BEEP: a hard 50% square per voice (MSB of the
				// accumulator), OR-mixed. No duty tricks — the clean reference
				// tone. The note envelope (qVol) gates it so decay/gate still
				// works, but the shape stays a pure square.
				bool out = false;
				for (int v = 0; v < nActive; v++)
				{
					phase[v] += uint32_t(inc[v]);
					if ((phase[v] >> 31) & 1u) out = true; // 50% square
				}
				toneBit = out && (qVol != 0);
				break;
			}
			case EngPlipPlop:
			{
				// Joffa Smith BUZZ: one oscillator, two edges. osc2 (phaseB) is
				// seeded half a wrap ahead of osc1; XOR of the two duty-squares
				// gives the twin-edge "plip-plop" self-phasing shape.
				phase[0]  += uint32_t(inc[0]);
				phaseB[0] += uint32_t(inc[0]);
				bool a = (phase[0]  >> 24) < dutyHi;
				bool b = (phaseB[0] >> 24) < dutyHi;
				toneBit = a ^ b;
				break;
			}
			case EngTritone:
			{
				// Shiru Tritone: per-voice duty-compare square (LD A,H; CP duty;
				// SBC A,A) interleaved across voices, gated by the volume mask.
				bool out = false;
				for (int v = 0; v < nActive; v++)
				{
					phase[v] += uint32_t(inc[v]);
					bool sq = (phase[v] >> 24) < dutyHi;
					if (sq && qVol) out = !out; // interleave (XOR) the squares
				}
				toneBit = out;
				break;
			}
			case EngQchan:
			{
				// Shiru Qchan: pin-pulse per voice OR-mixed, each AND-masked by
				// the per-voice volume (SBC A,A; AND volN; OR). qVol==0 => silent.
				uint32_t mix = 0;
				for (int v = 0; v < nActive; v++)
				{
					phase[v] += uint32_t(inc[v]);
					bool pin = (phase[v] >> 24) < dutyHi;
					if (pin && qVol) mix = 1;
				}
				toneBit = (mix != 0);
				break;
			}
			case EngPhaser:
			{
				// Shiru Phaser1: two oscillators per voice, each toggling on
				// carry, XOR'd; osc2 detuned (incB) and phase-offset = timbre.
				uint32_t mix = 0;
				for (int v = 0; v < nActive; v++)
				{
					phase[v]  += uint32_t(inc[v]);
					phaseB[v] += incB[v];
					bool a = (phase[v]  >> 24) < dutyHi;
					bool b = (phaseB[v] >> 24) < dutyHi;
					mix ^= (a ^ b) ? 1u : 0u;
				}
				toneBit = (mix & 1u);
				break;
			}
			case EngSavage:
			{
				// Jason Brooke's Savage: pulse-interleave with SKEW — each voice's
				// increment is perturbed on its LOW bits only (savageMask), so the
				// tone grits/wobbles while the base PITCH (high bits) still tracks
				// CV/Knob. Two voices interleaved (XOR).
				bool out = false;
				for (int v = 0; v < nActive; v++)
				{
					uint32_t sInc = uint32_t(inc[v]);
					if (savageOn) sInc ^= savageMask;   // wander only the low bits
					phase[v] += sInc;
					bool sq = (phase[v] >> 24) < dutyHi;
					if (sq) out = !out;
				}
				toneBit = out;
				break;
			}
			default: // EngMusicBox
			{
				// Mark Alexander's Music Box: two straight interleaved squares
				// (LD A,H; toggle on wrap), OR-mixed — clean, bright two-voice.
				bool out = false;
				for (int v = 0; v < nActive; v++)
				{
					phase[v] += uint32_t(inc[v]);
					if ((phase[v] >> 24) < dutyHi) out = true;
				}
				toneBit = out;
				break;
			}
			}

			bitA = toneBit;
			sumA += bitA ? 1 : 0;

			// ---- Drum lane (lane B) ----
			bool drumBit = false;
			if (drumActive)
			{
				if (kit == KitSynth)
				{
					// Synthesized drums harvested from MusicBox / MusicStudio /
					// Savage / Huby. drumIdx 0..4: kick / drop / slide / noise /
					// snare. drumPitch is a phase accumulator; its increment is
					// swept over the burst to give each its pitch envelope.
					uint32_t frac = (kDrumTicks - drumCtr); // 0..kDrumTicks progress
					switch (drumIdx)
					{
					case 0: // KICK — MusicBox rising-period boom (pitch falls)
					{
						int ei = int((frac * 15) / kDrumTicks); if (ei > 14) ei = 14;
						uint32_t per = uint32_t(kKickEnvelope[ei]);
						drumPitch += 0x00A00000u / per; // slower as period grows
						drumBit = (drumPitch >> 31) & 1u;
						break;
					}
					case 1: // DROP — MusicStudio fast falling pitch
						drumPitch += 0x06000000u - (frac * 0x00000C00u);
						drumBit = (drumPitch >> 31) & 1u;
						break;
					case 2: // SLIDE — Huby rising pitch (period ramps down)
						drumPitch += 0x01000000u + (frac * 0x00000C00u);
						drumBit = (drumPitch >> 31) & 1u;
						break;
					case 3: // NOISE — Savage LFSR colour
						drumBit = (noiseReg & kSavageNoise[0][1]) != 0;
						break;
					default: // SNARE — noise + a tone body (Savage-ish)
						drumPitch += 0x05000000u;
						drumBit = ((drumPitch >> 31) & 1u) ^ (noiseReg & 1u);
						break;
					}
				}
				else if (kit == KitPCM)
				{
					// Phaser digital drums: 8 drums × 1024 1-bit samples,
					// bit-striped. Step through the 1024 samples over the burst.
					drumAcc += kPcmStep;
					if (drumAcc >> 16) { drumAcc &= 0xFFFF; drumSampPos++; }
					if (drumSampPos >= 1024) { drumActive = false; }
					else
						drumBit = (kDrumSamples[drumSampPos] >> drumIdx) & 1u;
				}
				else if (kit == KitTritone)
				{
					// Shiru Tritone kit: idx<14 = tone (step/xor), else noise.
					const uint8_t* d = kTritoneDrums[drumIdx];
					if (drumIdx < 14)
					{
						if (drumPhase == 0) { drumState = !drumState; drumPhase = 1u + d[0]; }
						else drumPhase--;
						drumBit = drumState;
					}
					else
					{
						drumBit = (noiseReg & d[1]) != 0; // masked noise
					}
				}
				else // KitClick — PlipPlop/SpecialFX swept clicks
				{
					// 0=TOM1 rising, 1=TOM2 falling, 2=BASS descending, 3=SNARE noise
					if (drumIdx == 3)
						drumBit = (noiseReg & 1u);
					else
					{
						if (drumPhase == 0)
						{
							drumState = !drumState;
							uint32_t base = 4 + (drumCtr >> 3);
							drumPhase = (drumIdx == 0) ? (4 + ((kDrumTicks - drumCtr) >> 4))
							          : (drumIdx == 1) ? base
							          :                  (base << 1);
						}
						else drumPhase--;
						drumBit = drumState;
					}
				}
				if (drumCtr) drumCtr--;
				else drumActive = false;
			}
			bitB = drumBit;
			sumB += bitB ? 1 : 0;
		}

		// --- Outputs ------------------------------------------------------
		// True 1-bit on the pulse jacks (relies on downstream RC physics):
		//   Pulse Out 1 = tone engine, Pulse Out 2 = drum lane.
		PulseOut1(bitA);
		PulseOut2(bitB);

		// PCM density on the audio jacks (smoother monitor of each lane).
		int32_t densA = (sumA * 4095) / kN - 2048; // -2048..2047
		int32_t densB = (sumB * 4095) / kN - 2048;
		AudioOut1(int16_t(densA));
		AudioOut2(int16_t(densB));

		// --- LEDs: engine (left column bar 0..5) + decay/kit/drum --------
		if ((sampleCount++ & 0x1FF) == 0)
		{
			// Left column = engine number in BINARY (0..6):
			//   LED 0 = bit 0, LED 2 = bit 1, LED 4 = bit 2.
			int e = int(engine);
			LedOn(0, e & 1);
			LedOn(2, e & 2);
			LedOn(4, e & 4);
			// Right column: note-decay glow (1), drum kit (3), drum activity (5).
			LedBrightness(1, uint16_t(noteEnv));
			LedBrightness(3, uint16_t(kitIdx * 1200));
			LedBrightness(5, drumActive ? 4095 : 0);
		}

		// TODO (iterate on hardware, citing utz tutorial parts):
		//  - SID sweep / Earth Shaker / Duty Modulation (Part 11).
		//  - Wavetable / PCM via accu-MSB → sample-pointer (Part 10/12).
		//  - Richer drum bank / ROM-style PWM samples once sources arrive.
	}

private:
	uint32_t phase[kVoices];   // primary accumulators (all engines)
	uint32_t phaseB[kVoices];  // second accumulators (Phaser engine)
	int32_t  inc[kVoices];
	uint32_t incB[kVoices];    // Phaser second-oscillator increments (detuned)
	uint32_t noiseReg;

	// Drum lane (own output; three kits selected by the Switch).
	bool     drumActive;
	int      drumIdx;    // drum index within the current kit
	uint32_t drumCtr;    // remaining ticks in the burst
	uint32_t drumPhase;  // swept half-period counter (click / tritone-tone)
	uint32_t drumAcc;    // PCM playback accumulator (Q16)
	int      drumSampPos;// PCM sample index (0..1023)
	bool     drumState;  // current drum output bit
	uint32_t drumPitch;  // synth-drum oscillator period (kick/drop/slide)

	int      kitIdx;     // current drum kit (cycled by momentary switch tap)
	Switch   lastSwitch; // for edge-detecting the switch tap

	uint32_t skewReg;    // Savage skew: XOR-wandering duty modulation state

	int32_t  noteEnv;    // Qchan-style per-note decay, Q12 (0..4095)

	int32_t cv1Smoothed, cv2Smoothed;
	int32_t ai1Smoothed, ai2Smoothed;
	uint32_t sampleCount;
};

// Entry point for the ZX card's alternate boot mode. Called from main.cpp when
// the momentary switch is held at power-on. Sets OneBit's 144MHz clock and runs
// (blocking). The ZX path uses a different clock, so the dispatcher sets it here.
void run_onebit()
{
	set_sys_clock_khz(144000, true);
	static OneBit oneBit;   // constructed only when this mode is chosen
	oneBit.Run();
}
