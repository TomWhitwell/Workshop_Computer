#pragma once
#include <cstdint>
#include "StereoBuffer.h"
#include "Params.h"

namespace imp
{

// The audio half of the reslice engine (runs on core 0, every sample).
//
// Three layers (playheads) loop over the same 0.75-second buffer. Each layer
// cuts its part of the buffer into slices and plays them back in the order
// given by the live *slice map*: for every slot, which slice, forwards or
// backwards, at what pitch, with what small offset.
//
// Slice maps are made on core 1 (Control.h), only when Chaos moves, and
// core 1 also turns each map into the per-slot start frames and rates this
// class plays from. Between re-cuts everything here is deterministic: a
// still knob repeats exactly.
//
// Texture mode: each layer free-runs through its slots; each slot is a
// grain that crossfades into the next. At low chaos, slices play in order
// and the crossfades line up exactly, so you hear the plain loop.
//
// Rhythm mode: slots advance on Pulse In 2 (or an internal clock); each
// slot is a short percussive hit, and the layers' different slot counts
// make their cycles interlock.
//
// Timing: this runs inside the 48kHz audio interrupt, so every path is
// short and fixed -- no divisions (core 1 precomputes every envelope step)
// and no loops longer than the six voices.
class SliceEngine
{
public:
	explicit SliceEngine(const StereoBuffer &buffer) : buffer_(buffer) {}

	// Pulse In 2, every sample: `rise` is a rising edge, `connected` is
	// ComputerCard's jack detection.
	//
	// Why edges aren't used straight away: to detect an empty jack,
	// ComputerCard feeds a random bit pattern into every unpatched input and
	// waits until the input has matched it for 32 bits -- up to ~11ms. So for
	// ~11ms after a cable is pulled out, Pulse In reads that random pattern:
	// a burst of fake edges. Each edge therefore waits kConfirmDelay (20ms);
	// if the jack is reported empty before then, it's thrown away.
	//
	// So that hits still land on time, the Rhythm clock isn't the edges
	// themselves but an internal beat, locked to the confirmed edges' tempo
	// and phase (each confirmed edge schedules the *next* beat exactly one
	// period after it). Only the very first beat of a new clock is late. When
	// the cable comes out -- or the clock stops -- the beat simply carries
	// on at the last tempo, and so does the Texture loop speed. Clearing the
	// buffer (Main fully anticlockwise) forgets the tempo: ResetClock().
	void ClockInput(bool rise, bool connected)
	{
		now_++;
		if (sinceBeat_ < kMaxClockPeriod)
			sinceBeat_++;
		if (!connected)
		{
			edgeCount_ = 0;
			haveLastEdge_ = false; // a replugged clock starts a fresh measurement
		}
		else if (rise && edgeCount_ < kMaxEdges)
		{
			edges_[(edgeHead_ + edgeCount_) & (kMaxEdges - 1)] = now_;
			edgeCount_++;
		}
		while (edgeCount_ > 0 && now_ - edges_[edgeHead_] >= kConfirmDelay)
		{
			uint32_t t = edges_[edgeHead_];
			edgeHead_ = (edgeHead_ + 1) & (kMaxEdges - 1);
			edgeCount_--;
			ConfirmEdge(t);
		}
		if (clockHeld_ && --beatCountdown_ <= 0)
		{
			Beat();
			beatCountdown_ = clockPeriod_;
		}
	}

	void ResetClock()
	{
		clockHeld_ = false;
		haveLastEdge_ = false;
		edgeCount_ = 0;
	}

	int32_t ClockPeriod() const { return clockPeriod_; }
	bool ClockActive() const { return clockHeld_; }

