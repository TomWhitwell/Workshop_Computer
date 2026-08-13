// Slag — metallic percussion, generated.
//
// One harsh metal-percussion voice wired to a random step sequencer whose
// stream you can LOCK into a loop. Every step re-rolls the whole voice 
// — pitch, decay, noise blend, corrosion, accent, and whether the step fires 
// at all — and STEPS freezes the last N of those rolls into a repeating pattern. 
// Leave it Off for endless fresh chaos; when you hear something you like, lock it.
//
// The random stream is a hash of (seed, step index), not a stored buffer, so
// "locking" is just picking a window into it — instant, and re-slicing the same
// window at a different length keeps the pattern's character.
//
// The voice: six inharmonic square partials (the classic metal-percussion
// recipe) blended with white noise — or with Audio In 1, if you patch it —
// through a decay envelope, then into "rust": digital overdrive plus
// downsampling, from warm to aliased garbage.
//
// There is deliberately no filter and no effects. The metal and the kick come
// out of separate jacks instead, so each can take its own processing without
// the other one going through it too.
//
// With no clock patched, CV In 1 becomes a keyboard: a jump on the pitch input
// is a new note. Four buttons on a 4 Voltages gives four repeatable hits — the
// voltage picks the pitch *and* keys the timbre roll, so each button is its own
// sound rather than the same clang transposed.
//
// I/O:
//   Audio In 1    replaces the internal noise source (blend it in with Tone)
//   Audio In 2    unused
//   Audio Out 1   the metal, rusted — no low end in it at all
//   Audio Out 2   the kick, clean and never through the drive
//   CV In 1       pitch, 1V/oct, adds to the Pitch knob — and the keyboard
//   CV In 2       chance, adds to the Chance knob
//   Pulse In 1    clock. Unpatched, the internal clock runs at Tempo instead
//   Pulse In 2    reset — restart the locked pattern at step 1
//   Pulse Out 1   trigger — fires on every step that sounds
//   Pulse Out 2   ghost — fires on every step that was skipped
//   CV Out 1      the step's pitch as 1V/oct CV, sampled and held
//   CV Out 2      the decay envelope, 0..+6V
//
//   Switch Middle  Main = Pitch    X = Decay   Y = Rust
//   Switch Up      Main = Steps    X = Chance  Y = Spread
//   Switch Down    tap = hit it by hand. HELD, the knobs re-bank again:
//                  Main = Tempo    X = Tone    Y = Metal
//
//   LEDs          a dot walks the pattern; its brightness is the hit

#include "ComputerCard.h"
#include <cmath>

class Slag : public ComputerCard
{
	// ---- pitch units: 1024 = one octave. CV In +/-2048 == +/-6V, so 1V/oct = *3.
	static constexpr int32_t POCT  = 1024;
	static constexpr int32_t PMIN  = 0;
	static constexpr int32_t PMAX  = 6800;    // top partial (8.3x) stays under Nyquist

	// Phase increment of the fundamental at pitch 0 (~28Hz), Q32 per 48kHz sample.
	static constexpr uint32_t INC0 = 2504878u;

	// Six square partials, Q8 ratios against the fundamental. Both ends are
	// inharmonic — whole-number harmonics ring-modulate into a periodic signal,
	// which is a beep, so there is deliberately no tuned end to this knob.
	// Metal opens the ratios out: tight is bell and anvil, wide is scrapyard.
	static constexpr int32_t TIGHT[6] = {256, 370,  414,  493,  641,  682};
	static constexpr int32_t WIDE[6]  = {256, 419,  733, 1108, 1607, 2131};

	static constexpr int32_t ENV_MAX = 65536;   // Q16 envelope
	static constexpr int32_t PDROP   = 512;     // attack pitch blip, half an octave
	static constexpr int32_t PULSE_LEN = 480;   // 10ms
	static constexpr int32_t TAKEOVER  = 96;    // knob travel needed to grab a param
	static constexpr int32_t KB_JUMP   = 40;    // CV counts (~1.4 semitones) = a new note
	static constexpr int32_t PHRASE_BREAK = 96; // /256 chance a step leaves the phrase

