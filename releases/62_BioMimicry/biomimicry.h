// biomimicry.h — shared types for the BioMimicry card.
//
// Five physics engines drive four agents. Every engine implements the same
// contract: given the control state, advance the model by one control tick and
// return a bitmask of which agents fired.

#pragma once
#include <stdint.h>
#include "fastmath.h"

namespace bio {

/// Number of agents exposed to the outside world: four trigger outputs, four
/// voices, four state CVs.
constexpr int kNumAgents = 4;

/// The flock modes (Geese, Meteors) run more birds internally than they have
/// outputs. Four agents cannot sound like a flock; twelve can. Each internal
/// member is only a probability roll, so the cost is trivial, and members fold
/// down onto the four output channels — a channel fires if ANY of its members
/// fires. Density and overlap read as a swarm rather than as four things.
constexpr int kSwarmSize = 12;
constexpr int kSwarmPerAgent = kSwarmSize / kNumAgents;   // 3

/// ProcessSample() runs at 48kHz; the physics run every kCtrlDiv samples.
/// 48000/32 = 1500Hz control rate — 0.67ms timing granularity, far finer than
/// the ear resolves for triggers, and 32x cheaper than running physics at
/// audio rate.
constexpr int   kCtrlDiv  = 32;
constexpr int   kSampleRate = 48000;
constexpr int   kCtrlRate = kSampleRate / kCtrlDiv; // 1500 Hz

/// The six ecosystems, in cycling order.
enum class Mode : uint8_t
{
	Horses = 0,
	Geese,
	Frogs,
	Rain,
	Meteors,
	Cicadas,
	Count
};
constexpr int kNumModes = static_cast<int>(Mode::Count);

/// Gate routing, selected by the toggle position.
enum class Routing : uint8_t
{
	Discrete, // Switch Up:     agents -> individual outputs
	Summed    // Switch Middle: agents OR'd -> Pulse 1, CV outs carry state
};

/// Chosen by holding the momentary switch at power-on. Same six engines, two
/// completely different instruments made out of them.
enum class BootMode : uint8_t
{
	Rhythm,   // normal: the physics fire discrete triggers and one-shot voices
	Drone     // alt:    the physics drive continuous tone, an ambient counterpart
};

/// Control state, resampled once per control tick and handed to the engine.
/// All the Q16 fields are 0..65536.
struct Ctrl
{
	int32_t physics;     // Knob Main (+ CV In 1): the per-mode physics variable
	int32_t chaos;       // Knob Y: global randomness / spread
	int     population;  // Knob X (+ CV In 2): 1..kNumAgents active agents
	bool    spook;       // Pulse In 1 rising edge this tick
	bool    clock;       // Pulse In 2 rising edge this tick
	int32_t clockPeriod; // control ticks between the last two Pulse In 2 edges,
	                     // 0 if no clock is running. Lets an engine entrain to
	                     // an external tempo rather than just being nudged.
	int32_t loudness;    // Q16 envelope of Audio In 1: how loud the room is.
	                     // A disturbed environment - it quietens the shy modes
	                     // and agitates the reactive ones. 0 when unpatched.
};

/// What an engine produces each control tick.
struct EngineOut
{
	uint8_t triggers;              // bit i set = agent i fired this tick
	int32_t state[kNumAgents];     // Q16 0..65536, continuous internal state,
	                               // exposed on the CV outs in Summed routing
	int32_t global;                // Q16, a whole-ecosystem value (density,
	                               // sync coherence, ...) for CV Out 2

	/// Which sub-member of each agent fired, when the mode has a meaningful one:
	/// in Horses this is WHICH HOOF (0=LH 1=LF 2=RH 3=RF), so the voice can play
	/// that hoof's own sound. Engines that have no such distinction leave it 0
	/// and the voice falls back to its own round robin.
	uint8_t member[kNumAgents];
};

/// Common base for the five engines. Virtual dispatch happens once per control
/// tick (1500Hz), not per sample, so the indirect call is irrelevant.
class Engine
{
public:
	virtual ~Engine() {}

	/// Re-seed / reset to a sane starting state. Called on mode change.
	virtual void reset(uint32_t seed) = 0;

	/// Advance one control tick.
	virtual void tick(const Ctrl &c, EngineOut &out) = 0;
};

} // namespace bio
