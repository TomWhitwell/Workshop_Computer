// =============================================================================
//  qi_config.h : EVERY tuning number for the QiGong Timer lives here.
//  Change behaviour by editing numbers here, not the engine code.
//  AI-assisted (Claude / Anthropic).
//
//  Units: "samples" are 1/48000 s.  Knob values are 0..4095.  LED levels 0..4095
//  (the library squares them, so 1000 is roughly 6% of full PWM brightness).
// =============================================================================
#pragma once
#include <stdint.h>

namespace qi {

static const int32_t kSampleRate = 48000;
static const int32_t kMs = 48;                              // samples per millisecond

// ---- timer behaviour ----------------------------------------------------------------------------
static const int32_t kSettleSamples = 10 * kSampleRate;     // the silent "settling period" after Start (10 s)
static const int32_t kPulseSamples  = 100 * kMs;            // length of each pulse on Pulse Out 1/2 (100 ms)

// ---- settings ranges and power-up defaults (no saved settings in v1) -----------------------------
// (Owner's simplified set-up, 8 Oct: Main = intervals, X = minutes, Y = seconds; LEDs + soft clicks, no voice.)
static const int32_t kMinIntervals = 1, kMaxIntervals = 36;  // Main (Z up)
static const int32_t kMinMinutes = 0,  kMaxMinutes = 36;     // X (Z up); 0 = seconds only (owner, 19 Oct); no LEDs for 0
static const int32_t kMinIntervalSeconds = 10;               // 0 min + 0 s would be no interval at all: it becomes 10 s
static const int32_t kSecondsStep = 10, kSecondsNotches = 7; // Y (Z up): 0, 10 ... 60 extra seconds
static const int32_t kDefaultMinutes = 5, kDefaultSecIdx = 0, kDefaultIntervals = 6;   // 5:00 x 6

// ---- Z switch --------------------------------------------------------------------------------------
static const int32_t kBootGuard    = 125 * kMs;    // ignore Z for 125 ms after power-up (it reads Down briefly at boot)
static const int32_t kBounceSamples = 10 * kMs;    // Z-down presses shorter than 10 ms are ignored
static const int32_t kHoldSamples  = 3 * kSampleRate;   // hold Z down 3 s = stop
static const int32_t kTapMaxSamples = 800 * kMs;   // a press longer than 0.8 s is not a tap: letting go then does nothing
static const int32_t kHoldShowAfter = kTapMaxSamples;   // from 0.8 s of holding (while running) the LEDs show the hold filling up
static const int32_t kTapGap = 400 * kMs;          // taps closer together than this are counted together (3 = preview mode),
                                                   // so a single tap acts 0.4 s after it is released
static const int32_t kModeStable   = 20 * kMs;     // Z must sit Up (or leave Up) this long before set-up mode switches

// ---- set-up knobs (Z up) ---------------------------------------------------------------------------
// Notches: each value owns an equal slice of the knob.  To leave a value, the knob must pass its slice edge by
// kNotchHyst, so a knob resting on a boundary (or a little electrical jitter) can never flicker between two values.
static const int32_t kNotchHyst  = 48;
// "Take over immediately": on entering set-up (or a new X zone) the knob is armed at its current position and does
// nothing until it moves more than kTakeoverMove from there (so merely flipping Z or X never changes a value).
// After that, the knob's position sets the value directly.  (If a knob already sits at the end you want, nudge it away
// and back: it cannot take over without moving.)
static const int32_t kTakeoverMove = 40;   // (80 was too far: from near an end stop you sometimes couldn't move it 80)

// ---- set-up clicks: a very soft, calm "tok" per notch, like a safe dial; a slightly different pitch per setting ----
// (A4 / D5 / G5: three notes that sit well together.)  Short sine bursts with a gentle attack, on both outputs,
// only in set-up, never into the effects, never gated by the silence keeper.
static const int32_t kClickLevel  = 48;            // peak, 12-bit units (about -33 dBFS: very soft)
static const int32_t kClickHz[3]  = {440, 587, 784};   // intervals, minutes, seconds
static const int32_t kClickAttack = 32;            // 0.67 ms fade-in: no sharp edge
static const int     kClickDecay  = 8;             // then fades with a ~5 ms time constant
static const int32_t kClickLength = 40 * kMs;      // (finished after 40 ms)

// ---- the start of a session and preview mode ----
static const int32_t kDoubleChimeGap = 600 * kMs;  // the start is marked by TWO Pulse 1s, this far apart
static const int32_t kPreviewStep = kSampleRate;   // preview mode: chime (Pulse 1), 1 s later bell (Pulse 2), 1 s later chime...

// ---- outputs ------------------------------------------------------------------------------------------

// ---- LEDs (2 x 3 grid:  0 1 / 2 3 / 4 5).  Kept dim on purpose: this card is used with eyes closed / lights low.
static const uint8_t kLedOrder[6] = {0, 1, 2, 3, 4, 5};   // order the bars fill in (reading order, top-left first)
static const int32_t kLedBright   = 2400;          // countdown, "full" set-up LEDs, end glow
static const int32_t kLedMedium   = 900;           // "medium" set-up LEDs
static const int32_t kSetupBlinkSamples = 400 * kMs;   // a blinking set-up LED: 0.4 s bright, 0.4 s low
static const int32_t kLedRun      = 1400;          // session progress bar
static const int32_t kLedReady    = 900;           // the slow "ready" breath of all six LEDs while stopped
static const int32_t kLedFaint    = 350;           // set-up with X in its middle zone (nothing selected)
static const int32_t kLedPausedLow = 15;           // paused: the bar blinks down to this % of its level
static const int32_t kBlinkSamples = kSampleRate;  // paused blink: 1 s on, 1 s low
static const int32_t kFlashSamples = 600 * kMs;    // a soft flash of all LEDs on each pulse, fading over 0.6 s
static const bool    kFlashOnPulse = true;
static const int32_t kDoneGlowSamples = 6 * kSampleRate;  // after the end pulse: all six glow, fading out over 6 s

}  // namespace qi

