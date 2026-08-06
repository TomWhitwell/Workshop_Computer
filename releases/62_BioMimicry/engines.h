// engines.h — the five BioMimicry physics engines.
//
// Each is a self-contained model with no allocation and no shared state; the
// card owns one instance of each and points at the active one.

#pragma once
#include "biomimicry.h"

namespace bio {

/// Mode 1 — one stride clock driving four hooves at real equine phase offsets.
/// Knob Main picks the gait (walk / trot / canter / gallop) and the stride rate
/// within that gait's own band. Chaos jitters each hoof's timing without
/// breaking the gait's phase relationships.
class HorsesEngine : public Engine
{
public:
	void reset(uint32_t seed) override;
	void tick(const Ctrl &c, EngineOut &out) override;
private:
	// Population is a HERD, not a leg count: each horse keeps all four hooves and
	// gets its own stride clock, drifting slightly against the others the way
	// animals travelling together never quite match step. Amputating hooves as
	// the knob came down was the old behaviour and it only ever made one lame
	// horse — turning the knob up added nothing at all.
	uint32_t stride_[kNumAgents];     // one stride clock per horse
	int32_t  speed_[kNumAgents];      // per-horse rate offset, Q16 around 1.0
	// How closely the herd is in step, Q16, smoothed. global used to be the lead
	// horse's raw stride phase - a ramp on a CV out that every other mode uses
	// for a level.
	int32_t  herdSync_;
	// Per-horse footfall envelope, Q16: up when that animal plants a hoof, down
	// between. state[] used to be the raw stride PHASE, a sawtooth on a CV out
	// that carries a level in every other mode.
	int32_t  hoofEnv_[kNumAgents];
	int32_t  jitter_[kNumAgents][kNumAgents];  // [horse][hoof] timing offset
	uint8_t  lastStep_[kNumAgents][kNumAgents];
	// Countdown for the second foot of a "simultaneous" pair, so trot and
	// canter flam instead of summing two clops into one.
	int16_t  pending_[kNumAgents][kNumAgents];
	uint8_t  lastGait_;
	uint32_t rng_;
};

/// Mode 2 — cascading probability network. A node firing raises every other
/// node's probability via a decaying excitation, so one honk sets off the flock.
/// A refractory period after each fire keeps the cascade from self-sustaining.
class GeeseEngine : public Engine
{
public:
	void reset(uint32_t seed) override;
	void tick(const Ctrl &c, EngineOut &out) override;
private:
	// Runs kSwarmSize birds folded onto kNumAgents outputs — four geese never
	// sounded like a flock.
	int32_t  excite_[kSwarmSize];     // Q16 incoming excitation
	uint16_t refractory_[kSwarmSize]; // control ticks remaining
	uint32_t rng_;
};

/// Mode 3 — Kuramoto coupled oscillators. Each agent pulls the others toward
/// its phase with strength K; Knob Main inversely sets K, sweeping from locked
/// metronomic sync to fully independent chaos.
class FrogsEngine : public Engine
{
public:
	void reset(uint32_t seed) override;
	void tick(const Ctrl &c, EngineOut &out) override;
private:
	// A pond of kSwarmSize frogs folded onto the four outputs. Four voices could
	// lock or scatter but never sounded like a chorus; twelve do, and the
	// Kuramoto order parameter is far more legible with a real population.
	uint32_t phase_[kSwarmSize];
	int32_t  natural_[kSwarmSize];  // per-frog natural frequency increment
	// Per-agent croak envelope, Q16: rises when that group calls and decays
	// between. state[] used to carry the lead frog's raw PHASE, which is a
	// sawtooth rather than anything about the pond, and every other engine puts
	// a level there.
	int32_t  croak_[kNumAgents];
	uint32_t clockPhase_;           // phantom oscillator tracking Pulse In 2
	uint32_t rng_;
};

/// Mode 4 — leaky integrate-and-fire buckets. Noise fills each bucket, a fixed
/// leak drains it, and crossing the threshold fires and empties it. Staggered
/// thresholds and leak rates stop the four from locking together.
class RainEngine : public Engine
{
public:
	void reset(uint32_t seed) override;
	void tick(const Ctrl &c, EngineOut &out) override;
private:
	int32_t  level_[kNumAgents];    // Q16 accumulator
	uint32_t rng_;
};

/// Mode 5 — inhomogeneous Poisson process. A single hidden random-walk LFO sets
/// a shared firing density, so all agents cluster and fall silent together.
class MeteorsEngine : public Engine
{
public:
	void reset(uint32_t seed) override;
	void tick(const Ctrl &c, EngineOut &out) override;
private:
	int32_t  density_;      // Q16, the slewed "debris field" level
	int32_t  target_;       // Q16, current random-walk target
	uint16_t untilNewTarget_;
	uint32_t rng_;
};

/// Mode 6 — density-dependent chorus. Where Geese couple on excitation and Frogs
/// couple on phase, cicadas couple on AMPLITUDE: how loud the field already is
/// drives how fast each insect calls, and that feedback loop is what produces
/// the slow pulsing waves of loudness a real cicada field makes. Positive
/// feedback swells the chorus, fatigue drags it back down, and it breathes.
class CicadasEngine : public Engine
{
public:
	void reset(uint32_t seed) override;
	void tick(const Ctrl &c, EngineOut &out) override;
private:
	uint32_t phase_[kSwarmSize];    // each insect's call cycle
	int32_t  fatigue_[kSwarmSize];  // Q16, rises with calling, forces a rest
	// Per-insect constitution. Without these every insect tired and recovered at
	// exactly the same rate, so the field locked into a metronomic pulse —
	// clusters every 0.32s with 0.01s of variance, which reads as a gallop
	// rather than a cicada field. Real insects differ, and that spread is what
	// keeps the swelling irregular.
	uint8_t  recover_[kSwarmSize];  // fatigue decay shift, 11..13
	int32_t  stamina_[kSwarmSize];  // Q16 scale on how fast fatigue accrues
	// Per-insect NATURAL RATE. This is the one that matters: with a single
	// shared rate law the twelve phases converged to R=0.88 within five seconds
	// and stayed there, so every insect called at the same instant and the
	// field pulsed like a gallop. Detuning them is what keeps a chorus a chorus.
	int32_t  tempo_[kSwarmSize];    // Q16 multiplier on the base call rate

	// PATCHES. One shared field made the whole model a relaxation oscillator:
	// everyone recovers, everyone calls, everyone tires, repeat — one period,
	// which is why it read as a gallop. Splitting the field into patches that
	// hear mostly themselves lets them swell out of step with each other, and
	// that is what makes the swelling irregular rather than metronomic.
	static constexpr int kPatches = 4;
	static constexpr int kPerPatch = kSwarmSize / kPatches;
	int32_t  patchField_[kPatches]; // Q16 local loudness
	int32_t  field_;                // Q16 mean across patches, for the CV out
	// A footstep in the grass, Q16, decaying. SEPARATE from fatigue_ because the
	// two are different things: fatigue slows a tired insect to a quarter rate
	// and must stay that way, or the swells stop breathing. A startle stops the
	// field dead. Overloading fatigue_ for both could only ever give one depth.
	int32_t  startle_;
	uint32_t rng_;
};

} // namespace bio
