#pragma once
#include <cstdint>
#include "Choir.h"
#include "Config.h"
#include "Dsp.h"
#include "Leds.h"
#include "Panel.h"
#include "Plate.h"
#include "Piano.h"
#include "Params.h"
#include "Random.h"
#include "Scheduler.h"
#include "SmoothRandom.h"
#include "Surface.h"
#include "Tables.h"

namespace ab
{

// Everything core 1 does once per block: read the panel, run the scheduler,
// render the voices into the block, and fill in the block's settings for
// core 0 (gains, CV and pulse outs). Also the LEDs.
class Control
{
public:
	void Seed(uint32_t seed)
	{
		rng_.Seed(seed);
		// Each CV out has its own generator, so the two never move together.
		cvRng_[0].Seed(rng_.Next());
		cvRng_[1].Seed(rng_.Next());
		surface_.Seed(rng_.Next());
		cv_[0].Configure(kCv1MinMv, kCv1MaxMv, kCvIntervalMinBlocks, kCvIntervalMaxBlocks, kCvStageBlocks);
		cv_[1].Configure(kCv2MinMv, kCv2MaxMv, kCvIntervalMinBlocks, kCvIntervalMaxBlocks, kCvStageBlocks);
	}

	// One block: fill `b` (settings and 32 frames). led[] gets the LEDs.
	void Tick(const Status &s, Block &b, int32_t led[6])
	{
		blocks_++;
		entropy_ = entropy_ * 31u + s.noise;
		if (!started_)
		{
			Silence(b);
			for (int i = 0; i < 6; i++)
				led[i] = 0;
			if (blocks_ < (uint32_t)kSettleBlocks)
				return;
			panel_.Start(s);
			scheduler_.Opening(now_);
			for (int i = 0; i < kLoops; i++)
				panNow_[i] = scheduler_.Pan(i) * 256;
			started_ = true;
		}

		if (panel_.Tick(s))
			AskForNewPiece();

		UpdatePulses(b); // count down before this block's notes start new ones
		GlidePans();
		// The old piece plays on under its fade, as a record fades out.
		if (flushLeft_ == 0)
			scheduler_.Tick(now_, panel_.Linear(Panel::kDrift), rng_, [this](const Scheduler::Note &n) { Play(n); });
		Render(b);
		Between(b);
		UpdateCvs(b);
		b.pulse[0] = triggerLeft_ > 0;
		b.pulse[1] = gateLeft_ > 0;

		int32_t choirLed[2];
		choir_.Loudness(gainL_, gainR_, choirLed);
		choirLed[0] = (choirLed[0] * fadeQ12_) >> 12;
		choirLed[1] = (choirLed[1] * fadeQ12_) >> 12;
		leds_.Update(panel_, choirLed, cvMv_, led);
		MixSettings(b);
		b.volume = (b.volume * fadeQ12_) >> 12; // the master fade between pieces
		now_ += kBlock;
	}

	// Every voice sounding at once, as loud as they come: the worst case,
	// for the cycle bench and tests.
	void BenchFill()
	{
		for (int i = 0; i < Piano::kVoices; i++)
			piano_.NoteOn(45 + i * 4, i % kLoops, 4096);
		for (int i = 0; i < Choir::kVoices; i++)
			choir_.NoteOn(52 + i * 4, i, 4096, i & 1, 4096, rng_);
	}

	const Panel &GetPanel() const { return panel_; }			// for tests
	const Scheduler &GetScheduler() const { return scheduler_; } // for tests
	uint32_t Pieces() const { return pieces_; }					// new pieces asked for, for tests
	bool Fading() const { return fadeLeft_ > 0 || flushLeft_ > 0; } // between pieces, for tests
	int32_t FadeQ12() const { return fadeQ12_; }				// for tests
	uint32_t PianoNotes() const { return pianoNotes_; }			// for tests
	uint32_t ChoirNotes() const { return choirNotes_; }			// for tests
	const Piano &GetPiano() const { return piano_; }			// for tests
	const Choir &GetChoir() const { return choir_; }			// for tests
	int32_t LowestNote(int layer) const { return lowest_[layer]; }	 // for tests
	int32_t HighestNote(int layer) const { return highest_[layer]; } // for tests
	int32_t PanGainL(int i) const { return gainL_[i]; }			// for tests
	int32_t PanGainR(int i) const { return gainR_[i]; }			// for tests

private:
	void Silence(Block &b)
	{
		b.dryGain = b.wetGain = b.volume = 0;
		b.flush = false;
		b.cvMv[0] = cvMv_[0];
		b.cvMv[1] = cvMv_[1];
		b.pulse[0] = b.pulse[1] = false;
		for (int i = 0; i < kBlock; i++)
			b.frame[i] = Frame{};
	}

