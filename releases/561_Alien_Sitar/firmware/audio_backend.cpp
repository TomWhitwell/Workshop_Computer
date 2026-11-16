/// @file audio_backend.cpp
/// @brief Hardware backend: 48 kHz ComputerCard callback on core0, 24 kHz instrument on core1.
///
/// Core0 owns all hardware I/O inside the DMA-driven 48 kHz audio interrupt:
/// ADC mux, normalisation probes, knob/switch/CV reads, DAC and PWM outputs.
/// Control settings update at a 750 Hz tick (with an extra read on switch
/// edges); filtered input is decimated to 24 kHz and handed to the core1
/// worker through a bounded SPSC queue; each returned 24 kHz frame is
/// reconstructed to 48 kHz with a zero-stuffed filter pair, fixed 4x makeup
/// gain, slew limiting and a nominal +/-5 V cap. During the ~32 ms boot-only
/// quiet period the outputs stay silent while the ADC mux values and the 32-bit
/// jack probe history settle, so initial zeroes cannot mimic a Down press.
/// Neither the audio callback nor the core1 worker ever waits for the other:
/// full/empty bounded queues are counted and the output fades on underrun.
/// Boot restore, card-flash saves and the USB editor foreground poll
/// (see storage.cpp / usb_editor.cpp) stay in this core0 context. Callback,
/// worker and whole-IRQ durations are measured into SWD-visible counters; the
/// numbers are diagnostics of this build, not hardware performance guarantees.
/** RP2040 ComputerCard backend. core0: hardware/control/rate conversion;
 * core1: 24k fixed-point instrument. No waiting for core1 in the audio IRQ. */
#include "audio_backend.hpp"
#include "../port/editor.hpp"
#include "storage.hpp"
#ifdef MTM_USB_EDITOR
#include "usb_editor.hpp"
#define COMPUTERCARD_FOREGROUND() computer::pollUsbEditor()
#endif
#include "../port/config.hpp"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
// Optional vendor hooks include mux/ADC/probe/DAC work in timing, not just
// ProcessSample. The recording tail and Cortex-M entry/exit are extra overhead.
namespace {
void recordAudioIRQ(uint32_t elapsed);
}
// Whole-handler timing hooks. The generated header's AudioCallback braces its
// entire body with these, so the recorded duration covers the complete 48 kHz
// DMA IRQ work on core0 - ADC mux, jack probe, DAC/library code - not just the
// ProcessSample override; the recording tail and exception entry/exit remain
// extra overhead outside the recorded span.
#define COMPUTERCARD_IRQ_START() const uint32_t computerIRQStart = time_us_32()
#define COMPUTERCARD_IRQ_END() recordAudioIRQ(time_us_32() - computerIRQStart)
#include "../port/auxiliary.hpp"
#include "../port/controller.hpp"
#include "../port/events.hpp"
#include "../port/output.hpp"
#include "../port/processor.hpp"
#include "../port/queue.hpp"
#include "ComputerCard.h"