	// [3/2] Pade approximant of tanh: y = x(x^2 + 27) / (9x^2 + 27), valid to |x| <= 3.
	// Working in units of 2048 = 1.0: 27 -> 27*2048, and |x| <= 3 -> 6144.
	static constexpr int32_t PADE_K   = 27 * 2048;
	static constexpr int32_t PADE_MAX = 3 * 2048;

	// Hit archetypes, one drawn per step. The randomiser used to nudge every
	// parameter independently around a common centre, which makes a single
	// instrument wobble rather than a kit play — every hit had a bit of
	// everything in it. These are discrete *kinds* of hit instead, and Spread
	// decides how far from "all of it at once" a step is allowed to stray.
	// Multipliers on the knobs, /256, so the panel still sets the palette.
	// `kickDens` and `metalDens` are each voice's share of Chance, /256. They are
	// what make the two voices separate *rhythms* rather than one rhythm with a
	// level balance: each voice rolls its own dice against its own density, so
	// they land on different steps and only sometimes coincide. Gating levels on
	// a single shared skip roll — which is what this was — can never do that,
	// because both voices are then offered exactly the same steps.
	struct Hit { int16_t kickDens, metalDens, tone, level, decay; };
	static constexpr Hit HITS[8] = {
		{256, 256, 256, 256, 256},   // both, as the knobs have it
		{256,  60,  64, 240, 220},   // kick-driven, metal occasional
		{ 50, 256, 256, 256, 256},   // metal-driven, kick occasional
		{ 30, 200, 200, 150, 480},   // sparse ticks over almost no kick
		{230, 130, 200, 230, 300},   // rolling kick, sparse air
		{ 16, 256, 256, 130, 600},   // hats — metal only, near enough
		{200, 200,  96, 256, 200},   // heavy, both, long
		{190,  40, 160, 100, 420},   // ghost kicks, almost no metal
	};

	static constexpr int32_t LENS[10] = {0, 2, 3, 4, 5, 7, 8, 10, 16, 32};
	static constexpr int32_t ZB[11]   = {0,410,819,1229,1638,2048,2458,2867,3277,3686,4096};

	// --- voice ---
	uint32_t phase[6] = {0,0,0,0,0,0};
	uint32_t inc[6]   = {0,0,0,0,0,0};
	int32_t  ratio[6] = {256, 370, 414, 493, 641, 682};   // set by Metal each pass
	uint32_t bodyPhase = 0, bodyInc = 0; // the kick, three octaves under the knob
	static constexpr int32_t BODY   = 256;  // full scale — it has its own jack now
	static constexpr int32_t BSWEEP = 2048; // kick pitch sweep, two octaves
	int32_t  env = 0, penv = 0;          // metal amplitude and attack pitch blip
	int32_t  benv = 0, kenv = 0;         // kick amplitude and its pitch sweep
	int32_t  decRate = 200;              // env subtrahend numerator, big = fast
	uint32_t rng = 0x1234567u;
	uint32_t pow2lut[256];               // Q16 2^(i/256), built at boot
	uint16_t xfLut[257];                 // sin(i/256 * pi/2) * 256, for crossfades
	int16_t  sinLut[1024];               // full sine, for the kick body

	// --- rust ---
	// The sample-rate reducer is a phase accumulator, not a sample counter: a
	// counter can only divide by whole numbers, so the whole top half of the
	// Rust knob had just eight reachable rates. This is continuous.
	uint32_t dsPhase = 0, dsInc = 0xFFFFFFFFu;
	int32_t  dsHold = 0, drive = 77, makeup = 13981;

	// --- sequencer ---
	uint32_t seed = 0xC0FFEEu;
	bool     seeded = false;
	uint32_t freeStep = 0;               // never repeats — the free-running stream
	uint32_t patBase = 0;                // stream index the locked window opens at
	uint32_t patPhase = 0;
	int32_t  patLen = 0;                 // 0 = Off
	int32_t  stepsZone = 0;
	uint32_t clockCount = 0, sampleCtr = 0;
	int32_t  trigTimer = 0, ghostTimer = 0;
	int      ledStep = 0;
	bool     lastSwDown = false;