	// A tap down (or Pulse In 1): the LEDs sweep at once, and everything
	// you hear fades out over 15s -- the old piece playing on underneath,
	// and its voices, echo, reverb, hiss and audio inputs all brought down
	// together, like pulling down a master fader. With the output silent, the voices stop and core
	// 0 empties the echo and reverb (the flush), so nothing of the old piece
	// is left. Then the new piece starts, and the output comes back up over
	// 250ms. A tap while it's fading or flushing changes nothing: the new
	// piece is picked when the flush ends. A tap during the fade back in
	// starts a fade out from wherever it has got to.
	void AskForNewPiece()
	{
		pieces_++;
		leds_.NewPiece();
		if (fadeLeft_ > 0 || flushLeft_ > 0)
			return;
		// Both fades are squared, so this picks up exactly where the fade in
		// had got to.
		fadeLeft_ = fadeInLeft_ > 0 ? ((kFadeInBlocks - fadeInLeft_) * kNewPieceFadeBlocks) / kFadeInBlocks + 1
									: kNewPieceFadeBlocks;
		fadeInLeft_ = 0;
	}

	// One block of the time between pieces: the fade out, the flush, the
	// fade back in. Sets fadeQ12_, the master fade, and the block's flush.
	void Between(Block &b)
	{
		b.flush = false; // blocks go round the queue: clear last time's
		if (fadeLeft_ > 0)
		{
			int32_t left = (fadeLeft_ * 4096) / kNewPieceFadeBlocks;
			fadeQ12_ = (left * left) >> 12;
			if (--fadeLeft_ == 0)
			{
				// Silent: stop every voice, and empty the echo and reverb.
				piano_.Clear();
				choir_.Clear();
				flushLeft_ = kFlushBlocks;
			}
		}
		else if (flushLeft_ > 0)
		{
			fadeQ12_ = 0;
			b.flush = true;
			if (--flushLeft_ == 0)
			{
				NewPiece();
				fadeInLeft_ = kFadeInBlocks;
			}
		}
		else if (fadeInLeft_ > 0)
		{
			int32_t done = ((kFadeInBlocks - fadeInLeft_) * 4096) / kFadeInBlocks;
			fadeQ12_ = (done * done) >> 12;
			fadeInLeft_--;
		}
		else
			fadeQ12_ = 4096;
	}

	void NewPiece()
	{
		// Fresh randomness for every new piece: the jitter in the panel's
		// readings, and the moment of the tap itself.
		rng_.Seed(rng_.Next() ^ entropy_ ^ (uint32_t)now_);
		scheduler_.NewPiece(now_, rng_);
		// Each loop glides to its new stereo position over 0.6s.
		for (int i = 0; i < kLoops; i++)
			panStep_[i] = (scheduler_.Pan(i) * 256 - panNow_[i]) / kPanGlideBlocks;
		panGlide_ = kPanGlideBlocks;
	}

	// One note from the scheduler.
	void Play(const Scheduler::Note &n)
	{
		lowest_[n.layer] = n.note < lowest_[n.layer] ? n.note : lowest_[n.layer];
		highest_[n.layer] = n.note > highest_[n.layer] ? n.note : highest_[n.layer];
		if (n.layer == kPiano)
		{
			pianoNotes_++;
			// Each strike a little different in weight: 70..100%.
			piano_.NoteOn(n.note, n.loop, rng_.Between(2867, 4096));
			// Pulse Out 1: a 10ms trigger -- unless one started within the
			// last 15ms, so the pulse always drops low between triggers.
			if (sinceTrigger_ >= kTriggerMergeBlocks)
			{
				triggerLeft_ = kTriggerBlocks;
				sinceTrigger_ = 0;
			}
			// Pulse Out 2: high until 1.5s (/ tempo) after the last note.
			gateLeft_ = (kGateBlocks * 4096) / scheduler_.TempoQ12();
			// The top LEDs: by the loop's pan (power shares, Q12).
			leds_.Strike((gainL_[n.loop] * gainL_[n.loop]) >> 18, (gainR_[n.loop] * gainR_[n.loop]) >> 18);
		}
		else
		{
			choirNotes_++;
			// Mostly "ah", sometimes "oo"; each note a little different in
			// loudness, and its vibrato starting at its own point.
			int32_t vowel = rng_.Chance(1638) ? 1 : 0;
			choir_.NoteOn(n.note, n.loop, rng_.Between(3482, 4096), vowel, scheduler_.TempoQ12(), rng_);
		}
	}

