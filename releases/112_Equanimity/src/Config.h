#pragma once
#include <cstdint>

// Every number you might want to change by ear, in one place.
//
// Times are in milliseconds unless the name says otherwise. "Q12" means a
// fraction stored as a whole number out of 4096 (so 4096 = 1.0, 2048 = 0.5),
// the usual way to do fractions on a chip with no floating-point unit.
// Likewise Q15 is out of 32768 and Q24 out of 16777216.

namespace eq
{

// ---- Timing ----------------------------------------------------------------

constexpr int32_t kSampleRate = 48000;
// Core 1 runs the slow "control" work once per millisecond: the tide, note
// loops, drone, triggers, wander and LEDs.
constexpr int32_t kTickSamples = 48;
// The Computer smooths knob and switch readings; give them 100ms to settle
// after power-on before trusting them.
constexpr uint32_t kSettleSamples = 4800;

// ---- The tide --------------------------------------------------------------

// Three slow sines with unrelated periods, summed. Each starts at a random
// phase, so no two power-ons rise and fall the same way.
constexpr int32_t kTidePeriodSeconds[3] = {47, 113, 271};

// New notes at low tide and at high tide (one every this many ms).
constexpr int32_t kBirthEveryMsLowTide = 12000;
constexpr int32_t kBirthEveryMsHighTide = 2500;

// ---- Melody voices and tones ---------------------------------------------------

constexpr int32_t kVoices = 8; // also the number of note loops

// The six tones Main picks with the switch up. Each is the same two-operator
// FM pair -- a modulator wobbling the phase of a carrier -- set up
// differently:
//
//   ratio       modulator frequency / note frequency (x256). Whole-number
//               ratios give harmonic, pitched tones; others (2.76, 3.5) give
//               the inharmonic partials of glockenspiels and bells.
//   octave      octaves above the melody range (glockenspiel and xylophone
//               sound an octave up, as the real instruments do)
//   index       brightness (modulation index, radians x 4096) for a note
//               born at the lowest and the highest tide
//   indexDecay  how fast the brightness dies away (ms to -60dB)
//   ring        how long the note rings (ms to -60dB) at the bottom and the
//               top of the range -- low notes ring longer
//   attack      ms
//   tremolo     the vibraphone's 5Hz wobble
struct Tone
{
	int32_t ratioQ8;
	int32_t octave;
	int32_t indexLowTideQ12, indexHighTideQ12;
	int32_t indexDecayMs;
	int32_t ringMsLow, ringMsHigh;
	int32_t attackMs;
	bool tremolo;
};

constexpr int kTones = 6;
constexpr Tone kTone[kTones] = {
	// ratio  oct  index low/high  idxDecay  ring low/high  att  trem
	{256, 0, 1230, 8192, 250, 2400, 1600, 1, false},	// Harp: quick pluck, gentle, Eno-ish
	{1024, 0, 1640, 10240, 40, 1200, 700, 1, false},	// Marimba: a click of brightness, warm, woody
	{768, 1, 1640, 8192, 30, 600, 350, 1, false},		// Xylophone: short, sharp, drier
	{707, 1, 2048, 10240, 1500, 2800, 1800, 1, false},	// Glockenspiel: bright, rings, sparkly
	{1024, 0, 1230, 4096, 20000, 6000, 4000, 4, true},	// Vibraphone: soft, steady, wavering
	{896, 0, 1640, 14336, 1500, 5000, 3000, 3, false},	// Bell: bright burst, slow fade -- the Bloom sound
};

// The gentle lowpass after each voice: about 1.5kHz for a note born at low
// tide, 9kHz at high tide (one-pole coefficients, Q15).
constexpr int32_t kBellLpLowTideQ15 = 5842;
constexpr int32_t kBellLpHighTideQ15 = 22680;

// One voice at full velocity peaks at a quarter of full scale, leaving room
// for several to ring at once.
constexpr int32_t kVoicePeakQ12 = 1024;

// Vibraphone tremolo: 5Hz, dipping the melody by up to about a third. It
// fades in and out over ~300ms when the tone changes.
constexpr uint32_t kTremoloInc = 447392; // 5Hz as a 32-bit phase step per sample
constexpr int32_t kTremoloDepthQ12 = 1400;

// ---- Note loops ---------------------------------------------------------------

// The melody spans two octaves, from the root in octave 4 (MIDI 60 + root)
// to the root in octave 6.
constexpr int32_t kMelodyBottom = 60;
constexpr int32_t kMelodySpan = 24;
// Time between a loop's repeats, picked at random per loop: wide apart, so
// the melody has space and the delays fill the gaps.
constexpr int32_t kLoopPeriodMinMs = 5000;
constexpr int32_t kLoopPeriodMaxMs = 25000;
constexpr int32_t kRepeatsMin = 4;
constexpr int32_t kRepeatsMax = 10;
// A newborn note's loudness, picked at random between these (Q12).
constexpr int32_t kVelocityMinQ12 = 2870; // 0.7
constexpr int32_t kVelocityMaxQ12 = 4096;
// Each repeat is quieter than the one before by this factor (Q12), chosen
// so the last repeat lands at about 5% (-26dB) of the first, however many
// repeats the loop has. Indexed by the number of repeats.
constexpr int32_t kRepeatFadeQ12[kRepeatsMax + 1] = {0, 0, 0, 0, 1509, 1937, 2250, 2486, 2670, 2817, 2936};
// After Pulse In 2 asks for a new set, the old loops play at most this many
// more repeats, fading faster, and at most kNewSetPeriodMs apart -- so a
// change of set doesn't wait out a 25-second loop.
constexpr int32_t kNewSetRepeats = 2;
constexpr int32_t kNewSetPeriodMs = 9000;

// The changeover to a new set: the delays' feedback falls to nothing over
// kChangeoverDuckMs, so the old key drains out rather than ringing on under
// the new one. Once the old notes have rung out, the card waits
// kChangeoverDrainMs more (the long line is 9.7s), then starts the new set
// and brings the feedback back over kChangeoverRestoreMs.
constexpr int32_t kChangeoverDuckMs = 1500;
constexpr int32_t kChangeoverDrainMs = 10500;
constexpr int32_t kChangeoverRestoreMs = 3000;

// ---- Delays ------------------------------------------------------------------

// Short line: 1.1s at 24kHz, 16-bit. Long line: 9.7s at 8kHz, 8-bit mu-law.
constexpr int32_t kShortDelaySamples = 26400; // 1.1s at 24kHz
constexpr int32_t kLongDelaySamples = 77600;  // 9.7s at 8kHz

// Feedback with X or Y fully clockwise. Never unity: the saturator and
// lowpass in each loop do the rest, so it holds for a long time without
// running away.
constexpr int32_t kFeedbackCeilingQ12 = 3973; // 0.97

// One-pole lowpass coefficients (Q15).
constexpr int32_t kShortInLpQ15 = 23918;   // ~10kHz before dropping to 24kHz
constexpr int32_t kShortLoopLpQ15 = 25956; // ~6kHz in the short feedback loop
constexpr int32_t kLongInLpQ15 = 12044;	   // ~3.5kHz (twice) before dropping to 8kHz
constexpr int32_t kLongOutLpQ15 = 12044;   // ~3.5kHz after reading back
constexpr int32_t kLongLoopLpQ15 = 28168;  // ~2.5kHz in the long feedback loop

// Wet level relative to dry, before the wet/dry crossfade (Q12). Half, so
// both lines at full scale together just reach full scale.
constexpr int32_t kWetLevelQ12 = 2048;

// Overall output level (Q12), after the mix and before the output limiter.
// 1.5x puts typical peaks around +/-3V.
constexpr int32_t kOutputGainQ12 = 6144;

// Tape wobble: how far each read point drifts (ms) and how quickly. The
// drift is a smooth random walk, the same generator as the wander CV.
constexpr int32_t kShortWobbleMs = 3;
constexpr int32_t kLongWobbleMs = 15;
constexpr int32_t kWobbleIntervalMinMs = 1500;
constexpr int32_t kWobbleIntervalMaxMs = 4000;
constexpr int32_t kWobbleStageMs = 700;

// ---- Drone ------------------------------------------------------------------

// The drone is built the way Eno describes his: not one tone but "several
// unstable elements that change in both timbre and volume", each on its
// own long cycle. The cycles have unrelated ("incommensurable") lengths, as
// with the tape loops of Music for Airports, so the layers drift in and out
// of prominence and never line up the same way twice. The chord's colour
// keeps shifting -- root-heavy, open fifth, bright octave -- without the
// drone ever audibly changing note. Its pitch changes only with a new set
// (as Bloom's drone does with each mood).
//
// Four layers above the root in octave 2 (MIDI 36 + root, 65-123Hz): the
// root, the 5th, the octave and the 5th above that. Each is a 1:1 FM pair.
//
//   interval   semitones above the root
//   weight     share of the drone's level at the top of its cycle (Q12);
//              the four add up to about 4096
//   floor      how far it sinks at the bottom of its volume cycle (Q12 of
//              its weight): the root never fades far, so the key holds
//   detune     a few cents off (x65536 / 2^(cents/1200) - 1), so the layers
//              beat gently against each other's harmonics
//   volumeSecs, timbreSecs   the lengths of its two cycles (x10, so 413 is
//              41.3 seconds). All eight are different, and none shares a
//              factor with the others or with the tide's 47 / 113 / 271s.
struct DroneLayer
{
	int32_t interval;
	int32_t weightQ12;
	int32_t floorQ12;
	int32_t detuneQ16;
	int32_t volumeSecsX10;
	int32_t timbreSecsX10;
};

constexpr int kDroneLayers = 4;
constexpr DroneLayer kDroneLayer[kDroneLayers] = {
	// interval  weight floor  detune  volume  timbre
	{0, 1600, 1840, 0, 413, 299},	 // root: the anchor, never below ~45%
	{7, 1100, 300, 114, 537, 373},	 // 5th, +3 cents
	{12, 900, 200, -151, 619, 447},	 // octave, -4 cents
	{19, 500, 0, 190, 711, 331},	 // 5th above the octave, +5 cents, comes and goes
};

constexpr int32_t kDroneBottom = 36;
// How bright each layer gets (FM index, radians x 4096) at the bottom and
// top of its timbre cycle. The top rises with the tide.
constexpr int32_t kDroneIndexMinQ12 = 2048;			 // 0.5
constexpr int32_t kDroneIndexMaxLowTideQ12 = 6144;	 // 1.5
constexpr int32_t kDroneIndexMaxHighTideQ12 = 12288; // 3.0
// Overall drone level at full volume (Q12 of full scale, all layers up).
constexpr int32_t kDronePeakQ12 = 900;
// Fading for a change of set: out over kDroneReleaseMs, pitch changes in
// silence, then in over kDroneSwellMs.
constexpr int32_t kDroneReleaseMs = 2000;
constexpr int32_t kDroneSwellMs = 6000;

// A resonant two-pole lowpass over the whole drone, its cutoff drifting on
// a cycle of its own (kDroneFilterSecsX10) and opening with the tide:
// kDroneFilterBaseHz, raised by up to kDroneFilterCycleOctavesQ12 by the
// cycle and kDroneFilterTideOctavesQ12 by the tide (octaves x 4096). About
// 180Hz to 1.8kHz in all.
constexpr int32_t kDroneFilterBaseHz = 180;
constexpr int32_t kDroneFilterSecsX10 = 830;
constexpr int32_t kDroneFilterCycleOctavesQ12 = 6144; // 1.5
constexpr int32_t kDroneFilterTideOctavesQ12 = 7372;  // 1.8
// Damping (Q15): 1 / resonance. 16384 is a resonance of 2.
constexpr int32_t kDroneFilterDampingQ15 = 16384;

// How much of the drone goes into the delays (Q12): a little, for space.
// Only above ~150Hz (a one-pole highpass, Q15), so the delays get its
// harmonics but not its bass, which would build up into mud.
constexpr int32_t kDroneSendQ12 = 600;
constexpr int32_t kDroneSendHighpassQ15 = 636;

// ---- Volumes (switch up: X = melody, Y = drone) ----------------------------------

// Where the volumes start (as knob positions, 0..4095) until X and Y set
// them. Knob position is squared, so the knobs feel even to the ear.
constexpr int32_t kMelodyVolumeStart = 3700;
constexpr int32_t kDroneVolumeStart = 3000;

// ---- Wander (CV Out 1 and 2) and triggers (Pulse Out 1 and 2) -----------------

// Wander: 0 to about +5V on CV Out 2, like Impermanence's CV Out 1. CV Out 1
// is its mirror image (the two always add up to ~5V): the ebb to its flow.
constexpr int32_t kWanderMax = 1700;
constexpr int32_t kWanderIntervalMinMs = 1000;
constexpr int32_t kWanderIntervalMaxMs = 5000;
constexpr int32_t kWanderStageMs = 700;
// Speed (Q12) at low and high tide: lazy, then restless.
constexpr int32_t kWanderSpeedLowTideQ12 = 2048;
constexpr int32_t kWanderSpeedHighTideQ12 = 12288;

// Both Pulse Outs give short triggers.
constexpr int32_t kTriggerMs = 10;

// Tide triggers (Pulse Out 2): about one every 20s at low tide, rising to
// about 30 times that rate at high tide, where they also come in clusters.
constexpr int32_t kTideTriggerRateLowTideQ24 = 839; // 0.05 per second, per ms
constexpr int32_t kTideTriggerRateOctavesQ12 = 20099; // x30 = 4.9 octaves
constexpr int32_t kTideTriggerClusterMax = 4;		  // extra triggers in a cluster at high tide
constexpr int32_t kTideTriggerClusterGapMinMs = 150;
constexpr int32_t kTideTriggerClusterGapMaxMs = 400;

// ---- Controls and LEDs ------------------------------------------------------

// Pulse In edges wait this long, and are dropped if the jack turns out to
// be empty: pulling a cable out makes ComputerCard's jack detection feed
// ~11ms of random edges into the input.
constexpr int32_t kPulseConfirmMs = 20;

// With the switch up, Main has to move this far before the tone follows it,
// so flipping the switch up never changes the tone by itself.
constexpr int32_t kToneMoveThreshold = 160;

constexpr int32_t kBirthFlickerMs = 180;

} // namespace eq