	// --- latched per-step values ---
	int32_t  stepPitch = 0;              // pitch units, +/-2 octaves at full spread
	int32_t  stepNoise = 64;             // 0..256 noise blend
	int32_t  stepLevel = 256;            // 0..256 accent, shared by both voices
	bool     stepKickOn = true;          // did the kick win its roll this step
	bool     stepMetalOn = true;         // did the metal win its roll this step
	int32_t  stepDecMul = 256;           // 32..512, scales the decay rate
	int32_t  stepRust = 0;               // added to the Rust knob
	int32_t  stepPDrop = PDROP;          // attack pitch blip depth, 0..3 octaves
	int32_t  stepCV = 0;                 // CV Out 1, sampled and held

	// --- knob-derived controls ---
	int32_t  pitchBase = 3072, rustKnob = 0, chance = 256, spread = 128;
	int32_t  toneBase = 69, tempoPeriod = 16360;
	int32_t  cloudMix = 205, ratchet = 0;

	// --- ratchets ---
	uint32_t lastClockAt = 0, clockGap = 12000;
	int32_t  ratchetLeft = 0;
	uint32_t ratchetPeriod = 0, ratchetTimer = 0;
	bool     extNoise = false;

	// --- keyboard on the pitch input ---
	int32_t  kbLast = 0;
	bool     kbArm = false;

	// Three banks of three knobs, selected by the switch (Down is a held bank).
	// A knob does not take over its parameter until you have physically moved
	// it, so going to set Steps or Tempo does not yank Pitch on the way back.
	struct Banked { int32_t val, entry; bool live; };
	Banked bank[3][3];
	int    curBank = -1;

	// A 32-bit avalanche (Wang/lowbias32). Once per step, so cost is irrelevant;
	// what matters is that neighbouring indices give unrelated values.
	static uint32_t hash32(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16; return x;
	}

	static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	// Constant-power crossfade, m = 0..256. A linear blend of two uncorrelated
	// signals loses 3dB in the middle — the Tone knob measured a 5dB hole at
	// three-quarters — because the weights sum to one but the *powers* do not.
	int32_t xfade(int32_t a, int32_t b, int32_t m) const
	{
		return (a * (int32_t)xfLut[256 - m] + b * (int32_t)xfLut[m]) >> 8;
	}

public:
	Slag()
	{
		// Boot-time float only — never in the audio path. (See 92_Vox: a single
		// powf inside ProcessSample blows the whole per-sample budget.)
		for (int i = 0; i < 256; i++)
			pow2lut[i] = (uint32_t)(65536.0f * powf(2.0f, i / 256.0f) + 0.5f);
		for (int i = 0; i <= 256; i++)
			xfLut[i] = (uint16_t)(256.0f * sinf(i * 1.5707963f / 256.0f) + 0.5f);
		for (int i = 0; i < 1024; i++)
			sinLut[i] = (int16_t)(2047.0f * sinf(i * 6.2831853f / 1024.0f));

		// Sensible power-up values until the knobs are touched.
		int32_t init[3][3] = {
			{2048, 1400,  600},    // Middle: pitch, decay, rust
			// Chance sits below the halfway point on purpose: past it every step
			// fires and ratchets start, which is too busy to wake up to.
			{   0, 1500, 1200},    // Up:     steps (Off), chance (~73%), spread
			{1800, 1100, 2200},    // Down:   tempo (~3Hz), tone, metal
		};
		for (int b = 0; b < 3; b++)
			for (int k = 0; k < 3; k++) bank[b][k] = {init[b][k], init[b][k], false};
		updateVoice();
	}

private:
	// Pitch and rust have to be right on the attack transient, so this runs both
	// at control rate and immediately on trigger.
	// Phase increment for a pitch, in the usual 1024-per-octave units.
	uint32_t incFor(int32_t p) const
	{
		p = clampi(p, PMIN, PMAX);
		uint32_t m = pow2lut[(p & 1023) >> 2];
		return ((uint32_t)(((uint64_t)INC0 * m) >> 16)) << (p >> 10);
	}

