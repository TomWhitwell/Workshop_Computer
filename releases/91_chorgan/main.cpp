// chordseq — 6-voice morphing chord synthesizer for the Workshop Computer
//
//   CV In 1       : Root pitch V/oct (summed with Knob X)
//   CV In 2       : Timbre offset — bipolar, offsets Main knob position
//   Knob X        : Root pitch (C3–C6)
//   Knob Y        : First interval above root — 13 steps, unison to octave
//   Main Knob     : Oscillator shape — V-curve: centre=saw, 9/3=sine, edges=pulse
//   Switch Up     : Detune × 2 (12 or 18 cents depending on knob side)
//   Switch Mid    : Detune normal (0 or 6 cents depending on knob side)
//   Switch Down   : Tap = advance chord extension preset
//   Pulse In 1    : Rising edge = advance chord extension preset
//   Audio Out 1   : 6-voice mix (left)
//   Audio Out 2   : 6-voice mix with per-voice phase offset (right)
//   Pulse Out 1   : Square wave one octave below root
//   Pulse Out 2   : PWM square at root freq, duty animated by detune LFO
//   CV Out 1      : Root + interval pitch in 1V/oct (MIDI note)
//   CV Out 2      : Triangle LFO (same rate as Pulse Out 2); 0V at zero detune

#include "ComputerCard.h"

// ---------------------------------------------------------------------------
// V/oct lookup table — 341 entries spanning one octave.
// Source: Utility Pair by Chris Johnson.
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

// ---------------------------------------------------------------------------
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
	// n 13..24: chain two lookups
	int32_t octave_up = root_inc << 1; // n=12 = ×2
	int rem = n - 12;
	return int32_t((int64_t(octave_up) * semitone_ratio_q16[rem]) >> 16);
}

// ---------------------------------------------------------------------------
// Chord extension preset table.
// extensions[y][preset][voice] — semitone offsets from root for voices 2..5.
// -128 = silent voice.
// y = Y-knob semitone (0..12), preset = 0..5, voice = 0..3
static const int8_t extensions[13][6][4] = {
	// Y=0: unison — only 5ths and octaves; no 3rds
	{ {7,12,-128,-128}, {7,19,-128,-128}, {12,19,-128,-128}, {7,12,19,-128}, {12,24,-128,-128}, {7,12,24,-128} },
	// Y=1: min 2nd — ambiguous clusters; 2nds, 7ths, tritones; no 3rds
	{ {1,6,12,-128}, {6,12,13,-128},  {1,7,13,-128},  {6,13,18,-128}, {1,12,18,-128}, {7,12,13,-128} },
	// Y=2: maj 2nd — sus2 world; 2nds, 4ths, 5ths, 9ths; no 3rds
	{ {7,12,-128,-128}, {5,7,12,-128},  {7,14,-128,-128}, {5,12,14,-128}, {7,12,14,-128}, {2,7,14,-128}  },
	// Y=3: min 3rd — minor world; m3, P5, m7, oct; no major 3rds
	{ {7,10,12,-128}, {7,12,15,-128},  {3,7,12,-128},  {7,10,15,-128}, {3,10,12,-128}, {7,12,19,-128} },
	// Y=4: maj 3rd — major world; M3, P5, M7, 9th; no minor 3rds
	{ {7,12,-128,-128}, {7,11,12,-128},  {4,7,12,-128},  {7,11,16,-128}, {4,11,12,-128}, {7,12,16,-128} },
	// Y=5: perfect 4th — sus4 world; 4ths, 5ths, octaves; no 3rds
	{ {7,12,-128,-128}, {5,7,12,-128},  {5,12,17,-128}, {7,12,17,-128}, {5,7,17,-128},  {5,12,19,-128} },
	// Y=6: tritone — diminished/augmented; tritones, m3, M6; no P5
	{ {6,9,12,-128},  {3,6,12,-128},   {6,12,15,-128}, {6,9,15,-128},  {3,9,12,-128},  {6,12,18,-128} },
	// Y=7: perfect 5th — power chord; 5ths, octaves, 2nd/9th OK; no 3rds
	{ {7,12,-128,-128}, {7,19,-128,-128}, {7,12,19,-128}, {2,7,12,-128},  {7,12,24,-128}, {2,12,19,-128} },
	// Y=8: min 6th — minor/Phrygian; m3, P5, m6, m7; no M3 or M6
	{ {3,8,12,-128},  {5,8,12,-128},   {7,8,12,-128},  {3,8,15,-128},  {5,8,15,-128},  {8,12,15,-128} },
	// Y=9: maj 6th — major/pentatonic; M3, P5, M6; no minor colours
	{ {4,7,12,-128},  {7,9,12,-128},   {4,9,12,-128},  {7,12,16,-128}, {4,7,9,-128},   {9,12,16,-128} },
	// Y=10: min 7th — dominant/blues; P5, m7, 9th; no M7
	{ {7,10,12,-128}, {7,10,14,-128},  {5,7,10,-128},  {7,10,17,-128}, {2,7,10,-128},  {7,10,19,-128} },
	// Y=11: maj 7th — major 7th/lydian; M3, P5, M7, #4 for lydian; no m7
	{ {7,11,12,-128}, {6,11,12,-128},  {4,7,11,-128},  {7,11,16,-128}, {6,7,11,-128},  {4,11,18,-128} },
	// Y=12: octave — quartal/suspended; 4ths, 5ths, 2nds; no 3rds
	{ {7,12,-128,-128}, {5,12,-128,-128}, {7,12,17,-128}, {5,7,12,-128},  {2,7,12,-128},  {5,12,19,-128} },
};

