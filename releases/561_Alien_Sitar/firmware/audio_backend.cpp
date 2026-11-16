/** RP2040 ComputerCard backend. core0: hardware/control/rate conversion;
 * core1: 24k fixed-point instrument. No waiting for core1 in the audio IRQ. */
#include "audio_backend.hpp"
#include "../port/config.hpp"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
// Optional vendor hooks include mux/ADC/probe/DAC work in timing, not just
// ProcessSample. The recording tail and Cortex-M entry/exit are extra overhead.
namespace {
void recordAudioIRQ(uint32_t elapsed);
}
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
    std::atomic<uint32_t> maxWorkerUs{0}, lateWorker{0}, underruns{0}, inputOverruns{0},
        maxCallbackUs{0}, callbackOverruns{0}, maxIRQUs{0}, irqOverruns{0};
} diagnostics;
void maximum(std::atomic<uint32_t> &v, uint32_t x) {
    if (x > v.load(std::memory_order_relaxed)) {
        v.store(x, std::memory_order_relaxed);
    }
}
void recordAudioIRQ(uint32_t elapsed) {
    maximum(diagnostics.maxIRQUs, elapsed);
    // Conservatively flag >=20 us: 48 kHz leaves 20.83 us for the whole ISR.
    if (elapsed >= computer::timing::irqWarningUs) {
        diagnostics.irqOverruns.store(diagnostics.irqOverruns.load(std::memory_order_relaxed) + 1,
                                      std::memory_order_relaxed);
    }
}
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
class Card final : public ComputerCard {
    computer::Controller controller;
    computer::Controls controls;
    computer::Settings settings;
    computer::Events events;
    computer::AuxiliaryOutputs auxiliary;
    mtm::Fir in1, in2, out1, out2;
    mtm::Frame frame{};
    computer::Output conditioned;
    uint32_t tick = 0;
    int32_t bootFrames = computer::timing::bootQuietFrames, pluckPulse = 0;
    void updateControls();
    void exchangeFrame(int32_t stringInput, int32_t haloInput);
    void writeAudio(bool internal);
    void updateIndicators();
    void ProcessSample() override;

public:
    Card();
};

Card::Card() {
    EnableNormalisationProbe();
}

void Card::updateControls() {
    controls.knobs[computer::MainKnob] = KnobVal(Main);
    controls.knobs[computer::LeftKnob] = KnobVal(X);
    controls.knobs[computer::RightKnob] = KnobVal(Y);
    controls.pitchConnected = Connected(CV1);
    controls.bendConnected = Connected(CV2);
    controls.audio1Connected = Connected(Audio1);
    controls.audio2Connected = Connected(Audio2);
    controls.pitchMv = CVInMillivolts(0);
    controls.bendMv = CVInMillivolts(1);
    controls.audio1Mv = AudioIn1Millivolts();
    controls.audio2Mv = AudioIn2Millivolts();
    controls.position = static_cast<computer::Position>(SwitchVal());
    settings = controller.update(controls);
}

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

void Card::updateIndicators() {
    CVOut1Millivolts(auxiliary.pitchMillivolts());
    CVOut2Millivolts(auxiliary.haloMillivolts());
    LedOn(0, pluckPulse > 0);
    LedOn(1, settings.external);
    LedBrightness(2, mtm::clip(auxiliary.dryLevelQ15() / 4, 0, 4095));
    LedBrightness(3, mtm::clip(auxiliary.haloLevelQ15() / 4, 0, 4095));
    LedOn(4, diagnostics.underruns.load() || diagnostics.inputOverruns.load() ||
                 diagnostics.callbackOverruns.load() || diagnostics.irqOverruns.load());
    LedOn(5, !InputsCalibrated() || !CVOutsCalibrated());
}

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
void runInstrument() {
    // Construct after clock setup, with static storage rather than the IRQ stack.
    static Card card;
    for (unsigned i = 0; i < timing::initialOutputFrames; ++i) {
        output.push(mtm::Frame{});
    }
    multicore_launch_core1(worker);
    card.Run();
}
} // namespace computer