	void updateVoice()
	{
		uint32_t base = incFor(pitchBase + stepPitch + ((penv * stepPDrop) >> 16));
		for (int i = 0; i < 6; i++)
			inc[i] = (uint32_t)(((uint64_t)base * (uint32_t)ratio[i]) >> 8);

		// The kick runs three octaves under the *knob*, and deliberately ignores
		// both the per-step random and the big attack blip — a bass drum that
		// jumps two octaves a hit is not a bass drum.
		//
		// The sweep rides the *fast* envelope while the level rides the slow
		// one, which is the whole trick of a 909-style kick: pitch falls two
		// octaves in ~25ms while the amplitude rings on for ten times that. At
		// Pitch noon it starts around 210Hz and lands on 53Hz. That drop is the
		// thump; a body without it is just a low hum.
		bodyInc = incFor(pitchBase - 3072 + ((kenv * BSWEEP) >> 16));

		int32_t r = clampi(rustKnob + stepRust, 0, 4095);

		// Drive runs 0.3x to ~8x. Starting well below unity keeps the bottom of
		// the knob on the linear part of the shaper — genuinely clean — because
		// the makeup gain below puts the level back either way.
		drive = 77 + (r >> 1);
		int32_t d  = clampi((2048 * drive) >> 8, 1, PADE_MAX);
		int32_t qd = (d * d) >> 11;
		int32_t pd = (d * (qd + PADE_K)) / (9 * qd + PADE_K);
		makeup = (2048 << 12) / (pd > 0 ? pd : 1);

		// Sample-rate reduction over the top half of the knob: 48kHz down to
		// 3kHz, four octaves, continuous.
		if (r <= 2048) dsInc = 0xFFFFFFFFu;
		else
		{
			int32_t t = (r - 2048) << 1;                        // 0..4094
			uint32_t hi = 0xFFFFFFFFu >> (t >> 10);
			dsInc = hi - (uint32_t)(((uint64_t)(hi >> 1) * (uint32_t)(t & 1023)) >> 10);
		}
	}