// ---------------------------------------------------------------------------
// Per-voice stereo phase offsets: 0, 15, 30, 45, 60, 75 degrees
static constexpr uint32_t kStereoOff[6] = {
	0,
	0xFFFFFFFFu / 24,       // 15°
	0xFFFFFFFFu / 12,       // 30°
	0xFFFFFFFFu / 8,        // 45°
	0xFFFFFFFFu / 6,        // 60°
	5 * (0xFFFFFFFFu / 24), // 75°
};

// Detune spread weights for 6 voices: [-2, -1, 0, 0, +1, +2] * detuneStep
static constexpr int8_t kDetuneWeight[6] = { -2, -1, 0, 0, 1, 2 };

static constexpr int32_t kHoldoffSamples = 9600;
static constexpr int32_t kFadeInSamples  = 480;

// ---------------------------------------------------------------------------
class ChordSeq : public ComputerCard
{
public:
	uint32_t phase[6];
	uint32_t subPhase;   // sub-octave for Pulse Out 1
	uint32_t pwmPhase;   // PWM accumulator for Pulse Out 2
	uint32_t detunePhase;

	int      preset;
	int      semitone;       // current Y semitone
	int      lastSemitone;   // previous Y semitone, for change detection

	// Switch tap/hold detection
	int32_t  switchDownTimer;
	bool     switchHandled;
	bool     downArmed;
	int32_t  pendingStoreSlot;

	// Pulse In 1 arm guard
	bool     pu1Armed;

	// Pulse In 2 arm guard
	bool     pu2Armed;

	// Chord sequencer
	static constexpr int kMaxChords = 8;
	struct ChordState { int32_t voct_in; int32_t intervalSemi; int32_t preset; };
	ChordState chordSeq[kMaxChords];
	int32_t    chordCount;
	int32_t    chordWriteIdx;
	int32_t    chordPlayIdx;

	// Chord override (recalled via Pulse In 2)
	bool     chordOverride;
	int32_t  overrideVoct;
	int32_t  overrideInterval;
	int32_t  overridePreset;
	int32_t  overrideSlot;
	int32_t  overrideBaseVoct;      // voct_in at time of recall, for break detection
	int32_t  overrideBaseInterval;

	// Boot mode
	bool     slewMode;       // true = slew mode (switch held Down at boot)
	bool     modeLocked;     // true once boot switch has been sampled
	int32_t  bootAnim;       // countdown for boot LED animation

	// Slew state — persistent voice increments (only used in slew mode)
	int32_t  voiceIncSlew[6];
	int32_t  slewShift;      // IIR shift: 0=instant, 9=fast, 12=slow, 14=glacial