	// One sample of output, 12-bit scale (may exceed +/-2047 before the
	// card's soft clip). Returns a bit per layer that wrapped back to its
	// first slot this sample (bit 0 = layer 1).
	uint32_t Process(const EngineParams &p, int32_t &outL, int32_t &outR)
	{
		if (p.epoch != epoch_)
		{
			epoch_ = p.epoch;
			Adopt(p);
		}
		cycleEnds_ = 0;

		if (p.rhythm)
		{
			bool trigger = beatDue_;
			if (!clockHeld_ && ++internalCount_ >= p.internalPeriod)
				trigger = true;
			if (trigger)
			{
				internalCount_ = 0;
				for (int l = 0; l < kLayers; l++)
				{
					Advance(p.layer[l], l);
					Request(l, kRequestRhythm);
				}
			}
		}
		else
		{
			for (int l = 0; l < kLayers; l++)
			{
				Layer &layer = layer_[l];
				const LayerParams &lp = p.layer[l];
				layer.slotAcc += (uint32_t)p.slotSpeed;
				if (layer.slotAcc >= lp.slotQ)
				{
					layer.slotAcc -= lp.slotQ;
					if (layer.slotAcc >= lp.slotQ)
						layer.slotAcc = 0;
					Advance(lp, l);
					Request(l, kRequestTexture);
				}
			}
		}
		beatDue_ = false;
		ServeRequests(p);

		int32_t o0[2], o1[2], o2[2];
		ProcessLayer(layer_[0], buffer_, recLength_, p.layer[0].level, o0);
		ProcessLayer(layer_[1], buffer_, recLength_, p.layer[1].level, o1);
		ProcessLayer(layer_[2], buffer_, recLength_, p.layer[2].level, o2);
		// Layer 2 plays with its channels swapped, for width. Three layers
		// at full level would overload, so scale the sum by 5/8.
		outL = ((o0[0] + o1[1] + o2[0]) * 5) >> 3;
		outR = ((o0[1] + o1[0] + o2[1]) * 5) >> 3;
		return cycleEnds_;
	}

private:
	struct Grain
	{
		int32_t start; // frame
		int32_t rate;  // Q12 frames per sample, negative = reverse
		int32_t hold, attackStep, releaseStep;
	};

	struct Voice
	{
		int32_t pos = 0;  // Q12 frame position
		int32_t rate = 0; // Q12
		int32_t env = 0;  // Q16, 0..65536
		int32_t attackStep = 0, releaseStep = 0;
		int32_t holdLeft = 0; // samples left before the fade out starts
		int32_t stage = kOff;
		bool nearest = false; // x2 and faster: plain frame reads (see ProcessLayer)
	};

	struct Layer
	{
		Voice voice[2];
		bool pending = false; // a grain waiting for a voice to fade out
		int pendingVoice = 0;
		Grain pendingGrain{};
		int32_t slot = 0;
		uint32_t slotAcc = 0; // Q12 source frames played in this slot
		int32_t request = kRequestNone; // a grain waiting to be started...
		int32_t requestDelay = 0;		// ...in this many samples
	};

	static constexpr int32_t kOff = 0, kOn = 1;
	static constexpr int32_t kRequestNone = 0, kRequestTexture = 1, kRequestRhythm = 2;
	static constexpr int32_t kMinClockPeriod = 240;	   // 5ms (200Hz)
	static constexpr int32_t kMaxClockPeriod = 192000; // 4s; slower isn't a clock
	static constexpr uint32_t kConfirmDelay = 960;	   // 20ms: > the ~11ms unplug detection
	static constexpr int32_t kMaxEdges = 8;			   // power of two
	static constexpr int32_t kStealFadeShift = 8;	   // reused voice fades over 256 samples (~5ms)

	void ConfirmEdge(uint32_t t)
	{
		if (haveLastEdge_)
		{
			uint32_t d = t - lastEdge_;
			if (d >= (uint32_t)kMinClockPeriod && d <= (uint32_t)kMaxClockPeriod)
			{
				clockPeriod_ = (int32_t)d;
				clockHeld_ = true;
			}
		}
		lastEdge_ = t;
		haveLastEdge_ = true;
		if (!clockHeld_)
		{
			beatDue_ = true; // the first edge of a new clock: no tempo yet, play it late
			return;
		}
		// If the beat for this edge hasn't happened yet (the clock sped up),
		// play it now, then line the next beat up one period after the edge.
		if (sinceBeat_ * 2 > clockPeriod_)
			Beat();
		int32_t late = (int32_t)(now_ - t);
		beatCountdown_ = clockPeriod_ - late;
		if (beatCountdown_ < 1)
			beatCountdown_ = 1;
	}

	void Beat()
	{
		beatDue_ = true;
		sinceBeat_ = 0;
	}