	void updateControls()
	{
		Switch sw = SwitchVal();
		int b = (sw == Up) ? 1 : (sw == Down ? 2 : 0);
		if (b != curBank)
		{
			// At power-up the panel should not lie, so the bank you boot into
			// adopts its knobs straight away. After that, takeover applies.
			bool boot = (curBank < 0);
			curBank = b;
			for (int k = 0; k < 3; k++)
			{
				bank[b][k].entry = KnobVal((Knob)k);
				bank[b][k].live  = boot;
				if (boot) bank[b][k].val = KnobVal((Knob)k);
			}
		}
		for (int k = 0; k < 3; k++)
		{
			int32_t kv = KnobVal((Knob)k);
			Banked &p = bank[b][k];
			if (!p.live)
			{
				int32_t d = kv - p.entry;
				if ((d < 0 ? -d : d) > TAKEOVER) p.live = true;
			}
			if (p.live) p.val = kv;
		}

		pitchBase = 1024 + ((bank[0][0].val * 5120) >> 12) + CVIn1() * 3;
		rustKnob  = bank[0][2].val;
		spread    = (bank[1][2].val * 257) >> 12;
		toneBase  = (bank[2][1].val * 257) >> 12;

		// Density runs past "every step fires" into ratchets. Below halfway it
		// is the skip probability; above, every step fires and the knob buys an
		// increasing chance that a step subdivides instead.
		int32_t dens = clampi(((bank[1][1].val + CVIn2() * 2) * 513) >> 12, 0, 512);
		chance  = dens > 256 ? 256 : dens;
		ratchet = dens > 256 ? dens - 256 : 0;

		// Metal: opens the partials out from a tight bell to a wide scrapyard,
		// and crossfades the bare fundamental against the XOR cloud. Keeping a
		// floor of cloud means the tonal end is a struck bell rather than a
		// bare square — a clear pitch with metal around it, not a beep.
		int32_t mk = bank[2][2].val;
		for (int i = 0; i < 6; i++) ratio[i] = TIGHT[i] + (((WIDE[i] - TIGHT[i]) * mk) >> 12);
		cloudMix = 128 + ((mk * 129) >> 12);

		// Tempo: ~0.5Hz to ~32Hz, and fully anticlockwise turns the internal
		// clock off so the card only speaks when you play it.
		int32_t tk = bank[2][0].val;
		if (tk < 64) tempoPeriod = 0;
		else
		{
			int32_t te = tk * 6;
			int32_t to = te >> 12; if (to > 5) to = 5;
			int32_t th = 96000 >> to;
			tempoPeriod = th - (((th >> 1) * (te & 4095)) >> 12);
		}

		// Minimum viable keyboard. A jump on the pitch input is a new note: the
		// voltage sets the pitch and keys the timbre, so four buttons on a
		// 4 Voltages give four repeatable hits. Only with no clock patched, so a
		// sequencer's pitch CV cannot double-trigger against its own clock.
		// Armed on the jump and fired once it settles, so a slewed source lands
		// on the pitch it arrived at rather than one it passed through.
		if (Disconnected(Pulse1))
		{
			int32_t cv = CVIn1();
			int32_t d = cv - kbLast; if (d < 0) d = -d;
			if (kbArm) { if (d < 8) { kbArm = false; playHit(0xA5A50000u + (uint32_t)((cv + 2048) >> 7)); } }
			else if (d > KB_JUMP) kbArm = true;
			kbLast = cv;
		}

		// Decay: ~4ms to ~2.7s, exponential across eleven octaves without a LUT.
		int32_t e = bank[0][1].val * 11;
		int32_t oct = e >> 12; if (oct > 10) oct = 10;
		int32_t hi = 2730 >> oct;
		int32_t d = hi - (((hi >> 1) * (e & 4095)) >> 12);
		decRate = clampi((d * stepDecMul) >> 8, 3, 4000);

		// Steps, with a deadband so knob jitter at a zone edge cannot flip the
		// pattern length underneath you.
		int32_t ks = bank[1][0].val;
		int32_t z = 0; while (z < 9 && ks >= ZB[z + 1]) z++;
		if (z != stepsZone && ks > ZB[z] + 48 && ks < ZB[z + 1] - 48) stepsZone = z;
		int32_t newLen = LENS[stepsZone];
		if (newLen != patLen)
		{
			// Coming out of Off, lock the window that just played. Changing
			// between lengths re-slices that same window instead of rerolling.
			if (newLen && patLen == 0) patBase = freeStep - (uint32_t)newLen;
			patPhase = 0;
			patLen = newLen;
		}

		extNoise = Connected(Audio1);

		int32_t bright = 300 + ((env * 3795) >> 16);
		for (int i = 0; i < 6; i++) LedBrightness(i, i == ledStep ? (uint16_t)bright : 0);

		updateVoice();
	}

	void stirSeed()
	{
		// Otherwise every power-up plays the identical "random" sequence.
		if (seeded) return;
		seed ^= sampleCtr ^ ((uint32_t)KnobVal(Main) << 20)
		                  ^ ((uint32_t)KnobVal(X) << 8) ^ (uint32_t)KnobVal(Y);
		seeded = true;
	}