	// PU2 / CV Out 2 envelope: downward ramp 50%→20%, rate set by zone
	int32_t  pwmEnvelope;    // current duty level, 819..2048 (out of 4096)
	int32_t  pwmEnvAcc;      // sub-sample accumulator for fractional steps (Q16)
	int      lastZone;       // 0..3, detect zone change to reset envelope
	int32_t  lastVoctSemi;   // last voct_in semitone bucket, for pitch-change detection

	// Startup
	int32_t  sampleCount;
	int32_t  fadeGain;

	ChordSeq()
	{
		for (int i = 0; i < 6; i++) phase[i] = uint32_t(i) * (0xFFFFFFFFu / 6);
		for (int i = 0; i < 6; i++) voiceIncSlew[i] = 0;
		subPhase     = 0;
		pwmPhase     = 0;
		detunePhase  = 0;
		preset        = 0;
		semitone      = 0;
		lastSemitone  = -1;
		switchDownTimer  = 0;
		switchHandled    = false;
		downArmed        = true;
		pendingStoreSlot = -1;
		pu1Armed         = false;
		pu2Armed         = false;
		chordCount       = 0;
		chordWriteIdx    = 0;
		chordPlayIdx     = 0;
		chordOverride    = false;
		overrideVoct     = 0;
		overrideInterval = 0;
		overridePreset   = 0;
		overrideSlot     = 0;
		overrideBaseVoct     = 0;
		overrideBaseInterval = 0;
		slewMode         = false;
		modeLocked       = false;
		bootAnim         = 0;
		slewShift        = 0;
		pwmEnvelope      = 2048;
		pwmEnvAcc        = 0;
		lastZone         = -1;
		lastVoctSemi     = -1;
		sampleCount      = 0;
		fadeGain         = 0;
	}

	// Shaped oscillator.
	// shapeParam 0..2047: 0=sine, 1023=triangle, 2047=saw  (inner V zone)
	// pulseAmt   0..2047: 0=sine edge, 2047=narrow pulse    (outer zone)
	// Returns -2047..2047.
	int32_t __not_in_flash_func(ShapedOsc)(uint32_t ph, int32_t shapeParam, int32_t pulseAmt)
	{
		// Sawtooth: phase 0→-2048, 0x80000000→0, 0xFFFFFFFF→2047
		int32_t saw = int32_t(ph >> 20) - 2048;

		// Triangle: peaks at saw=0, troughs at saw=±2047
		int32_t tri;
		if (saw >= 0)
			tri = 2047 - (saw << 1);
		else
			tri = 2047 + (saw << 1);

		// Sine-ish: cubic softening of triangle
		int32_t tri_norm = tri >> 3; // -256..255, avoids overflow
		int32_t sine_ish = tri - ((tri * (tri_norm * tri_norm)) >> 17);

		int32_t out;
		if (pulseAmt > 0)
		{
			// Outer zone: pulse wave. Duty narrows as pulseAmt increases.
			// threshold sweeps from 0 (50% duty) to -1900 (~5% duty) as pulseAmt → 2047.
			// saw < threshold → high (+2047), else low (-2047).
			int32_t threshold = -(pulseAmt * 1900) / 2047; // 0 → -1900
			int32_t pulse = (saw < threshold) ? 2047 : -2047;
			// Crossfade sine→pulse over first quarter of pulseAmt to avoid a hard click
			// at the zone boundary.
			if (pulseAmt < 512)
				out = (sine_ish * (511 - pulseAmt) + pulse * pulseAmt) >> 9;
			else
				out = pulse;
		}
		else
		{
			// Inner zone: sine → triangle → saw
			if (shapeParam < 1024)
				out = (sine_ish * (1023 - shapeParam) + tri * shapeParam) >> 10;
			else
			{
				int32_t t = shapeParam - 1024;
				out = (tri * (1023 - t) + saw * t) >> 10;
			}
		}
		return out;
	}