	// React to a structural change in the parameters (core 1 bumps
	// EngineParams::epoch): a new take length, a new map, a change of mode.
	// These are rare, so the extra work here is fine on the odd sample.
	void Adopt(const EngineParams &p)
	{
		if (p.recLength != recLength_)
		{
			recLength_ = p.recLength;
			int32_t lenQ = recLength_ << 12;
			for (Layer &layer : layer_)
				for (Voice &v : layer.voice)
					if (v.pos >= lenQ)
						v.pos %= lenQ; // at most 6 divisions, once per take
		}
		for (int l = 0; l < kLayers; l++)
			if (layer_[l].slot >= p.layer[l].count)
				layer_[l].slot = 0;

		bool restart = false;
		if (p.reseedSeq != reseedSeq_)
		{
			// A new map: in Texture it takes over now -- the gesture
			// destroying the old loop (old grains fade out underneath).
			// In Rhythm it takes over on the next clock pulse.
			reseedSeq_ = p.reseedSeq;
			restart = !p.rhythm;
		}
		if (p.rhythm != rhythm_)
		{
			rhythm_ = p.rhythm;
			if (rhythm_)
			{
				beatDue_ = true; // first hit straight away
				internalCount_ = 0;
			}
			else
				restart = true;
		}
		if (restart)
		{
			for (int l = 0; l < kLayers; l++)
			{
				layer_[l].slotAcc = 0;
				Request(l, kRequestTexture);
			}
		}
	}

	// Start the grains that are due. Layer 1 starts its grain one sample
	// after it's asked for, layer 2 two samples after, layer 3 three -- so a
	// grain start never lands on the same sample as a slot change or a new
	// map, and never alongside another layer's. When all three want a new grain at once (every Rhythm pulse,
	// every re-cut in Texture), doing them together was the most expensive
	// sample the card had; staggered, the cost is spread over three
	// samples. 63us at most can't be heard, and because each layer's delay
	// is fixed, each layer still repeats exactly on its own cycle.
	void ServeRequests(const EngineParams &p)
	{
		for (int l = 0; l < kLayers; l++)
		{
			Layer &layer = layer_[l];
			if (layer.request == kRequestNone || layer.requestDelay-- > 0)
				continue;
			int32_t r = layer.request;
			layer.request = kRequestNone;
			if (r == kRequestTexture)
				StartTextureGrain(p, l);
			else
				StartRhythmGrain(p, l);
		}
	}

	void Request(int l, int32_t kind)
	{
		layer_[l].request = kind;
		layer_[l].requestDelay = l + 1; // +1: never on the same sample as the event that asked
	}

	void Advance(const LayerParams &lp, int l)
	{
		Layer &layer = layer_[l];
		if (++layer.slot >= lp.count)
		{
			layer.slot = 0;
			cycleEnds_ |= 1u << l;
		}
	}

	// Where a slot's grain starts, and how fast/which way it plays. Core 1
	// worked both out when the map changed (Control::Publish), so this is a
	// lookup and one multiply for the read rate (which a clock does not
	// change -- see EngineParams).
	void Place(const EngineParams &p, int l, Grain &g)
	{
		const LayerParams &lp = p.layer[l];
		int32_t slot = layer_[l].slot;
		g.start = lp.slotStart[slot];
		g.rate = (lp.slotPitch[slot] * p.readRate) >> 12;
	}

	void StartTextureGrain(const EngineParams &p, int l)
	{
		// Fade in, play to the end of the slot, then fade out while the
		// next slot's grain fades in. With in-order slices both grains read
		// the same audio at that moment, so the join is seamless.
		const LayerParams &lp = p.layer[l];
		Grain g;
		Place(p, l, g);
		g.hold = lp.texHold;
		g.attackStep = lp.texAttackStep;
		g.releaseStep = lp.texReleaseStep;
		StartGrain(layer_[l], g);
	}

	void StartRhythmGrain(const EngineParams &p, int l)
	{
		// Percussive: a 1ms attack, then a straight decay to silence.
		Grain g;
		Place(p, l, g);
		g.hold = p.rhyHold;
		g.attackStep = p.rhyAttackStep;
		g.releaseStep = p.rhyReleaseStep;
		StartGrain(layer_[l], g);
	}

	// Give a grain a voice. If both voices are still sounding, the quieter
	// one fades out quickly (~5ms) and the grain starts when it's silent --
	// never a hard cut, so never a click.
	static void StartGrain(Layer &layer, const Grain &g)
	{
		if (layer.pending)
		{
			layer.pendingGrain = g;
			return;
		}
		if (layer.voice[0].stage == kOff)
		{
			Launch(layer.voice[0], g);
			return;
		}
		if (layer.voice[1].stage == kOff)
		{
			Launch(layer.voice[1], g);
			return;
		}
		int quiet = layer.voice[0].env <= layer.voice[1].env ? 0 : 1;
		Voice &v = layer.voice[quiet];
		v.holdLeft = 0; // straight into its fade out
		v.releaseStep = (v.env >> kStealFadeShift) + 1;
		layer.pending = true;
		layer.pendingVoice = quiet;
		layer.pendingGrain = g;
	}