	// Turn one hash into a whole voice. `keyed` means the note was played rather
	// than rolled, so its pitch comes from the knob and the CV, not the dice.
	// Returns false if neither voice wanted this step — the caller treats that
	// as a skip and pulses the ghost output.
	bool applyRoll(uint32_t h, uint32_t idxForRoll, bool keyed)
	{
		uint32_t h2 = hash32(h), h3 = hash32(h2);
		int32_t rP = (int32_t)((h  >> 8)  & 0xFF) - 128;
		int32_t rD = (int32_t)((h  >> 16) & 0xFF) - 128;
		int32_t rN = (int32_t)((h  >> 24) & 0xFF) - 128;
		int32_t rR = (int32_t)( h2        & 0xFF) - 128;
		int32_t rL = (int32_t)((h2 >> 8)  & 0xFF) - 128;
		int32_t rE = (int32_t)((h2 >> 16) & 0xFF) - 128;

		// Pick a kind of hit, then blend from "everything" toward it by Spread —
		// so Spread at zero still gives the uniform voice it always did. The
		// continuous nudges below are deliberately smaller than they were: the
		// archetype should decide what this hit *is*, and the dice only colour
		// it in. Micro-randomising every parameter was the thing that made
		// every step sound like every other step.
		// A phrase holds one archetype across a run of steps, so a passage
		// leans kick-heavy or metal-heavy before moving on. Roughly a third of
		// steps break out of it, which keeps the run from turning into a
		// machine-gun of one sound while still reading as a phrase. The phrase
		// index comes off the same stream, so locking a pattern locks its shape.
		uint32_t ph = hash32(seed ^ 0xB1A5C0DEu ^ (idxForRoll >> 3));
		int32_t hi = (int32_t)(ph & 7);
		if ((int32_t)(h3 & 0xFF) < PHRASE_BREAK) hi = (int32_t)((h3 >> 8) & 7);

		const Hit &H = HITS[hi];
		int32_t kM = 256 + (((int32_t)H.kickDens  - 256) * spread >> 8);
		int32_t mM = 256 + (((int32_t)H.metalDens - 256) * spread >> 8);
		int32_t tM = 256 + (((int32_t)H.tone      - 256) * spread >> 8);
		int32_t lM = 256 + (((int32_t)H.level     - 256) * spread >> 8);
		int32_t dM = 256 + (((int32_t)H.decay     - 256) * spread >> 8);

		// Each voice rolls its own dice against its own share of Chance. Two
		// rolls, two rhythms — they coincide only as often as chance dictates,
		// instead of always landing on the same steps.
		stepKickOn  = keyed || ((int32_t)( h        & 0xFF) < ((chance * kM) >> 8));
		stepMetalOn = keyed || ((int32_t)((h3 >> 16) & 0xFF) < ((chance * mM) >> 8));
		if (!stepKickOn && !stepMetalOn) return false;

		int32_t acc = clampi(lM + ((rL * spread) >> 9), 24, 256);
		stepPitch  = keyed ? 0 : ((rP * spread) >> 4);              // +/-2 octaves
		stepLevel  = acc;
		stepNoise  = clampi(((toneBase * tM) >> 8) + ((rN * spread) >> 8), 0, 256);
		stepDecMul = clampi(dM + ((rD * spread) >> 8), 32, 768);
		stepRust   = (rR * spread) >> 3;
		// How far the attack blips the pitch, 0 to 3 octaves. On the real thing
		// the envelope goes to pitch rather than amplitude, and its depth is a
		// modulation destination — it is the difference between a tick, a thud
		// and a zap, so it earns a slice of the roll.
		stepPDrop  = clampi(PDROP + ((rE * spread) >> 4), 0, 3072);
		stepCV     = clampi(((pitchBase + stepPitch) * 341) >> 10, -2048, 2047);

		// A voice that lost its roll is left alone rather than silenced, so a
		// long kick rings on underneath the metal-only steps that follow it.
		if (stepMetalOn) { env  = ENV_MAX; penv = ENV_MAX; }
		if (stepKickOn)  { benv = ENV_MAX; kenv = ENV_MAX; }
		trigTimer = PULSE_LEN;
		updateVoice();
		return true;
	}

	// A hit played by hand or by the keyboard. No skip roll — you asked for it —
	// and the locked pattern does not move out from under you.
	void playHit(uint32_t idx)
	{
		stirSeed();
		applyRoll(hash32(seed ^ (idx * 0x9E3779B9u)), idx, true);
	}