// =============================================================================
//  EFFECTS (stage 2): two independent chains, In 1 -> Out 1 and In 2 -> Out 2.
//  Each chain: input -> delay (echoes) -> reverb (fed by the input AND the echoes) ; dry always at full level.
//  Both run at 24 kHz (half rate), the two channels on alternate samples so the CPU load is even.
// =============================================================================
namespace qi {

// ---- the reverb: 4-line feedback delay network (FDN) ----
static const int kFdnLen[4]   = {855, 1101, 1355, 1593};  // line lengths (samples at 24 kHz: 36..66 ms)
static const int kFdnSize     = 1640;                     // buffer per line (room for the slow wander)
static const int kApLen[2]    = {261, 195};               // two input diffusers (smear the attack into a wash)
static const int32_t kFdnWanderQ8 = 6 * 256;              // each line's length wanders +/-6 samples (avoids metallic ringing)
static const int kFdnWanderSec[4] = {13, 17, 19, 23};     // ... over these periods (seconds)
static const int32_t kFdnClamp = 4000000;                 // safety limit (Q6 units: ~30x full scale; keeps every multiply inside int32)
static const float kRevTimeMinS = 1.0f,  kRevTimeMaxS = 25.0f;     // X on the reverb page: decay time (RT60), exponential
static const float kDarkBrightHz = 9000.0f, kDarkDarkHz = 600.0f; // damping cutoff, bright -> dark (the tables count in "darkness";
                                                                  // the Y knob is brightness: darkness = 4095 - Y)

// ---- the delay ----
static const int kDelLen = 24064;                         // 1.003 s at 24 kHz (16-bit: 47 KB per channel)
static const float kDelMinMs = 60.0f, kDelMaxMs = 1000.0f; // X on the delay page: echo time, exponential
static const int32_t kDelFbMaxQ15 = 30474;                // Y on the delay page: feedback 0 .. 93% (never runaway)
static const int32_t kDelDampQ15 = 20000;                 // each repeat a little darker (~3.6 kHz one-pole in the loop)
static const int32_t kDelKnee = 16000;                    // inside the delay loop: soft saturation above this (Q3; ~ full scale)
static const int kDelGlideShift = 11;                     // echo-time changes glide (~85 ms), like tape: no clicks

// ---- filters around the effects (24 kHz, one-pole: cutoff ~ 24000 / (2 pi 2^shift)) ----
static const int kSendHpShift = 7;                        // send high-pass, two stages at ~30 Hz
static const int kWetDcShift  = 10;                       // wet DC blocker ~3.7 Hz

// ---- levels ----
static const int32_t kRevReturnQ12 = 1250;                // reverb level at the top of its mix knob (calibrated: full mix ~ +3 dB over a dry bell)
static const int32_t kDelReturnQ12 = 4096;                // first echo = the dry level at the top of its mix knob
static const int32_t kMixDead = 48;                       // mix knobs below this = exactly zero (truly dry)
static const float kRevRefHz = 150.0f;                    // the decay-time label is made true around here (the body of a bell)
static const float kRevCompExp = 0.15f;                   // how strongly long decays get less input (0 = none, 0.5 = energy-equal)
// Output: the DRY signal is never touched.  Only the wet part is limited, softly, using the headroom the dry signal
// leaves (it passes untouched up to half of that headroom, then bends toward it), so Out never exceeds full scale.

// ---- effect pages (Z middle): Main picks the page; X and Y edit it with soft pickup ----
// Three clear positions only (owner, 19 Oct), each ~8% of Main's travel, each with its LED sign:
//   fully anticlockwise = REVERB (top row), 12 o'clock = MIX (middle row), fully clockwise = DELAY (bottom row).
// Anywhere else Main changes nothing.
static const int32_t kPageRevTop = 330, kPageMixLo = 1883, kPageMixHi = 2213, kPageDelBottom = 3765;
static const int32_t kPageHyst = 60;
static const int32_t kFxShowSamples   = 2 * kSampleRate;   // LEDs show an effect value for 2 s after its knob moves
static const int32_t kPageShowSamples = 1200 * kMs;        // ... and which page for 1.2 s when Main enters it

// ---- power-up effect settings (knob units 0..4095; soft pickup holds them until a knob passes through) ----
static const int32_t kDefRevTime = 2600;   // ~7.7 s decay
static const int32_t kDefRevBright = 1495; // darkish (Y on the reverb page is BRIGHTNESS: clockwise = brighter, owner's rule 19 Oct)
static const int32_t kDefRevMix  = 2400;   // moderate
static const int32_t kDefDelMix  = 0;      // delay off
static const int32_t kDefDelTime = 2300;   // ~290 ms
static const int32_t kDefDelFb   = 1600;   // ~36%

}  // namespace qi