namespace {
computer::Queue<computer::Packet> input;
computer::Queue<mtm::Frame> output;
computer::Processor processor;
// Ownership: core0 produces input and consumes output; core1 does the reverse.
// Only core1 accesses Processor/Voice state. Diagnostic counters each have one
// writer, so load-plus-store increments avoid atomic RMW spin-lock helpers.
// Visible with SWD; units microseconds, counters wrap modulo 2^32.
struct Diagnostics {
    // Peak/late pairs in declaration order: worker, then input queue, then the
    // 48 kHz callback, then the complete IRQ body. Late thresholds are the
    // configured budgets: worker >= 42 us, callback >= 21 us, IRQ >= 20 us.
    std::atomic<uint32_t> maxWorkerUs{0}, lateWorker{0}, underruns{0}, inputOverruns{0},
        maxCallbackUs{0}, callbackOverruns{0}, maxIRQUs{0}, irqOverruns{0};
} diagnostics;
/// @brief Widens @p v to hold @p x when @p x is larger, using relaxed atomics.
/// @param v Microsecond-maximum counter to update; relaxed load and store.
/// @param x Newly measured duration in microseconds.
/// @note Load-plus-store (no atomic RMW) is safe because every diagnostics
///       counter here has a single writer core.
void maximum(std::atomic<uint32_t> &v, uint32_t x) {
    if (x > v.load(std::memory_order_relaxed)) {
        v.store(x, std::memory_order_relaxed);
    }
}
/// @brief Records one whole-IRQ-body duration sample from the vendor end hook.
///
/// Runs inside the audio IRQ on core0, immediately after the ComputerCard
/// callback body. The 48 kHz sample period leaves 20.83 us for the complete
/// handler, so the configured @c irqWarningUs budget of 20 us is deliberately
/// conservative; bodies at or above it latch `irqOverruns`, which lights the
/// latching fault LED 4 together with the other timing/queue faults.
/// @param elapsed Measured whole-handler duration in microseconds.
/// @note Single writer (core0 audio IRQ) per counter; relaxed atomics.
void recordAudioIRQ(uint32_t elapsed) {
    maximum(diagnostics.maxIRQUs, elapsed);
    // Conservatively flag >=20 us: 48 kHz leaves 20.83 us for the whole ISR.
    if (elapsed >= computer::timing::irqWarningUs) {
        diagnostics.irqOverruns.store(diagnostics.irqOverruns.load(std::memory_order_relaxed) + 1,
                                      std::memory_order_relaxed);
    }
}
/// @brief Core1 body: the 24 kHz fixed-point instrument worker, forever.
///
/// Per pass it pops one Packet (settings snapshot, decimated string/halo
/// inputs, pluck/damp events) from `input`, renders one mtm::Frame (mix, dry,
/// wet plus smoothed period) on `processor`, and pushes it to `output`. Both
/// queues are bounded SPSC; a pass with an empty input or a full output queue
/// idles with tight_loop_contents() instead of blocking or dropping work. The
/// worker frame budget is 42 us: renders at or above it latch `lateWorker`.
/// This runs on core1, launched by runInstrument, with sole access to
/// Processor/Voice state.
/// @note Never returns; never touched by the audio IRQ (the queues do the
///       exchange and neither side waits).
void worker() {
    computer::Packet p;
    for (;;) {
        if (output.size() == computer::timing::queueCapacity || !input.pop(p)) {
            tight_loop_contents();
            continue;
        }
        auto start = time_us_32();
        auto frame = processor.process(p);
        auto elapsed = time_us_32() - start;
        maximum(diagnostics.maxWorkerUs, elapsed);
        if (elapsed >= computer::timing::workerWarningUs) {
            diagnostics.lateWorker.store(diagnostics.lateWorker.load(std::memory_order_relaxed) + 1,
                                         std::memory_order_relaxed);
        }
        // The worker is the sole writer and prechecked capacity; push cannot block.
        output.push(frame);
    }
}
/// @brief Core0 ComputerCard subclass: hardware callback and instrument control.
///
/// One statically stored instance (see runInstrument). Everything here runs
/// on core0 - the 48 kHz ProcessSample audio callback, the 750 Hz control
/// state machine, the flash journal interaction and the auxiliary CV/pulse/
/// LED outputs - while the voice synthesis itself runs on core1 behind the
/// bounded SPSC queues of this file.
class Card final : public ComputerCard {
    computer::Controller controller;      ///< Panel + held-Down control state machine.
    computer::saved::State bootState;     ///< Boot journal snapshot loaded in the constructor.
    bool bootPending = false;             ///< A loaded boot state still awaits restoration.
    computer::Controls controls;          ///< Latest panel readings and calibrated millivolts.
    computer::Settings settings;          ///< Settings snapshot for the next 24 kHz packet.
    computer::Events events;              ///< Pending pluck/damp flags from pulse input edges.
    computer::AuxiliaryOutputs auxiliary; ///< Followers, bloom pulse and derived CV/pulse values.
    // Rate conversion: input 48->24 kHz decimating pair (in1/in2) and output
    // 24->48 kHz zero-stuffing pair (out1/out2), whose result is conditioned
    // by `conditioned` with the fixed 4x makeup gain, nominal +/-5 V cap and
    // 48 kHz slew limiter.
    mtm::Fir in1, in2, out1, out2;
    mtm::Frame frame{}; ///< Last worker frame popped (fresh or faded fallback).
    computer::Output conditioned;
    uint32_t tick = 0; ///< Monotonic 48 kHz counter driving control/voice frames.
    // Boot-quiet countdown (silent DAC/pulse outputs) and the PulseOut1 pluck
    // pulse countdown; both count 48 kHz frames.
    int32_t bootFrames = computer::timing::bootQuietFrames, pluckPulse = 0;
    // Callback pipeline stages, implemented below: refresh controls (750 Hz
    // and on switch edges), exchange a 24 kHz packet with the worker, write
    // 48 kHz audio every frame, then update indicators on control frames.
    void updateControls();
    void exchangeFrame(int32_t stringInput, int32_t haloInput);
    void writeAudio(bool internal);
    void updateIndicators();
    void ProcessSample() override;

public:
    Card();
};

/// @brief Enables jack normalisation probing and loads the boot journal.
///
/// Runs on core0 within runInstrument, after the regulator/clock raise and
/// before core1 starts. The probe must be enabled before any Connected()
/// query answers; storage is sized from the detected flash capacity (larger
/// compatible cards are supported), and the newest valid journal record, if
/// any, is kept for the first control tick after the boot-quiet period.
/// @post bootPending is true exactly when load() found a valid bootState.
Card::Card() {
    EnableNormalisationProbe();
    computer::storage::initialise(FlashSizeBytes());
    bootPending = computer::storage::load(bootState);
}

/// @brief Reads the panel APIs into `controls` and advances control state.
///
/// Fills `controls` from the three knobs, calibrated CV/audio millivolts,
/// jack detection and the switch, all read on core0 in this callback-owned
/// path. A connected CV1 additionally receives the saved user affine input
/// correction. The first call after boot restores the journal through
/// editor::restoreAtBoot (honouring a Down switch held at boot); later calls
/// run the controller's normal update, and under MTM_USB_EDITOR the editor
/// bridge services queued commands at this boundary. Called on core0 from
/// ProcessSample at 750 Hz control frames and on switch-position changes.
/// @note pitchFactoryMv deliberately keeps the factory-scaled (uncorrected)
///       CV1 reading for the editor's calibration readback (PERSISTENCE.md).
void Card::updateControls() {
    controls.knobs[computer::MainKnob] = KnobVal(Main);
    controls.knobs[computer::LeftKnob] = KnobVal(X);
    controls.knobs[computer::RightKnob] = KnobVal(Y);
    controls.pitchConnected = Connected(CV1);
    controls.bendConnected = Connected(CV2);
    controls.audio1Connected = Connected(Audio1);
    controls.audio2Connected = Connected(Audio2);
    controls.pitchMv = CVInMillivolts(0);
    controls.pitchFactoryMv = controls.pitchMv;
    if (controls.pitchConnected) {
        controls.pitchMv =
            computer::saved::apply(controller.pitchInputCalibration, controls.pitchMv);
    }
    controls.bendMv = CVInMillivolts(1);
    controls.audio1Mv = AudioIn1Millivolts();
    controls.audio2Mv = AudioIn2Millivolts();
    controls.position = static_cast<computer::Position>(SwitchVal());
    if (bootPending) {
        settings = computer::editor::restoreAtBoot(bootState, controller, controls);
        bootPending = false;
    } else {
        settings = controller.update(controls);
    }
#ifdef MTM_USB_EDITOR
    computer::editorBridge.service(controller, controls, settings);
#endif
}

/// @brief Exchanges one 24 kHz packet with the core1 worker (non-blocking).
///
/// Called on every alternate 48 kHz hardware sample. `input.push` is
/// non-blocking; acceptance arms the 5 ms PulseOut1 pluck pulse and notifies
/// the damp path, while rejection counts `inputOverruns` and leaves the
/// pluck/damp flags pending for the next packet. `output.pop` then fetches
/// the freshly rendered frame; a miss counts `underruns` and fades the stale
/// frame by 31/32 per sample instead of repeating one DC sample. Runs on
/// core0 inside the audio callback; neither queue op can block.
/// @param stringInput Decimated/filtered Audio1 sample (Q15) exciting the string.
/// @param haloInput Decimated/filtered Audio2 sample (Q15) exciting the halo bank.
/// @post `frame` holds a fresh worker frame or the smooth-fading fallback.
void Card::exchangeFrame(int32_t stringInput, int32_t haloInput) {
    computer::Packet p{settings, stringInput, haloInput, events.pluck, events.damp};
    if (input.push(p)) {
        if (events.pluck) {
            pluckPulse = computer::timing::pluckPulseFrames;
        }
        if (p.damp) {
            auxiliary.notifyDamp();
        }
        events.pluck = events.damp = false;
    } else {
        diagnostics.inputOverruns.store(diagnostics.inputOverruns.load(std::memory_order_relaxed) +
                                            1,
                                        std::memory_order_relaxed);
    }
    const bool validFrame = output.pop(frame);
    if (!validFrame) {
        diagnostics.underruns.store(diagnostics.underruns.load(std::memory_order_relaxed) + 1,
                                    std::memory_order_relaxed);
        // Smooth loss-of-worker fallback instead of repeating one DC sample.
        frame.mix = frame.mix * 31 / 32;
        frame.dry = frame.dry * 31 / 32;
        frame.wet = frame.wet * 31 / 32;
    }
    auxiliary.advance(frame, validFrame);
}

/// @brief Reconstructs and writes one 48 kHz output frame plus pulse outputs.
///
/// Runs on every hardware sample, on core0. The zero-stuffed 24->48 kHz
/// filter advance doubles for reconstruction gain (unity after doubling) and
/// `conditioned` applies the fixed 4x makeup gain, nominal +/-5 V cap and
/// slew limiting before the division into signed 12-bit DAC codes. Non-voice
/// frames feed zeroes so the filter interpolates instead of stretching the
/// last frame. PulseOut1 stays high while the pluck pulse counts down,
/// PulseOut2 follows the auxiliary bloom detector.
/// @param internal True on voice frames, when fresh frame mix/wet are valid.
void Card::writeAudio(bool internal) {
    // Zero-stuffed 24->48k reconstruction, unity gain after doubling.
    conditioned.next(2 * out1.next(internal ? frame.mix : 0),
                     2 * out2.next(internal ? frame.wet : 0));
    AudioOut1(static_cast<int16_t>(conditioned.mix / computer::timing::q15ToDac));
    AudioOut2(static_cast<int16_t>(conditioned.halo / computer::timing::q15ToDac));
    PulseOut1(pluckPulse > 0);
    if (pluckPulse > 0) {
        --pluckPulse;
    }
    PulseOut2(auxiliary.bloomPulseHigh());
}

/// @brief Refreshes auxiliary CV outputs and the six LEDs at 750 Hz.
///
/// CVOut1 plays the pitch CV in calibrated millivolts with the user affine
/// output correction; CVOut2 is the halo level follower. LEDs read: 0 pluck
/// pulse, 1 external mode, 2 melody level, 3 halo level, 4 the latching
/// queue/timing fault (any underrun/overrun counter nonzero), 5 missing
/// input or CV-output factory calibration. Runs on core0 in the callback.
/// @note Fault counters never reset, so LED 4 latches until reboot.
/// @warning Not processed during the boot-quiet period.
void Card::updateIndicators() {
    CVOut1Millivolts(
        computer::saved::apply(controller.pitchOutputCalibration, auxiliary.pitchMillivolts()));
    CVOut2Millivolts(auxiliary.haloMillivolts());
    LedOn(0, pluckPulse > 0);
    LedOn(1, settings.external);
    LedBrightness(2, mtm::clip(auxiliary.dryLevelQ15() / 4, 0, 4095));
    LedBrightness(3, mtm::clip(auxiliary.haloLevelQ15() / 4, 0, 4095));
    LedOn(4, diagnostics.underruns.load() || diagnostics.inputOverruns.load() ||
                 diagnostics.callbackOverruns.load() || diagnostics.irqOverruns.load());
    LedOn(5, !InputsCalibrated() || !CVOutsCalibrated());
}

/// @brief The 48 kHz audio callback body: the complete core0 real-time pipeline.
///
/// Entered from ComputerCard's ADC/DMA handler on core0 once per hardware
/// sample; bodies of 21 us or more latch `callbackOverruns` (the whole 48 kHz
/// period leaves only 20.83 us, so changes to this code deserve measurement).
/// Pipeline order: drop into the boot-quiet silence for the first 1536 frames
/// (about 32 ms) while the ADC mux and jack probe history settle; refresh
/// `controls` (control frames and switch edges); sample pulse inputs into
/// event flags; decimate Audio1/2 to 24 kHz; on voice frames exchange one
/// packet with the core1 worker; write 48 kHz audio every frame; refresh
/// indicators on control frames. No waiting, allocation or USB work here.
/// @note The vendor IRQ hooks around the full AudioCallback add the whole-
///       handler measurement covering this body plus mux/probe/DAC work.
void Card::ProcessSample() {
    const auto start = time_us_32();
    // The ADC mux initially contains zeroes, including a false Down switch.
    // Allow the 32-bit jack probe history and all mux channels to settle before
    // accepting events or taking modifier snapshots. This is only at boot.
    if (bootFrames > 0) {
        --bootFrames;
        AudioOut1(0);
        AudioOut2(0);
        PulseOut1(false);
        PulseOut2(false);
        return;
    }
    auto position = static_cast<computer::Position>(SwitchVal());
    if (computer::timing::controlFrame(tick) || position != controller.previousPosition()) {
        updateControls();
    }
    events.sample(PulseIn1(), PulseIn2(), position);
    int32_t a = in1.next(AudioIn1() * computer::timing::adcToQ15),
            b = in2.next(AudioIn2() * computer::timing::adcToQ15);
    const bool internal = computer::timing::voiceFrame(tick);
    if (internal) {
        exchangeFrame(a, b);
    }
    writeAudio(internal);
    if (computer::timing::controlFrame(tick)) {
        updateIndicators();
    }
    ++tick;
    const auto elapsed = time_us_32() - start;
    maximum(diagnostics.maxCallbackUs, elapsed);
    if (elapsed >= computer::timing::callbackWarningUs) {
        diagnostics.callbackOverruns.store(
            diagnostics.callbackOverruns.load(std::memory_order_relaxed) + 1,
            std::memory_order_relaxed);
    }
}
} // namespace
namespace computer {
/// @brief Full documentation belongs to the declaration in audio_backend.hpp.
///
/// Implementation notes: the card is constructed here (core0) with static
/// storage, safely off the run/IRQ stack, after the voltage/clock raise in
/// main() and before any concurrent work starts - local-static construction
/// therefore finishes before core1 launches, keeping -fno-threadsafe-statics
/// valid without a locking runtime. Priming `output` with 64 silent frames
/// gives core1's first renders a 2.667 ms elastic lead; core1 starts next,
/// then the optional USB editor, then ComputerCard's never-returning loop.
void runInstrument() {
    // Construct after clock setup, with static storage rather than the IRQ stack.
    static Card card;
    for (unsigned i = 0; i < timing::initialOutputFrames; ++i) {
        output.push(mtm::Frame{});
    }
    multicore_launch_core1(worker);
#ifdef MTM_USB_EDITOR
    startUsbEditor();
#endif
    card.Run();
}
} // namespace computer