	// The voices, into this block's frames.
	void Render(Block &b)
	{
		int32_t pianoL[kBlock] = {}, pianoR[kBlock] = {};
		int32_t choirL[kBlock] = {}, choirR[kBlock] = {};
		int32_t bend = Tape();
		piano_.Render(pianoL, pianoR, gainL_, gainR_, bend);
		choir_.Render(choirL, choirR, gainL_, gainR_, bend);
		int32_t noiseL[kBlock], noiseR[kBlock];
		surface_.Render(panel_.Linear(Panel::kTape), noiseL, noiseR);

		int32_t pg = panel_.Squared(Panel::kPiano);
		int32_t cg = panel_.Squared(Panel::kChoir);
		for (int i = 0; i < kBlock; i++)
		{
			int32_t pl = (pianoL[i] * pg) >> 12, pr = (pianoR[i] * pg) >> 12;
			int32_t cl = (choirL[i] * cg) >> 12, cr = (choirR[i] * cg) >> 12;
			Frame &f = b.frame[i];
			f.dryL = (int16_t)Sat16(pl + cl);
			f.dryR = (int16_t)Sat16(pr + cr);
			f.pianoL = (int16_t)Sat16(pl);
			f.pianoR = (int16_t)Sat16(pr);
			f.noiseL = (int16_t)Sat16(noiseL[i]);
			f.noiseR = (int16_t)Sat16(noiseR[i]);
		}
	}

	// Tape: a slow 0.23Hz wow and a 5.7Hz flutter bend every voice's pitch
	// together, by up to 11 and 2.2 cents at full Tape. Returns the bend as
	// a fraction of the pitch, Q16.
	int32_t Tape()
	{
		wowPhase_ += kWowInc;
		flutterPhase_ += kFlutterInc;
		int32_t tape = panel_.Linear(Panel::kTape);
		int32_t wow = (Sine(wowPhase_) * kWowQ16) >> 15;
		int32_t flutter = (Sine(flutterPhase_) * kFlutterQ16) >> 15;
		return ((wow + flutter) * tape) >> 12;
	}

	// 11 cents: 2^(11/1200) - 1 = 0.00637; 2.2 cents: 0.00127 (Q16).
	static constexpr int32_t kWowQ16 = 418;
	static constexpr int32_t kFlutterQ16 = 83;
	static constexpr uint32_t kWowInc = (uint32_t)(4294967296.0 * 0.23 / kBlocksPerSecond);
	static constexpr uint32_t kFlutterInc = (uint32_t)(4294967296.0 * 5.7 / kBlocksPerSecond);
	uint32_t wowPhase_ = 0, flutterPhase_ = 0;

	// Pan positions glide (linearly, 0.6s) after a new piece; the gains
	// follow every block.
	void GlidePans()
	{
		if (panGlide_ > 0)
		{
			panGlide_--;
			for (int i = 0; i < kLoops; i++)
				panNow_[i] = panGlide_ == 0 ? scheduler_.Pan(i) * 256 : panNow_[i] + panStep_[i];
		}
		for (int i = 0; i < kLoops; i++)
			PanGains(panNow_[i] >> 8, gainL_[i], gainR_[i]);
	}

	// The two smooth random CVs, in millivolts. They wander faster in a
	// faster piece.
	void UpdateCvs(Block &b)
	{
		for (int i = 0; i < 2; i++)
		{
			cvMv_[i] = cv_[i].Tick(cvRng_[i], scheduler_.TempoQ12());
			b.cvMv[i] = cvMv_[i];
		}
	}