// =============================================================================
//  SILENCE KEEPER (after Robopera's design, made timer-aware): no Workshop System filter or Ring Mod "VCA" closes
//  fully, so a faint leak/hum sits between alerts, and long reverbs would sustain it.  Each channel's keeper sits
//  BEFORE the effects: it learns the hum level, opens when a real sound arrives (and always on the card's own pulse:
//  Pulse 1 -> channel 1, Pulse 2 -> channel 2), and fades the effect send out slowly as the sound dies away.
//  All times below are at 24 kHz (the effects' rate).
// =============================================================================
namespace qi {
static const bool    kKeeperOn       = true;    // the keeper on the effect send
#ifdef QI_KEEPER_SEND_ONLY
static const bool    kKeeperOnDry    = false;   // (build with -DCARD_KEEPER_SEND_ONLY=ON): the dry signal is never touched
#else
static const bool    kKeeperOnDry    = true;    // OWNER'S CHOICE (8 Oct): also gate the dry signal between sounds = true silence
#endif
static const int     kKeeperRise     = 4;       // envelope follower: rises in ~0.7 ms
static const int     kKeeperFall     = 9;       // ... falls with ~21 ms
static const int32_t kKeeperFloorInit = 8;      // starting guess of the leak level (12-bit units)
static const int32_t kKeeperFloorMax = 1000;     // while closed it follows the leak level freely, up to this (~ -6 dBFS)
static const int32_t kKeeperCloseAdd = 3;       // after a pulse: close when the sound stays < 1.5 x leak level + 3 ...
static const int32_t kKeeperHold     = 30 * 24; // ... for 30 ms
static const int32_t kKeeperAlertHold = 400 * 24;   // the card's own pulse: open at once and stay open at least 0.4 s
static const int32_t kKeeperMaxOpen  = 20 * 24000;  // ... and never more than 20 s after it (in case the leak grows meanwhile)
static const int     kKeeperFadeOut  = 10;      // closing fades with ~43 ms (decaying tails are not chopped); opening is instant
static const int32_t kKeeperSnap     = 33;      // below -60 dB the gain snaps to exactly zero
// ONLY THE CARD'S OWN PULSES OPEN THE KEEPER, at all times (owner, 19 Oct: "completely silent before/after interval sessions, as well
// as during them").  History: first it also opened for any sound louder than the learned hum (so hand-triggered sounds worked while
// stopped); a leak growing with pitch (CV ramps) and then a BEATING leak (FM: it swells and fades, so the learned level kept dropping
// and each swell looked like a sound) got through.  To hear the voices while tuning, use preview mode (its pulses open the keeper).
}  // namespace qi