	void doStep()
	{
		stirSeed();
		ratchetLeft = 0;                 // a new step cancels the old subdivision

		uint32_t idx = patLen ? (patBase + patPhase) : freeStep;
		freeStep++;
		if (patLen) { ledStep = (int)(patPhase % 6); if (++patPhase >= (uint32_t)patLen) patPhase = 0; }
		else        { ledStep = (int)(idx % 6); }

		// The skip roll comes from the same hash as the timbres, so a locked
		// pattern locks its rhythm as well as its sounds.
		uint32_t h = hash32(seed ^ (idx * 0x9E3779B9u));
		if (!applyRoll(h, idx, false)) { ghostTimer = PULSE_LEN; return; }

		// Ratchet: split this step into 2..4 strikes of the same sound. A step
		// sequencer that only ever asks "does this one fire?" is a coin flip on
		// a grid; subdividing is what makes it read as a rhythm. The roll comes
		// from the same hash, so a locked pattern locks its ratchets too.
		static constexpr int32_t RN[4] = {1, 1, 2, 3};
		uint32_t h2 = hash32(h);
		if (ratchet && (int32_t)(((h2 >> 24) & 0x3F) << 2) < ratchet && clockGap > 1600)
		{
			ratchetLeft   = RN[(h2 >> 30) & 3];
			ratchetPeriod = clockGap / (uint32_t)(ratchetLeft + 1);
			ratchetTimer  = ratchetPeriod;
		}
	}

public:
	virtual void __not_in_flash_func(ProcessSample)()
	{
		sampleCtr++;
		if ((sampleCtr & 31) == 0) updateControls();

		// ---- clock ----
		bool clk = false;
		if (Connected(Pulse1)) clk = PulseInRisingEdge(0);
		else if (tempoPeriod && ++clockCount >= (uint32_t)tempoPeriod) { clockCount = 0; clk = true; }

		// The big red button. It plays a hit but does not advance the sequencer,
		// so reaching down to edit Tempo cannot shift a locked pattern.
		bool swDown = (SwitchVal() == Down);
		if (swDown && !lastSwDown) playHit(freeStep++);
		lastSwDown = swDown;

		if (PulseInRisingEdge(1)) patPhase = 0;
		if (clk)
		{
			// Ratchets need to know how long a step is. With an external clock
			// that means the gap since the last one; sanity-bounded so a first
			// edge or a stopped clock cannot schedule nonsense.
			uint32_t gap = sampleCtr - lastClockAt;
			if (lastClockAt && gap > 240 && gap < 480000) clockGap = gap;
			lastClockAt = sampleCtr;
			doStep();
		}

		if (ratchetLeft && --ratchetTimer == 0)
		{
			ratchetTimer = ratchetPeriod;
			ratchetLeft--;
			if (stepMetalOn) { env  = ENV_MAX; penv = ENV_MAX; }
			if (stepKickOn)  { benv = ENV_MAX; kenv = ENV_MAX; }
			trigTimer = PULSE_LEN;
			updateVoice();               // re-apply the attack pitch blip
		}

		// ---- envelopes, at 6kHz: fine for a decay, and eight times cheaper ----
		if ((sampleCtr & 7) == 0)
		{
			env  -= ((env  * decRate) >> 16) + 1; if (env  < 0) env  = 0;
			// ~25ms, not the ~4ms it was. A 4ms pitch drop is a click; stretched
			// out it reads as a thump, and it holds the body in the mix long
			// enough to be a drum rather than an attack transient.
			penv -= ((penv * 450)     >> 16) + 1; if (penv < 0) penv = 0;
			// ~270ms. 75ms is a house kick; industrial techno wants it ringing
			// on well past the metal. It still tracks the Decay knob, at a
			// quarter of its rate, so winding Decay down shortens the kick too
			// rather than leaving a boom trailing behind a tick.
			int32_t bRate = clampi(decRate >> 2, 40, 4000);
			benv -= ((benv * bRate)   >> 16) + 1; if (benv < 0) benv = 0;
			kenv -= ((kenv * 450)     >> 16) + 1; if (kenv < 0) kenv = 0;
		}

		// ---- the metal bank ----
		// XOR all six together rather than summing them. Six summed squares is
		// six discrete pitches — a chord, which is exactly why it sounded like a
		// videogame. XOR is a square-wave ring modulator: every oscillator beats
		// against every other, and the sum-and-difference products smear the
		// spectrum into something dense and inharmonic, which is what metal is.
		// The output is two-level, but the envelope in front of the overdrive
		// gives the drive plenty to bite on across the decay.
		uint32_t bits = 0;
		for (int i = 0; i < 6; i++) { phase[i] += inc[i]; bits ^= phase[i] >> 31; }
		int32_t cloud = bits ? 2040 : -2040;

		// Metal crossfades the bare fundamental against that cloud. Pure cloud
		// has no pitch at all — which is the point of the top of the knob, but
		// leaves nothing to tune. One clean partial underneath puts the pitch
		// back the way a bell has one: a strong fundamental in a cloud of
		// inharmonic partials, rather than the chord of six that started this.
		// Triangle, not square. A square fundamental carries strong odd
		// harmonics that sit right in the mid band and rattle; a triangle puts
		// the energy in the fundamental itself, which is what thump is.
		int32_t saw  = (int32_t)(phase[0] >> 16) - 32768;
		int32_t fund = ((saw < 0 ? -saw : saw) - 16384) >> 3;

		// The attack leans the mix onto the fundamental, so a hit lands with a
		// pitch and opens out into the cloud rather than being all cloud from
		// the first sample. (Leaning it the other way is the physically honest
		// choice — high partials damp fastest on real metal — but measured
		// brighter and thinner right where the ear judges the hit, so: no.)
		int32_t cm = cloudMix - ((penv * (cloudMix >> 1)) >> 16);
		int32_t metal = clampi(xfade(fund, cloud, cm), -2048, 2047);

		bodyPhase += bodyInc;

		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		int32_t src = extNoise ? (int32_t)AudioIn1() : ((int32_t)(rng & 0xFFF) - 2048);

		int32_t bus = xfade(metal, src, stepNoise);
		bus = ((bus * env) >> 16) * stepLevel >> 8;
		bus = clampi(bus, -2048, 2047);

		// ---- rust: downsample, then overdrive ----
		dsPhase += dsInc;
		if (dsPhase < dsInc) dsHold = bus;      // the accumulator wrapped: sample

		// Drive into the Pade shaper, then the makeup gain that puts full scale
		// back at full scale, so Rust changes the character and not the level.
		int32_t xg = clampi((dsHold * drive) >> 8, -PADE_MAX, PADE_MAX);
		int32_t q  = (xg * xg) >> 11;
		int32_t y  = (xg * (q + PADE_K)) / (9 * q + PADE_K);
		AudioOut(0, (int16_t)clampi((y * makeup) >> 12, -2048, 2047));

		// The kick, on its own jack. Never through the drive: run through it, a
		// near-full-scale 50Hz sine just gets its peaks flattened, which is a
		// buzz rather than a kick.
		//
		// A sine, not a triangle. A triangle at 50Hz puts real energy at 150 and
		// 250Hz, which you hear as buzz rather than feel as weight; a sine keeps
		// all of it on the fundamental, which is what moves air.
		//
		// Having its own output is what lets it run to full scale — sharing one
		// jack with the metal meant both had to give up headroom, and a master
		// saturator had to sit across the sum to catch the peaks. Neither is
		// needed now, so both jacks are louder and cleaner than the mix was.
		int32_t body = sinLut[bodyPhase >> 22];
		int32_t bAmt = (((benv * BODY) >> 16) * stepLevel) >> 8;
		int32_t bsig = (body * bAmt) >> 8;
		AudioOut(1, (int16_t)clampi(bsig, -2048, 2047));

		if (trigTimer)  trigTimer--;
		if (ghostTimer) ghostTimer--;
		PulseOut(0, trigTimer > 0);
		PulseOut(1, ghostTimer > 0);

		CVOut(0, (int16_t)stepCV);
		CVOut(1, (int16_t)((env * 2047) >> 16));
	}
};

// The hardware entry point. Skipped by the host simulator (sim/), which supplies
// its own main() and a mock ComputerCard.
#ifndef COMPUTERCARD_HOST_SIM
int main()
{
	set_sys_clock_khz(192000, true);

	static Slag slag;
	slag.EnableNormalisationProbe();
	slag.Run();
}
#endif
