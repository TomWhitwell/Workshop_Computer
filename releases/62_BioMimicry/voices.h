// voices.h — the audio side: a pool of voices rendered and placed at 48kHz.
//
// Two backends. The synthesized one is always available; the PCM one plays
// baked-in or uploaded samples.
//
// Holding the momentary switch at power-on selects TUNED mode, which plays those
// recordings at their own pitch: no per-agent body size, no per-hit wobble. The
// card otherwise assumes every sample is a creature and detunes accordingly,
// which is right for a flock and wrong for anything pitched.
//
// Both back-ends use ROUND ROBINS: every trigger picks a variant, and the
// variant means something per mode — which hoof, which bird, which species —
// rather than being decorative randomisation.

#pragma once
#include "biomimicry.h"

namespace bio {

/// Round-robin variants per mode. Eight, because with a flock of twelve geese
/// four recordings repeat audibly — and flash is not the constraint (the full
/// sample set is ~834KB of ~1.9MB free).
///
/// HORSES is the exception: its variants are the four HOOVES, not arbitrary
/// alternates, so it only ever uses the first four slots. See kHoofVariants.
constexpr int kNumVariants = 8;

/// Horses uses exactly one variant per hoof (LH, LF, RH, RF).
constexpr int kHoofVariants = 4;

/// One polyphonic slot, owned by one agent.
struct Voice
{
	// Envelope + tone state (synth backend)
	int32_t  env;        // Q16 amplitude envelope
	uint32_t phase;      // oscillator phase
	uint32_t phase2;     // second oscillator / formant
	uint32_t inc;        // phase increment
	uint32_t inc2;
	int32_t  pitchEnv;   // Q16 pitch-drop envelope (the "clop")
	uint32_t noiseRng;   // per-voice noise source
	int32_t  filt;       // one-pole filter memory for noise colouring
	int32_t  filt2;      // second pole, for the swoosh/honk shapes
	int32_t  bp;         // band-pass state, for resonant chirps
	uint8_t  decayShift; // envelope decay rate
	uint8_t  mode;       // which timbre to render
	uint8_t  variant;    // which round-robin variant this hit is

	// Karplus-Strong delay line (Frogs). 128 samples covers down to ~375Hz,
	// which is the bottom of the ribbit range.
	int16_t  ks[128];
	uint8_t  ksLen;
	uint8_t  ksPos;

	// Stereo placement for THIS hit, Q15 gains. Set at note-on so a mode can
	// place each trigger where its ecosystem would put it.
	int32_t  panL, panR;

	// PCM backend state.
	//
	// Position is split into WHOLE SAMPLES and a Q16 fraction rather than being
	// one Q16 number. A single uint32 Q16 position tops out at 2^32/65536 =
	// 65536 samples, i.e. 1.37 seconds — and meteors_4 (97493) and meteors_5
	// (118596) are longer than that, so the position wrapped back to zero before
	// it ever reached pcmLen. `idx < pcmLen` was then never false, the voice was
	// never freed, and the swoosh looped forever, straight through mode changes.
	const int8_t *pcm;
	uint32_t pcmLen;
	uint32_t pcmIdx;     // whole samples played
	uint32_t pcmFrac;    // Q16 fraction within the current sample
	uint32_t pcmInc;     // Q16 rate

	// Which agent this voice is currently sounding for, or -1 when free. Voices
	// are a POOL now rather than one-per-agent: a flock of twelve folded onto
	// four outputs retriggers the same agent long before its last sample has
	// finished, and with one voice each that cut the sound off mid-body.
	int8_t   agent;

	// Fade-in ramp, Q16, climbing to unity over a couple of ms at note-on.
	int32_t  release;

	// Fade-OUT, for a voice that has been stolen. Counts down from kQ16One; the
	// voice keeps playing its old sample, quieter each sample, and only frees
	// itself when it reaches zero.
	//
	// A fade-in alone cannot fix a steal: the discontinuity is on the OUTGOING
	// voice, which was stopping dead mid-waveform. That is inaudible when the
	// stolen voice was nearly finished and obvious when it was not — and
	// stealing "least left" makes the second case the normal one for modes whose
	// samples are all a similar length, like Geese.
	int32_t  fadeOut;
};

/// Voices in the pool. Two per agent: the swarm modes fold twelve members onto
/// four outputs, so one agent is routinely retriggered while its previous hit is
/// still sounding — Geese can retrigger at ~56ms against samples of 137-222ms.
constexpr int kNumVoices = kNumAgents * 2;

class VoiceBank
{
public:
	/// `tuned` = play samples at their recorded pitch, with no per-agent body
	/// size and no per-hit jitter. The card otherwise assumes every recording is
	/// an animal and detunes accordingly — which is right for a flock and wrong
	/// for anything pitched, where it reads as four out-of-tune copies wobbling.
	void init(bool usePcm, bool tuned = false);

	/// Trigger agent `i`'s voice with the timbre for `m`.
	/// `accent` (Q16) scales level; `variation` (Q16) nudges pitch per hit.
	/// `member` names the sub-member that fired where the mode has one (in
	/// Horses, which hoof); modes without one pass 0 and get a round robin.
	void note(int i, Mode m, int32_t accent, int32_t variation, uint8_t member);

	/// Render one sample of all voices, summed and panned into L/R.
	void render(int active, int16_t &l, int16_t &r);

	/// Which round-robin slot the most recent note used, 0-based, or -1 before
	/// anything has sounded. Alt boot puts this on a CV as 1V/oct, so an external
	/// oscillator or filter can follow WHICH sample the ecosystem just fired.
	int lastSlot() const { return lastSlot_; }

	bool usingPcm() const { return usePcm_; }

private:
	/// Pick the round-robin variant for this hit and place it in the stereo
	/// field, both according to the mode's own logic.
	void selectVariantAndPan(Voice &v, int agent, Mode m, uint8_t member);

	// Twice as many voices as agents, so an overlapping hit usually finds a free
	// one rather than cutting a live one short. ~332 bytes each against ~80KB
	// free, so the cost is RAM we have; the constraint was always CPU, and the
	// core-1 split left core 0 with headroom (Total 2209 of 2604).
	Voice v_[kNumVoices];
	uint8_t lastVariant_[kNumAgents];  // for no-immediate-repeat
	uint32_t rng_ = 0x9E3779B9u;

	// Which slots actually hold audio, resolved ONCE in init() rather than per
	// note-on. VariantCount() and PickUserVariant() each walk all eight slots
	// through the user header in XIP flash, and calling them per trigger cost
	// real cycles in the hot path for an answer that only changes when samples
	// are uploaded — which reboots the card anyway.
	uint8_t variantCount_[kNumModes] = {};
	uint8_t variantSlot_[kNumModes][kNumVariants] = {};

	// Resolved sample pointers, also cached at init. ResolveSample() reads the
	// user header through XIP — magic, version, size, offset — and note() called
	// it per trigger. Four flash reads while core 1 is contending for the same
	// bus is not cheap, and the answer only changes on upload, which reboots.
	const int8_t *sampleData_[kNumModes][kNumVariants] = {};
	uint32_t      sampleLen_[kNumModes][kNumVariants] = {};
	bool  usePcm_ = false;
	bool  tuned_  = false;
	int8_t lastSlot_ = -1;
};

} // namespace bio
