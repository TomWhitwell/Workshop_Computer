#pragma once
#include <cstdint>

namespace ab
{

// Every number you might want to tune by ear, in one place.

// ---- Time ---------------------------------------------------------------
//
// Core 1 renders the voices 32 samples at a time (a "block", 0.67ms), and
// everything control-rate -- knobs, the scheduler, LEDs, the random CVs, the
// pulse outs -- runs once per block too. So control times are counted in
// blocks: 1500 of them a second.
constexpr int32_t kSampleRate = 48000;
constexpr int32_t kBlock = 32;
constexpr int32_t kBlocksPerSecond = kSampleRate / kBlock; // 1500
constexpr int32_t BlocksFromMs(int32_t ms) { return (ms * 3 + 1) / 2; }

// Blocks waiting between the cores. Core 1 stays up to three blocks (2ms)
// ahead of core 0, which is enough slack for its occasional slower block.
constexpr int32_t kFifoBlocks = 4;

// Knob and switch readings start at zero and take ~10ms to settle after
// power-on, so the card ignores them for the first 100ms.
constexpr int32_t kSettleBlocks = BlocksFromMs(100);

// ---- Panel --------------------------------------------------------------

// Start-up values, Q12 (4096 = full).
constexpr int32_t kStartVolume = 3072;	// 75%
constexpr int32_t kStartReverb = 2662;	// 65%
constexpr int32_t kStartDrift = 1229;	// 30%
constexpr int32_t kStartTape = 1638;	// 40%
constexpr int32_t kStartPiano = 3277;	// 80%
constexpr int32_t kStartChoir = 3277;	// 80%

// All six settings glide to new values over about 50ms: a one-pole per
// block with a time constant of ~17ms (1 - e^(-0.67/17) = 0.038, Q15).
constexpr int32_t kSettingGlideQ15 = 1260;

// The down tap fires on press, then ignores the switch until it has been
// out of Down for 30ms (so contact bounce, or a long hold, can't fire it
// twice).
constexpr int32_t kTapDebounceBlocks = BlocksFromMs(30);

// Pulse In 1 edges wait 20ms with the jack still patched before they count:
// pulling a cable out makes ComputerCard's jack detection feed up to ~11ms
// of random edges into the input, which would otherwise start a new piece
// on every unplug.
constexpr int32_t kPulseConfirmBlocks = BlocksFromMs(20);

// CV In 1: 0 to +5V (0 to ~1706 counts) adds 0 to 100% reverb mix.
// CV In 2: +/-2.5V (+/-853 counts) adds +/-50% drift.
// Both are x 2.4 (= 4095 / 1706), as Q12.
constexpr int32_t kCvToSettingQ12 = 9830;

// The Main knob's reverb macro. Up to two-thirds of the way round it's a
// plain wet/dry mix over the usual plate (8.4s). From there to the end the
// plate grows too: its decay gain rises to 0.88 (an 18s tail) and its
// damping darkens from about 3.5kHz to 2kHz, so fully wet becomes a slow,
// dark wash. Its output level comes down 2.5dB as it grows, which keeps the
// longer wash (measured) as loud as the usual one. Q12 / Q15 / of 1024.
constexpr int32_t kReverbMacroStart = 2731;	  // 2/3 of the knob (with CV In 1)
constexpr int32_t kReverbMaxDecayQ15 = 28836; // 0.88
constexpr int32_t kReverbMaxDampQ15 = 13763;  // 0.42
constexpr int32_t kReverbMaxOutQ10 = 135;	  // -2.5dB

// A new piece waits for the old one to fade out first: everything -- voices,
// echo, reverb, hiss, audio inputs -- over 15s, about as long as the
// reverb's longest tail, falling with the square of the time left. Then,
// with the output silent, the echo and reverb are emptied (40 blocks, 27ms)
// and the new piece begins, the output coming back up over 250ms. (The
// cycle bench uses a 1s fade: what each block costs doesn't depend on it.)
#ifdef ABEYANCE_BENCH
constexpr int32_t kNewPieceFadeBlocks = BlocksFromMs(1000);
#else
constexpr int32_t kNewPieceFadeBlocks = BlocksFromMs(15000);
#endif
constexpr int32_t kFlushBlocks = 40;
constexpr int32_t kFadeInBlocks = BlocksFromMs(250);

// ---- LEDs ---------------------------------------------------------------
constexpr int32_t kZFlashBlocks = BlocksFromMs(800);   // the new mode's row lights
constexpr int32_t kSweepBlocks = BlocksFromMs(500);	   // new piece: top to bottom
constexpr int32_t kWaitPulseBlocks = BlocksFromMs(1200); // a waiting knob's row pulses this slowly
// A piano strike's LED fades to ~2% over about 3s: x 0.99913 per block.
constexpr int32_t kPianoLedFadeQ16 = 65479;

// ---- Random CVs ---------------------------------------------------------
// Each picks a new target every 3-12s (divided by the piece's tempo) and
// glides there through two smoothing stages. Targets stay inside 5-95% of
// the range, so the voltage never runs into a limit in a straight line.
constexpr int32_t kCv1MinMv = 250, kCv1MaxMv = 4750;   // CV Out 1: 0..5V
constexpr int32_t kCv2MinMv = -4500, kCv2MaxMv = 4500; // CV Out 2: -5..+5V, centred on 0V
constexpr int32_t kCvIntervalMinBlocks = 3 * kBlocksPerSecond;
constexpr int32_t kCvIntervalMaxBlocks = 12 * kBlocksPerSecond;
constexpr int32_t kCvStageBlocks = BlocksFromMs(800);

// ---- Pulse outs ---------------------------------------------------------
constexpr int32_t kTriggerBlocks = BlocksFromMs(10); // Pulse Out 1: 10ms per piano note
constexpr int32_t kTriggerMergeBlocks = BlocksFromMs(15); // notes closer than this share a trigger
constexpr int32_t kGateBlocks = BlocksFromMs(1500);	 // Pulse Out 2: 1.5s after each piano note (/ tempo)

} // namespace ab