	void __not_in_flash_func(ProcessSample)() override
	{
		// --- Pitch ---
		static constexpr int32_t kPitchBase  = 2472; // ~C3
		static constexpr int32_t kPitchRange = 1023; // 3 octaves to ~C6
		int32_t kx  = (KnobVal(Knob::X) * kPitchRange) >> 12;
		int32_t cv1 = CVIn1();
		int32_t voct_in = kPitchBase + kx + cv1;
		if (voct_in < 0)    voct_in = 0;
		if (voct_in > 4095) voct_in = 4095;

		int32_t root_inc = ExpVoct(voct_in);

		// --- Interval (Y knob → 13 semitone steps) ---
		semitone = (KnobVal(Knob::Y) * 13) >> 12;
		if (semitone > 12) semitone = 12;

		// --- Chord override: substitute recalled chord values ---
		// Break when voct_in moves >28 counts (~1 semitone) or interval changes.
		if (chordOverride)
		{
			int32_t diff = voct_in - overrideBaseVoct;
			if (diff < 0) diff = -diff;
			if (diff > 28 || semitone != overrideBaseInterval)
			{
				chordOverride = false;
			}
			else
			{
				voct_in  = overrideVoct;
				semitone = overrideInterval;
				preset   = overridePreset;
				root_inc = ExpVoct(voct_in);
			}
		}

		int32_t interval_inc = SemitoneInc(root_inc, semitone);

		// --- Envelope trigger detection ---
		// Fires when: pitch crosses a semitone boundary, interval changes,
		// preset changes (handled at event sites below), or chord recalled (same).
		bool envTrigger = false;

		// Pitch crossed a semitone boundary
		int32_t voctSemi = voct_in / 28; // ~1 semitone per bucket
		if (voctSemi != lastVoctSemi)
		{
			envTrigger   = true;
			lastVoctSemi = voctSemi;
		}

		// Interval (Y knob) changed
		if (semitone != lastSemitone)
		{
			envTrigger   = true;
			lastSemitone = semitone;
		}

		// --- Shape (V-curve with wavefold at extremes) ---
		// morphPos 0..4095 from main knob + CV In 2 offset
		// Inner V: 512..3583 → shapeParam 0..2047..0 (sine→saw→sine)
		// Outer zones: 0..511 and 3584..4095 → foldAmt 0..2047 (wavefold on sine)
		int32_t morphPos = KnobVal(Knob::Main) + CVIn2();
		if (morphPos < 0)    morphPos = 0;
		if (morphPos > 4095) morphPos = 4095;

		// Shape W-curve: pulse(extremes) → sine(9/3 o'clock) → saw(centre)
		int32_t shapeParam, pulseAmt;
		static constexpr int32_t kOuterZone = 512;
		if (morphPos < kOuterZone)
		{
			// CCW outer zone: sine → narrow pulse
			shapeParam = 0;
			pulseAmt   = ((kOuterZone - 1 - morphPos) * 2047) / (kOuterZone - 1);
		}
		else if (morphPos > 4095 - kOuterZone)
		{
			// CW outer zone: sine → narrow pulse
			shapeParam = 0;
			pulseAmt   = ((morphPos - (4095 - kOuterZone)) * 2047) / (kOuterZone - 1);
		}
		else
		{
			// Inner V: map 512..3583 → 0..2047..0 (sine→saw→sine)
			pulseAmt = 0;
			int32_t inner = morphPos - kOuterZone; // 0..3071
			if (inner < 1536)
				shapeParam = (inner * 2047) / 1535; // 0→2047 (sine→saw)
			else
				shapeParam = ((3071 - inner) * 2047) / 1535; // 2047→0 (saw→sine)
		}

		// --- Zone (physical knob + switch) ---
		// In normal mode: detune cents. In slew mode: slew rate.
		int32_t physKnob = KnobVal(Knob::Main);
		Switch  sw       = SwitchVal();

		// --- Zone → detune (normal mode) / slew rate (slew mode) ---
		// Zone 0: Mid CCW, Zone 1: Mid CW, Zone 2: Up CW, Zone 3: Up CCW
		// Switch Down (momentary tap) is not a stable zone — use zone 0 (~250ms envelope)
		int zone;
		if (sw == Switch::Up)
			zone = (physKnob < 2048) ? 3 : 2;
		else if (sw == Switch::Middle)
			zone = (physKnob < 2048) ? 0 : 1;
		else // Switch::Down — treat as zone 0 for ~250ms envelope rate
			zone = 0;

		// --- Detune (normal mode only) ---
		int32_t detuneStep = 0;
		if (!slewMode)
		{
			static constexpr int32_t kDetuneCents[4] = { 0, 5, 10, 15 };
			int32_t detuneAmtCents = kDetuneCents[zone];
			detuneStep = int32_t((int64_t(root_inc) * detuneAmtCents) / 1200);
		}
		else
		{
			// Slew mode: zone → IIR shift
			static constexpr int kSlewShift[4] = { 0, 9, 12, 14 };
			slewShift = kSlewShift[zone];
		}

		// --- PU2 / CV Out 2 envelope: 50%→20% downward ramp ---
		// Resets to 50% on zone change. Rate per zone:
		// Zone 0 (Mid CCW): instant (snap to 20%)
		// Zone 1 (Mid CW):  fast  (~100ms, step Q16 = 1229*65536/4800  ≈ 16776)
		// Zone 2 (Up CW):   slow  (~500ms, step Q16 = 1229*65536/24000 ≈ 3356)
		// Zone 3 (Up CCW):  glacial (~3s,  step Q16 = 1229*65536/144000 ≈ 559)
		// 50% duty = 2048, 20% duty = 819, range = 1229 counts
		// Zone 0 (Mid CCW): ~250ms = 12000 samples → 1229*65536/12000 ≈ 6716
		// Zone 1 (Mid CW):  ~100ms = 4800  samples → 1229*65536/4800  ≈ 16776 (fast)
		// Zone 2 (Up CW):   ~500ms = 24000 samples → 1229*65536/24000 ≈ 3356  (slow)
		// Zone 3 (Up CCW):  ~3s    = 144000 samples→ 1229*65536/144000 ≈ 559  (glacial)
		static constexpr int32_t kEnvStep[4] = { 6716, 16776, 3356, 559 };

		lastZone = zone; // track zone but don't use it to trigger envelope

		// --- Voice increment targets ---
		int32_t voice_inc_target[6];
		voice_inc_target[0] = root_inc     + kDetuneWeight[0] * detuneStep;
		voice_inc_target[1] = interval_inc + kDetuneWeight[1] * detuneStep;

		for (int v = 0; v < 4; v++)
		{
			int8_t ext = extensions[semitone][preset][v];
			if (ext == -128)
				voice_inc_target[2 + v] = 0;
			else
			{
				int32_t ext_inc = SemitoneInc(root_inc, int(ext));
				voice_inc_target[2 + v] = ext_inc + kDetuneWeight[2 + v] * detuneStep;
			}
		}

		// --- Apply slew (slew mode) or use targets directly (normal mode) ---
		int32_t voice_inc[6];
		if (slewMode && slewShift > 0)
		{
			for (int i = 0; i < 6; i++)
			{
				voiceIncSlew[i] += (voice_inc_target[i] - voiceIncSlew[i]) >> slewShift;
				voice_inc[i] = voiceIncSlew[i];
			}
		}
		else
		{
			for (int i = 0; i < 6; i++)
			{
				voice_inc[i]     = voice_inc_target[i];
				voiceIncSlew[i]  = voice_inc_target[i]; // keep slew state in sync when instant
			}
		}

		// --- Oscillator sum ---
		int32_t sum_L = 0, sum_R = 0;
		for (int i = 0; i < 6; i++)
		{
			phase[i] += uint32_t(voice_inc[i]);
			if (voice_inc_target[i] == 0 && voice_inc[i] == 0) continue;
			sum_L += ShapedOsc(phase[i],                    shapeParam, pulseAmt);
			sum_R += ShapedOsc(phase[i] + kStereoOff[i],   shapeParam, pulseAmt);
		}
		// Divide by 8 for headroom (not all 6 voices always active)
		int32_t out_L = sum_L >> 3;
		int32_t out_R = sum_R >> 3;

		// --- Pulse Out 1: sub-octave square ---
		subPhase += uint32_t(root_inc >> 1);
		PulseOut1(subPhase >> 31);

		// --- Pulse Out 2: PWM square at root, duty ramps 50%→20% ---
		pwmPhase += uint32_t(root_inc);
		PulseOut2(int32_t(pwmPhase >> 20) < pwmEnvelope);

		// --- CV Out 1: root + interval as MIDI note (update every 512 samples) ---
		static int32_t cvOutCounter = 0;
		if (++cvOutCounter >= 512)
		{
			cvOutCounter = 0;
			// C3 = MIDI 48 at voct_in ~2472; 341 steps per octave = 12 semitones
			int32_t midiNote = 48 + ((voct_in - 2472) * 12 + 170) / 341 + semitone;
			if (midiNote < 0)   midiNote = 0;
			if (midiNote > 127) midiNote = 127;
			CVOut1MIDINote(uint8_t(midiNote));
		}

		// --- CV Out 2: downward ramp matching PU2 duty (+5V at chord event, decays to 0V) ---
		// pwmEnvelope 2048→819, map to 2047→0
		int32_t cv2 = ((pwmEnvelope - 819) * 2047) / 1229;
		CVOut2(int16_t(cv2));

		// --- Boot mode detection (4800 samples = ~100ms for ADC to settle) ---
		if (!modeLocked && sampleCount == 4800)
		{
			slewMode  = (sw == Switch::Down);
			modeLocked = true;
			bootAnim   = 9600; // ~200ms LED animation
		}

		// --- Switch tap/hold (boot guard: wait 4800 samples) ---
		// In slew mode, Switch Down at boot selects the mode — don't treat it as a tap.
		// downArmed starts true but we clear it if Down was held at boot.
		if (slewMode && sampleCount <= 4800)
			downArmed = false;

		if (sampleCount > 4800)
		{
			if (sw == Switch::Down)
			{
				switchDownTimer++;
				// At exactly 1 second: mark pending store (LEDs change immediately)
				if (switchDownTimer == 48000 && downArmed)
					pendingStoreSlot = chordWriteIdx;
			}
			else
			{
				if (downArmed)
				{
					if (pendingStoreSlot >= 0)
					{
						// Release after hold: commit the store
						chordSeq[chordWriteIdx] = { voct_in, semitone, preset };
						chordWriteIdx = (chordWriteIdx + 1) % kMaxChords;
						if (chordCount < kMaxChords) chordCount++;
						pendingStoreSlot = -1;
					}
					else if (switchDownTimer > 0 && switchDownTimer < 48000)
					{
						// Short tap: advance preset
						preset = (preset + 1) % 6;
						if (chordOverride) overridePreset = preset;
						envTrigger = true;
					}
					downArmed = false;
				}
				if (switchDownTimer == 0) downArmed = true;
				switchDownTimer = 0;
			}
		}

		// --- Pulse In 1: advance preset on rising edge ---
		if (!PulseIn1()) pu1Armed = true;
		if (pu1Armed && PulseIn1RisingEdge())
		{
			preset = (preset + 1) % 6;
			if (chordOverride) overridePreset = preset;
			envTrigger = true;
		}

		// --- Pulse In 2: recall next stored chord (arm guard) ---
		if (!PulseIn2()) pu2Armed = true;
		if (pu2Armed && PulseIn2RisingEdge() && chordCount > 0)
		{
			int32_t idx      = chordPlayIdx % chordCount;
			overrideVoct     = chordSeq[idx].voct_in;
			overrideInterval = chordSeq[idx].intervalSemi;
			overridePreset   = chordSeq[idx].preset;
			overrideSlot     = idx;
			// Sample baseline from *physical* knob position (pre-override voct_in/semitone).
			// Use the raw knob computation so a prior override doesn't poison the baseline.
			int32_t physVoct = kPitchBase + (KnobVal(Knob::X) * kPitchRange >> 12) + CVIn1();
			if (physVoct < 0)    physVoct = 0;
			if (physVoct > 4095) physVoct = 4095;
			int32_t physSemi = (KnobVal(Knob::Y) * 13) >> 12;
			if (physSemi > 12) physSemi = 12;
			overrideBaseVoct     = physVoct;
			overrideBaseInterval = physSemi;
			chordOverride        = true;
			chordPlayIdx         = (chordPlayIdx + 1) % chordCount;
			envTrigger           = true;
		}

		// --- Envelope reset + decay (after all event handlers so tap/PU1 are included) ---
		if (envTrigger)
		{
			pwmEnvelope = 2048;
			pwmEnvAcc   = 0;
		}

		if (pwmEnvelope > 819)
		{
			pwmEnvAcc += kEnvStep[zone];
			int32_t step = pwmEnvAcc >> 16;
			pwmEnvAcc   &= 0xFFFF;
			pwmEnvelope -= step;
			if (pwmEnvelope < 819) pwmEnvelope = 819;
		}

		// --- LEDs ---
		if (bootAnim > 0)
		{
			// Boot animation: sweep LEDs to indicate mode.
			// Normal mode: single LED sweeps 0→5 (one pass).
			// Slew mode:   all LEDs pulse on then off (one flash).
			bootAnim--;
			int32_t pos = (9599 - bootAnim) * 6 / 9600; // 0..5
			if (slewMode)
			{
				// All LEDs on for first half, off for second half
				int32_t bright = (bootAnim > 4800) ? 4095 : 0;
				for (int i = 0; i < 6; i++) LedBrightness(i, bright);
			}
			else
			{
				for (int i = 0; i < 6; i++) LedBrightness(i, i == pos ? 4095 : 0);
			}
		}
		else
		{
			static int32_t ledCounter = 0;
			if (++ledCounter >= 256)
			{
				ledCounter = 0;
				if (pendingStoreSlot >= 0)
				{
					// Storing: LEDs 1,3,5 full bright; LEDs 0,2,4 = slot in binary
					LedBrightness(0, (pendingStoreSlot & 1) ? 4095 : 0);
					LedBrightness(1, 4095);
					LedBrightness(2, (pendingStoreSlot & 2) ? 4095 : 0);
					LedBrightness(3, 4095);
					LedBrightness(4, (pendingStoreSlot & 4) ? 4095 : 0);
					LedBrightness(5, 4095);
				}
				else if (chordOverride)
				{
					// Chord held: LEDs 0,2,4 full bright; LEDs 1,3,5 = slot in binary
					LedBrightness(0, 4095);
					LedBrightness(1, (overrideSlot & 1) ? 4095 : 0);
					LedBrightness(2, 4095);
					LedBrightness(3, (overrideSlot & 2) ? 4095 : 0);
					LedBrightness(4, 4095);
					LedBrightness(5, (overrideSlot & 4) ? 4095 : 0);
				}
				else
				{
					// Normal: Y position on LEDs 0–4, preset brightness on LED 5
					int32_t ledIdx = semitone * 5 / 13;
					for (int i = 0; i < 5; i++)
						LedOn(i, i == ledIdx);
					LedBrightness(5, uint16_t(preset * 819));
				}
			}
		}

		// --- Holdoff and fade-in ---
		if (sampleCount < kHoldoffSamples)
		{
			sampleCount++;
			AudioOut1(0);
			AudioOut2(0);
			return;
		}
		sampleCount++; // keep incrementing for switch guard

		if (fadeGain < 4095)
		{
			fadeGain += 4095 / kFadeInSamples;
			if (fadeGain > 4095) fadeGain = 4095;
			out_L = (out_L * fadeGain) >> 12;
			out_R = (out_R * fadeGain) >> 12;
		}

		if (out_L >  2047) out_L =  2047;
		if (out_L < -2047) out_L = -2047;
		if (out_R >  2047) out_R =  2047;
		if (out_R < -2047) out_R = -2047;

		AudioOut1(int16_t(out_L));
		AudioOut2(int16_t(out_R));
	}
};

ChordSeq chordseq;

int main()
{
	set_sys_clock_khz(144000, true);
	chordseq.Run();
}