	// Pulse Out 1: 10ms trigger per piano note. Pulse Out 2: a gate, high
	// until 1.5s (/ tempo) after the last piano note. The choir never
	// touches either: its notes sustain for seconds and would hold the gate
	// high most of the time.
	void UpdatePulses(Block &b)
	{
		(void)b;
		if (triggerLeft_ > 0)
			triggerLeft_--;
		if (sinceTrigger_ < 1000000)
			sinceTrigger_++;
		if (gateLeft_ > 0)
			gateLeft_--;
	}

	// Core 0's mix settings for this block.
	void MixSettings(Block &b)
	{
		// Reverb mix: an equal-power crossfade, so the overall loudness stays
		// put as it moves from dry to wet. Angle = mix x quarter cycle.
		int32_t reverb = panel_.Linear(Panel::kReverb);
		uint32_t angle = (uint32_t)reverb << 18;
		b.wetGain = Sine(angle) >> 3;				 // Q15 -> Q12
		b.dryGain = Sine(angle + 0x40000000u) >> 3; // cosine
		// The macro: past two-thirds, the plate grows longer and darker
		// (Config.h). t: 0..4096 across the last third.
		int32_t t = reverb <= kReverbMacroStart ? 0 : ((reverb - kReverbMacroStart) * 4096) / (4096 - kReverbMacroStart);
		t = t > 4096 ? 4096 : t;
		b.plateDecayQ15 = Plate::kDecayQ15 + (((kReverbMaxDecayQ15 - Plate::kDecayQ15) * t) >> 12);
		b.plateDampQ15 = Plate::kDampQ15 + (((kReverbMaxDampQ15 - Plate::kDampQ15) * t) >> 12);
		b.plateOutQ10 = Plate::kOutQ10 + (((kReverbMaxOutQ10 - Plate::kOutQ10) * t) >> 12);
		b.volume = panel_.Squared(Panel::kVolume);

		// The reverb's two slow swings: 0.3 and 0.37Hz, unrelated so they
		// never line up, +/-16 samples.
		lfoPhase_[0] += kLfoInc0;
		lfoPhase_[1] += kLfoInc1;
		b.lfoQ8[0] = (Sine(lfoPhase_[0]) * Plate::kExcursion) >> 7; // Q15 x 16 -> Q8
		b.lfoQ8[1] = (Sine(lfoPhase_[1]) * Plate::kExcursion) >> 7;
	}

	static constexpr uint32_t kLfoInc0 = (uint32_t)(4294967296.0 * 0.30 / kBlocksPerSecond);
	static constexpr uint32_t kLfoInc1 = (uint32_t)(4294967296.0 * 0.37 / kBlocksPerSecond);
	uint32_t lfoPhase_[2] = {0, 0x40000000u};

	static constexpr int32_t kPanGlideBlocks = BlocksFromMs(600);

	// Layout: small, hot state first.
	int64_t now_ = 0; // the first sample of the block being rendered
	uint32_t blocks_ = 0;
	bool started_ = false;
	int32_t triggerLeft_ = 0, sinceTrigger_ = 1000000, gateLeft_ = 0;
	int32_t panGlide_ = 0;
	// Between pieces (blocks still to go of each stage), and the master fade.
	int32_t fadeLeft_ = 0, flushLeft_ = 0, fadeInLeft_ = 0;
	int32_t fadeQ12_ = 4096;
	int32_t cvMv_[2] = {(kCv1MinMv + kCv1MaxMv) / 2, (kCv2MinMv + kCv2MaxMv) / 2};
	uint32_t entropy_ = 0;
	uint32_t pieces_ = 0, pianoNotes_ = 0, choirNotes_ = 0;
	int32_t lowest_[2] = {127, 127}, highest_[2] = {0, 0}; // notes played, for tests
	int32_t gainL_[kLoops] = {}, gainR_[kLoops] = {}; // Q15
	int32_t panNow_[kLoops] = {};					  // Q8
	int32_t panStep_[kLoops] = {};
	Random rng_;
	Random cvRng_[2];
	SmoothRandom cv_[2];
	Panel panel_;
	Leds leds_;
	Scheduler scheduler_;
	Piano piano_;
	Choir choir_;
	Surface surface_;
};

} // namespace ab