	// Starts arrive already wrapped into the take (Control::Publish).
	static void Launch(Voice &v, const Grain &g)
	{
		v.pos = g.start << 12;
		v.rate = g.rate;
		v.env = 0;
		v.attackStep = g.attackStep;
		v.releaseStep = g.releaseStep;
		v.holdLeft = g.hold;
		v.stage = kOn;
		v.nearest = g.rate >= 8192 || g.rate <= -8192;
	}

	// The voices of one layer. Deliberately not inlined: working from a
	// pointer to just this layer keeps every field a short load away, where
	// inlined into the whole interrupt the Cortex-M0+ (8 fast registers)
	// spent more time fetching addresses than doing audio.
	__attribute__((noinline)) static void ProcessLayer(Layer &layer, const StereoBuffer &buffer,
														int32_t recLength, int32_t level, int32_t out[2])
	{
		int32_t lenQ = recLength << 12;
		int32_t accL = 0, accR = 0;
		for (int k = 0; k < 2; k++)
		{
			Voice &v = layer.voice[k];
			if (v.stage == kOff)
				continue;

			// Read between two frames (linear interpolation), so slowed-down
			// slices stay smooth. One frame is enough when the playhead sits
			// exactly on a frame (unison and octave-up at normal speed), or
			// when it's moving at twice speed or more -- it's skipping frames
			// anyway, and the saving keeps the audio interrupt in budget.
			int32_t pos = v.pos;
			int32_t frac = pos & 4095;
			int32_t sl, sr;
			if (frac == 0 || v.nearest)
				buffer.Read(pos >> 12, sl, sr);
			else
			{
				int32_t l0, r0, l1, r1;
				buffer.ReadPair(pos >> 12, recLength, l0, r0, l1, r1);
				sl = l0 + (((l1 - l0) * frac) >> 12);
				sr = r0 + (((r1 - r0) * frac) >> 12);
			}
			int32_t gain = v.env >> 4; // Q12
			accL += sl * gain;
			accR += sr * gain;

			pos += v.rate;
			if (pos >= lenQ)
				pos -= lenQ;
			else if (pos < 0)
				pos += lenQ;
			v.pos = pos;

			// Envelope: rise (fade in, then flat at full) while hold time
			// remains, then fall (fade out) to silence.
			int32_t env = v.env;
			if (v.holdLeft > 0)
			{
				v.holdLeft--;
				env += v.attackStep;
				if (env > 65536)
					env = 65536;
			}
			else
			{
				env -= v.releaseStep;
				if (env <= 0)
				{
					env = 0;
					v.stage = kOff;
				}
			}
			v.env = env;
		}
		if (layer.pending && layer.voice[layer.pendingVoice].stage == kOff)
		{
			Launch(layer.voice[layer.pendingVoice], layer.pendingGrain);
			layer.pending = false;
		}
		out[0] = ((accL >> 12) * level) >> 12;
		out[1] = ((accR >> 12) * level) >> 12;
	}

	// Small, frequently used state first; the layers after.
	const StereoBuffer &buffer_;
	int32_t recLength_ = StereoBuffer::kFrames;
	uint32_t epoch_ = ~0u; // adopt the very first parameter block
	uint32_t cycleEnds_ = 0;
	bool rhythm_ = false;
	uint32_t reseedSeq_ = 0;

	// Pulse In 2 follower (see ClockInput).
	uint32_t now_ = 0;
	uint32_t edges_[kMaxEdges] = {};
	int32_t edgeHead_ = 0, edgeCount_ = 0;
	uint32_t lastEdge_ = 0;
	bool haveLastEdge_ = false;
	bool clockHeld_ = false;
	int32_t clockPeriod_ = 12000;
	int32_t beatCountdown_ = 0;
	int32_t sinceBeat_ = 0;
	bool beatDue_ = false;
	int32_t internalCount_ = 0;
	Layer layer_[kLayers];
};

} // namespace imp
