#pragma once
#include <cstdint>

// Every number you might want to change by ear, in one place.
//
// Times are in milliseconds unless the name says otherwise. "Q12" means a
// fraction stored as a whole number out of 4096 (so 4096 = 1.0, 2048 = 0.5),
// the usual way to do fractions on a chip with no floating-point unit.
// Likewise Q14 is out of 16384 and Q15 out of 32768.
//
// Many settings come in pairs that the morning's data moves between: "Dawn"
// and "Morning" ends of the sun's range, "Low" and "High" ends of the tide's.

namespace constancy
{

// ---- Timing ----------------------------------------------------------------

// Core 1 runs the slow "control" work once per millisecond.
constexpr int32_t kTickSamples = 48;
// The Computer smooths knob and switch readings; give them 100ms to settle
// after power-on before trusting them.
constexpr uint32_t kSettleSamples = 4800;

// ---- Voices ----------------------------------------------------------------
//
// Four melodic voices -- voice 0 is the lead, 1 to 3 are the loops -- and a
// drone. Each melodic voice is a stack of slightly detuned sawtooth waves,
// the "supersaw": several saws a few cents apart beat slowly against each
// other, which is what makes a CS-80 brass or string patch sound wide.

constexpr int kVoices = 4;
constexpr int kLead = 0;
// Saws per voice: five for the lead (the full CS-80 width, balanced: one
// in the centre, two each side), three for each loop. Saws are the number
// to watch on the chip; see README.md for the measured budget.
constexpr int kLeadSaws = 5;
constexpr int kSaws = 3;
// Each saw's fixed offset from the note, in cents. Saw 0 is in the centre
// of the stereo field, odd saws lean left and even ones right.
constexpr int32_t kSawSpreadCents[5] = {0, -9, 9, -4, 4};

// Icarus's drift: each saw's pitch wanders by up to this many cents, on a
// sine whose speed jumps to a new random value (up to 1Hz) every second.
constexpr int32_t kDriftCents = 10;
constexpr int32_t kDriftChangeMs = 1000;
constexpr int32_t kDriftMaxHzQ12 = 4096; // 1Hz

// One voice at full level peaks at about this share of full scale (Q14).
constexpr int32_t kVoicePeakQ14 = 7000;

// The voices' slow random panning (Icarus's Balance2 wobble): each voice's
// left/right balance drifts by up to this much (Q12 of full) every few
// seconds.
constexpr int32_t kPanWanderQ12 = 600;

// ---- Envelopes (the tide) --------------------------------------------------
//
// Icarus's cubed ADSR: attack, decay to a sustain level, hold while the
// note is held, release. "Cubed" means the level moves in a straight line
// in cube-root terms, then is cubed: attacks start gently and finish fast,
// releases fall quickly and then trail off a long way, like a hall.
//
// Low tide gives short, plucky strikes; high tide stretches everything
// into long, washed swells. Settings move between the two ends
// exponentially, so the middle of the tide sounds like the middle.
struct EnvelopeRange
{
	int32_t attackLow, attackHigh;	 // ms
	int32_t decayLow, decayHigh;	 // ms
	int32_t sustainLowQ12, sustainHighQ12;
	int32_t releaseLow, releaseHigh; // ms
};

constexpr EnvelopeRange kLoopEnvelope = {8, 2400, 250, 1500, 1200, 3500, 450, 5000};
// The lead stays more articulate than the pad at high tide, so a phrase
// still reads as a line.
constexpr EnvelopeRange kLeadEnvelope = {6, 380, 200, 900, 1800, 3500, 300, 2600};
// Icarus adds 15ms to every attack, so even the plucks never click.
constexpr int32_t kAttackFloorMs = 15;

// ---- The loops (voices 1 to 3) ---------------------------------------------
//
// As in Equanimity: each loop owns one note and repeats it on its own
// random cycle, a little quieter each time, then rests and a new loop
// begins. Here each loop also owns its own voice, so a repeat only ever
// restarts its own note: no stealing, no clipped tails.

constexpr int kLoopVoices = 3;
constexpr int32_t kMelodyBottom = 60; // the root in octave 4 (MIDI 60 + root)
constexpr int32_t kMelodySpan = 24;	  // two octaves
constexpr int32_t kLoopPeriodMinMs = 5000;
constexpr int32_t kLoopPeriodMaxMs = 15000;
constexpr int32_t kRepeatsMin = 4;
constexpr int32_t kRepeatsMax = 9;
// Rest between one loop ending (its note silent) and the next beginning.
constexpr int32_t kLoopRestMinMs = 500;
constexpr int32_t kLoopRestMaxMs = 6000;
// How long each strike holds before its release: a tap at low tide, a
// long swell at high tide.
constexpr int32_t kLoopGateLowMs = 60;
constexpr int32_t kLoopGateHighMs = 3500;
// A new loop's loudness, picked at random between these (Q12), and the
// fade per repeat (Q12) so the last repeat lands near -14dB, indexed by
// the number of repeats.
constexpr int32_t kVelocityMinQ12 = 2870;
constexpr int32_t kVelocityMaxQ12 = 4096;
constexpr int32_t kRepeatFadeQ12[kRepeatsMax + 1] = {0, 0, 0, 0, 2395, 2739, 2969, 3132, 3255, 3350};

// Notes a semitone above a drone note (the flat 2nd and flat 6th) rub
// against it. They come up this often compared with other notes (Q12)...
constexpr int32_t kRubNoteChanceQ12 = 1365; // a third as often
// ...and a loop only gets one at all when the tide is below this (Q12):
// they don't land on the long swells.
constexpr int32_t kRubNoteTideMaxQ12 = 2048;

// ---- The lead (voice 0) ----------------------------------------------------
//
// Phrases, not loops: three to six notes, mostly stepwise, ending on a
// long held note, then silence. The sun calls it: no phrases before
// sunrise, then more often as the sun climbs.

constexpr int32_t kPhraseNotesMin = 3;
constexpr int32_t kPhraseNotesMax = 6;
constexpr int32_t kLeadNoteMinMs = 280;
constexpr int32_t kLeadNoteMaxMs = 900;
constexpr int32_t kLeadHoldMinMs = 2500;
constexpr int32_t kLeadHoldMaxMs = 5000;
// Average silence between phrases, just after sunrise and with the sun at
// the top of the window. Each gap is random around this.
constexpr int32_t kPhraseGapSunriseMs = 22000;
constexpr int32_t kPhraseGapMorningMs = 5000;
// Steps through the scale between notes: chances (Q12) of moving 1, 2 or
// 3+ scale steps. Mostly stepwise.
constexpr int32_t kStepOneQ12 = 2900;
constexpr int32_t kStepTwoQ12 = 900;
// Portamento: the chance a note glides in from the last (Q12), and how long.
constexpr int32_t kGlideChanceQ12 = 1400;
constexpr int32_t kGlideMinMs = 60;
constexpr int32_t kGlideMaxMs = 240;
// Delayed vibrato: nothing for kVibratoDelayMs, then easing in over
// kVibratoRiseMs to kVibratoCents deep at kVibratoHzQ12.
constexpr int32_t kVibratoDelayMs = 350;
constexpr int32_t kVibratoRiseMs = 1200;
constexpr int32_t kVibratoCents = 16;
constexpr int32_t kVibratoHzQ12 = 21299; // 5.2Hz
// Brightness swell after each attack, standing in for CS-80 aftertouch:
// the lead's filter opens by up to kSwellOctavesQ12 over kSwellRiseMs, then
// eases back over kSwellFallMs.
constexpr int32_t kSwellOctavesQ12 = 5325; // 1.3 octaves
constexpr int32_t kSwellRiseMs = 700;
constexpr int32_t kSwellFallMs = 2500;
// Now and then a phrase ends with a slow fall in pitch, like easing off
// the CS-80's ribbon: chance (Q12), depth (semitones, Q12), time.
constexpr int32_t kFallChanceQ12 = 1230;
constexpr int32_t kFallMinQ12 = 6144;  // 1.5 semitones
constexpr int32_t kFallMaxQ12 = 12288; // 3 semitones
constexpr int32_t kFallMs = 1800;
// Lead level relative to a loop voice (Q12).
constexpr int32_t kLeadLevelQ12 = 4096;

// The melody's loudness across the morning. Before dawn the filters are
// dark and the notes long swells; around civil dawn the tide is low and the
// notes short plucks through a dark filter; by morning everything is open
// and the loop's long, bright delay sustains it -- measured, the melody
// came out 16dB louder at the end of the window than around civil dawn.
// This lifts it (dB x 10) at nine points across the sun's climb (0, 512 ..
// 4096), blended between them, to take out about two thirds of that: more
// even, but the dark still sounds like the dark.
constexpr int32_t kMelodyMakeupDbX10[9] = {41, 50, 64, 75, 54, 21, 8, -18, -37};

// ---- Reseed ----------------------------------------------------------------

// Switch down: every melodic note stops -- the loops end,
// and the notes still sounding release as they would (cutting them would
// click). The new key is picked at once and the drone glides to its root;
// no new melodic note starts until the drone has arrived and the old
// notes have died away.
//
// Meanwhile the loop's feedback dips to this (Q12) over kReseedDuckMs, so
// the old key drains out of it even when the Sun knob has the loop
// burning, and the new notes wait at least kReseedDrainMs from the tap.
// Then the feedback comes back over kReseedRestoreMs.
constexpr int32_t kReseedFeedbackQ12 = 1600;
constexpr int32_t kReseedDuckMs = 1500;
constexpr int32_t kReseedDrainMs = 3000;
constexpr int32_t kReseedRestoreMs = 4000;

// ---- Drone -----------------------------------------------------------------
//
// Root and fifth, two octaves below the melody (the root in octave 2,
// 65-123Hz). Two saws on the root, a few cents apart and drifting; one on
// the fifth, tuned pure (exactly 3:2 above the root, not equal tempered) so
// the drone stays still.

constexpr int kDroneSaws = 3;
constexpr int32_t kDroneBottom = 36;
constexpr int32_t kDroneSpreadCents = 4;
constexpr int32_t kDroneDriftCents = 3;
// Its own gentle lowpass (one-pole) so it sits behind the melody.
constexpr int32_t kDroneCutoffHz = 700;
constexpr int32_t kDronePeakQ14 = 4200;
// A slow breath in its level: +/- this much (Q12) over kDroneBreathMs.
constexpr int32_t kDroneBreathQ12 = 500;
constexpr int32_t kDroneBreathMs = 47000;
// How long the drone takes to glide to a new root after a reseed.
constexpr int32_t kDroneGlideMs = 4000;
// Fade in at power-on.
constexpr int32_t kDroneFadeInMs = 4000;
// A little of the drone goes into the feedback loop (Q12): mostly it goes
// round the loop, not through it.
constexpr int32_t kDroneSendQ12 = 450;

// ---- The Icarus loop -------------------------------------------------------
//
// Infinite Digits' Icarus: a short stereo delay with gentle filters, a stereo
// rotation, soft clipping and a Moog-style ladder all inside its feedback
// loop. Here the delay time follows the sun.

// The loop runs at half rate, 24kHz (see IcarusLoop.h). Room for the
// longest delay plus the sunrise swoop, in its samples per channel.
constexpr int32_t kDelaySize = 16200; // 0.675s
// Delay time at the bottom of the window, at sunrise, and at the top (ms).
// Icarus's own range is 50 to 500ms.
constexpr int32_t kDelayDawnMs = 60;
constexpr int32_t kDelaySunriseMs = 250;
constexpr int32_t kDelayMorningMs = 500;
// Icarus lags its delay time by 0.2s: changes bend the pitch of what's in
// the loop, and that's part of the sound. Lagged on purpose.
constexpr int32_t kDelayLagMs = 200;

// Destruction: random dropouts that duck the loop to half for 0.2s, only
// below a quarter-second delay (as in Icarus), more often as it shortens.
constexpr int32_t kDestructionBelowMs = 250;
constexpr int32_t kDestructionMaxHzQ12 = 12288; // 3 per second at the shortest delay
constexpr int32_t kDestructionMs = 200;

// The sunrise swoop: crossing the horizon stretches the delay by
// kSwoopMs over kSwoopRiseMs and lets it back over kSwoopFallMs, like
// holding Icarus's time key -- the loop bends down and back up in pitch.
constexpr int32_t kSwoopMs = 150;
constexpr int32_t kSwoopRiseMs = 1000;
constexpr int32_t kSwoopFallMs = 1600;
// It fires again only after the sun has gone this far (Q12) back below
// the horizon.
constexpr int32_t kSwoopRearmQ12 = 300;

// Sun (X): feedback from kFeedbackMinQ12 (knob left) through
// kFeedbackCentreQ12 to kFeedbackMaxQ12 (knob right, Icarus's top). Drive
// into the clipper rises from 1.0 to kDriveMaxQ12 with it (1.25 at the
// centre, Icarus's setting), so at the centre the loop's gain is just
// under 1: long tails that still fade. Right of centre it passes 1, and the
// soft clipper and ladder hold it at a burning, saturated sustain -- fly
// close to the sun.
constexpr int32_t kFeedbackMinQ12 = 1638;	 // 0.4
constexpr int32_t kFeedbackCentreQ12 = 3277; // 0.8
constexpr int32_t kFeedbackMaxQ12 = 6144;	 // 1.5
constexpr int32_t kDriveMaxQ12 = 8192;		 // 2.0
// Burning, the loop gets much louder; above a loop gain of 1 the wet level
// is trimmed by the square root of the excess, so it grows, but gently.
constexpr bool kBurnTrim = true;
// A running tide pushes the feedback towards the sun by up to this (Q12);
// a turning tide leaves it be.
constexpr int32_t kTideFeedbackNudgeQ12 = 330;

// The ladder's cutoff follows the sun: muffled before dawn, open by
// morning (Hz). Its resonance (0..4, Q12) stays gentle.
constexpr int32_t kLadderDawnHz = 1000;
constexpr int32_t kLadderSunriseHz = 3000;
constexpr int32_t kLadderMorningHz = 9000; // the loop's 24kHz tops out near 11kHz
constexpr int32_t kLadderResonanceQ12 = 1600; // 0.4

// Diffusion: three allpass stages per channel inside the loop smear each
// pass a little more, standing in for the Blade Runner hall (a real reverb
// is too heavy for the chip). Lengths in samples (left, right differ so
// the two sides decorrelate; at the loop's 24kHz, so 113 is 4.7ms), and
// the allpass gain (Q15).
constexpr int32_t kAllpassLeft[3] = {113, 89, 277};
constexpr int32_t kAllpassRight[3] = {131, 97, 263};
constexpr int32_t kAllpassGainQ15 = 18000; // 0.55

// The loop's own fixed parts, from Engine_Icarus.sc: OnePole(0.4),
// OnePole(-0.08) and Rotate2(0.2) (36 degrees) in the feedback path.
// OnePole(0.4) at 48kHz is OnePole(0.16) at the loop's 24kHz: it moves 84%
// of the way each sample.
constexpr int32_t kLoopPoleA_Q15 = 27525;
constexpr int32_t kLoopPoleB_Y_Q15 = 2621; // 0.08
constexpr int32_t kRotateCosQ15 = 26510;	// cos 36
constexpr int32_t kRotateSinQ15 = 19261;	// sin 36

// Audio In 1 and 2 go into the loop (only), this loud (Q12 of full scale
// per jack count: the jacks are 12-bit, the loop 16-bit).
constexpr int32_t kAudioInGain = 12;

// ---- Brightness (the sun) --------------------------------------------------

// The melody's own lowpass, CS-80 style, before the dry tap and the loop:
// muffled before dawn, opening with the sun (Hz). The lead's filter starts
// from the same place and swells above it.
constexpr int32_t kVoiceCutoffDawnHz = 1500;
constexpr int32_t kVoiceCutoffSunriseHz = 4000;
constexpr int32_t kVoiceCutoffMorningHz = 10000;

// ---- Water (Y) -------------------------------------------------------------
//
// One knob, two filters. In the centre both are open. To the left a
// lowpass closes, drowning everything; to the right a low-cut rises,
// thinning it. 12dB per octave each way.

constexpr int32_t kWaterDeadZone = 80; // knob counts either side of the centre
// Each filter fades in from nothing over this first part of its side
// (Q12: an eighth), so moving through the centre never clicks.
constexpr int32_t kWaterFadeInQ12 = 512;
constexpr int32_t kWaterLowpassOpenHz = 16000;
constexpr int32_t kWaterLowpassClosedHz = 120;
constexpr int32_t kWaterHighpassOpenHz = 15;
constexpr int32_t kWaterHighpassClosedHz = 2500;

// ---- Levels (switch up) ------------------------------------------------------

// The delay mix, fixed (as a knob position: 0 dry, 4095 the loop alone).
constexpr int32_t kDelayMix = 2700;
// Where the Levels page starts (as knob positions, 0..4095) until Main, X
// and Y set them: Main the Turing machine (fully clockwise: locked), X and
// Y the levels. Levels are knob position squared, so they feel even.
constexpr int32_t kTuringStart = 4095;
constexpr int32_t kMelodyLevelStart = 3663; // 80% volume (the knob is squared)
constexpr int32_t kDroneLevelStart = 2590;	// 40% volume
// Where the Play page starts if the card powers on with the switch up.
constexpr int32_t kTimeStart = 2048;
constexpr int32_t kSunStart = 2048;
constexpr int32_t kWaterStart = 2048;

// The loop's level against the dry voices, before the crossfade (Q12). The
// loop runs hot (it saturates), so it sits a little lower.
constexpr int32_t kWetLevelQ12 = 2900;

// Overall output level (Q12), after the mix and before the output limiter.
constexpr int32_t kOutputGainQ12 = 14336;

// ---- Time --------------------------------------------------------------------

// The Time position is lagged a little (ms) so knob jitter doesn't flutter
// the data. CV In 1 adds to it: +5V moves it about the whole window.
constexpr int32_t kTimeLagMs = 80;
// The Sun and Water values are lagged too.
constexpr int32_t kKnobLagMs = 40;

// ---- CV outs -------------------------------------------------------------------
//
// CV Out 1, the tide: a smooth random voltage, 0 to about +5V, wandering
// around the tide's height; still air wanders slowly and narrowly, wind
// makes it wider and choppier. (CV Out 2 is the Turing machine's pitch.)
constexpr int32_t kCvMax = 1700; // about +5V, in DAC counts
constexpr int32_t kTideWanderMinQ12 = 300;
constexpr int32_t kTideWanderMaxQ12 = 1300;
constexpr int32_t kTideWanderMsStill = 4000; // between targets
constexpr int32_t kTideWanderMsWindy = 500;

// ---- The Turing machine (CV Out 2, Pulse Out 2) -------------------------------
//
// Eight bits in a loop, stepped by the clock on Pulse In 1 (see Turing.h).
// Notes from the root in octave 3 (MIDI 48 + root: 1V below middle C's 0V)
// across kTuringSpan semitones. The last kTuringLockZone knob counts at
// either end of Main's travel (Levels page) hold the loop locked.
constexpr int32_t kTuringSteps = 8;
constexpr int32_t kTuringBottom = 48;
constexpr int32_t kTuringSpan = 24;
constexpr int32_t kTuringLockZone = 160;

// ---- Pulses and LEDs -----------------------------------------------------------

constexpr int32_t kTriggerMs = 10;
// Pulse In edges wait this long, and are dropped if the jack turns out to
// be empty: pulling a cable out makes ComputerCard's jack detection feed
// ~11ms of random edges into the input.
constexpr int32_t kPulseConfirmMs = 20;
constexpr int32_t kReseedFlickerMs = 400;

} // namespace constancy
