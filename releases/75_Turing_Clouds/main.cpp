/*
Turing Clouds — Program card for Music Thing Modular Workshop Computer

A granular texture generator driven by a Turing Machine shift register.
Audio In 1 is continuously recorded into a 1-second buffer. The Turing
Machine decides when grains trigger, and from where in the buffer they
play. The result is an evolving, semi-predictable cloud of sound that
sits somewhere between delay, reverb, and generative texture.
*/

#include "ComputerCard.h"
#include <stdint.h>

// === Constants ==============================================================
constexpr int kSampleRate = 48000;
constexpr int kBufferSamples = kSampleRate;      // 1 second buffer
constexpr int kMaxGrains = 4;
constexpr int kFixedPoint = 16;
constexpr int32_t kFixedOne = 1 << kFixedPoint;

// === Turing Machine =========================================================
class TuringMachine
{
public:
    TuringMachine(uint32_t seed = 0xACE1u) : seed_(seed ? seed : 0xACE1u) {}

    void SetLength(int len) { length_ = len < 2 ? 2 : (len > 16 ? 16 : len); }
    int Length() const { return length_; }

    // probability: 0..4095 maps to almost-always-rotate to almost-never-rotate
    void SetProbability(int prob) { probability_ = prob; }

    void Clock()
    {
        uint16_t mask = (1u << length_) - 1u;
        uint32_t rnd = LCG() >> 20; // 0..4095-ish
        uint16_t msb = (register_ >> (length_ - 1)) & 1u;
        bool invert = (int32_t)rnd < probability_;
        register_ = ((register_ << 1) | (invert ? (1u - msb) : msb)) & mask;
        position_ = (position_ + 1) & (length_ - 1);
    }

    uint16_t Register() const { return register_; }
    uint8_t Bits(int start, int count) const
    {
        return (register_ >> start) & ((1u << count) - 1u);
    }

    void Reset()
    {
        register_ = LCG() & 0xFFFFu;
        position_ = 0;
    }

private:
    uint32_t seed_;
    uint16_t register_ = 0;
    int length_ = 16;
    int probability_ = 2048;
    int position_ = 0;

    uint32_t LCG()
    {
        seed_ = 1664525u * seed_ + 1013904223u;
        return seed_;
    }
};

// === Grain ==================================================================
struct Grain
{
    int32_t readPos = 0;      // fixed-point buffer position
    int32_t increment = 0;    // normally +1.0
    int32_t env = 0;          // envelope phase, 0..kFixedOne
    int32_t envDelta = 0;     // envelope increment per sample
    int32_t size = 0;         // grain size in samples
    bool active = false;
};

// === Main Card ==============================================================
class TuringClouds : public ComputerCard
{
public:
    TuringClouds()
    {
        for (int i = 0; i < kBufferSamples; ++i)
            buffer_[i] = 0;
        for (int g = 0; g < kMaxGrains; ++g)
            grains_[g] = Grain();
    }

    virtual void ProcessSample() override
    {
        // Record audio input into circular buffer
        int16_t inL = AudioIn1();
        buffer_[writePos_] = inL;
        writePos_ = (writePos_ + 1) % kBufferSamples;

        // Read controls
        int32_t mainKnob = KnobVal(Knob::Main);
        int32_t xKnob = KnobVal(Knob::X);
        int32_t yKnob = KnobVal(Knob::Y);
        Switch sw = SwitchVal();

        // Freeze mode: stop writing buffer when switch held down
        bool freeze = (sw == Switch::Down);
        if (freeze)
        {
            // In freeze, back up write pointer so we keep re-reading the frozen segment
            writePos_ = (writePos_ + kBufferSamples - 1) % kBufferSamples;
        }

        // Clock handling
        bool useExternalClock = Connected(Input::Pulse1);
        bool trigger = false;
        if (useExternalClock)
        {
            trigger = PulseIn1RisingEdge();
        }
        else
        {
            // Internal clock: Main knob sets period from ~20ms to ~2s
            int32_t minPeriod = kSampleRate / 50;   // 20 ms
            int32_t maxPeriod = kSampleRate * 2;    // 2 s
            int32_t period = minPeriod + ((maxPeriod - minPeriod) * (4095 - mainKnob) >> 12);
            if (++internalClock_ >= period)
            {
                internalClock_ = 0;
                trigger = true;
            }
        }

        // Switch Up = Clouds mode, Middle = Delay mode
        mode_ = (sw == Switch::Middle) ? 1 : 0;

        // Update Turing Machine on trigger
        if (trigger)
        {
            // Length from Main knob when external clock, else fixed 16
            int len = useExternalClock ? (2 + (mainKnob * 15 >> 12)) : 16;
            turing_.SetLength(len);
            turing_.SetProbability(xKnob);
            turing_.Clock();

            uint16_t reg = turing_.Register();

            // Trigger grains based on lower bits
            for (int g = 0; g < kMaxGrains; ++g)
            {
                if ((reg >> g) & 1u)
                {
                    StartGrain(g, reg, yKnob);
                }
            }

            // Pulse output mirrors clock/trigger
            PulseOut1(true);
        }
        else
        {
            PulseOut1(false);
        }

        // Process grains and mix output
        int32_t mixL = 0;
        int32_t mixR = 0;
        int activeGrains = 0;

        for (int g = 0; g < kMaxGrains; ++g)
        {
            Grain &gr = grains_[g];
            if (!gr.active) continue;

            activeGrains++;

            // Read sample with linear interpolation
            int32_t idx = gr.readPos >> kFixedPoint;
            int32_t frac = gr.readPos & (kFixedOne - 1);
            int32_t s0 = buffer_[idx % kBufferSamples];
            int32_t s1 = buffer_[(idx + 1) % kBufferSamples];
            int32_t sample = s0 + (((s1 - s0) * frac) >> kFixedPoint);

            // Apply triangular envelope
            int32_t env = TriangleEnv(gr.env, gr.size);
            sample = (sample * env) >> kFixedPoint;

            // Pan grains alternately left/right
            if (g & 1)
                mixR += sample;
            else
                mixL += sample;

            // Advance grain
            gr.readPos += gr.increment;
            gr.env += gr.envDelta;
            if (gr.env >= gr.size)
            {
                gr.active = false;
            }
        }

        // Dry/wet mix: Y knob when in delay mode, mostly wet in clouds mode
        int32_t dry = inL;
        int32_t wet = (mixL + mixR) >> 1;

        int32_t output;
        if (mode_ == 1)
        {
            // Delay mode: Y controls feedback/density mix
            int32_t feedback = yKnob >> 1; // 0..2047
            output = dry + ((wet * feedback) >> 12);
        }
        else
        {
            // Clouds mode: blend dry with cloud texture
            int32_t wetAmount = 2048 + (yKnob >> 3); // ~50..100%
            if (wetAmount > 4095) wetAmount = 4095;
            output = dry + ((wet * wetAmount) >> 12);
        }

        // Soft clip
        if (output > 2047) output = 2047;
        if (output < -2048) output = -2048;

        AudioOut1((int16_t)output);
        AudioOut2((int16_t)mixR); // raw cloud on right channel

        // CV outputs: CV1 = active grain count, CV2 = Turing register value
        CVOut1((int16_t)((activeGrains * 512) - 2048));
        CVOut2((int16_t)(((int32_t)turing_.Register() * 4095) / 65535 - 2048));

        // LEDs show Turing register top 6 bits
        uint16_t reg = turing_.Register();
        for (int i = 0; i < 6; ++i)
        {
            LedOn(i, (reg >> (i + 10)) & 1u);
        }
    }

private:
    int16_t buffer_[kBufferSamples];
    int writePos_ = 0;
    int internalClock_ = 0;
    int mode_ = 0;

    TuringMachine turing_;
    Grain grains_[kMaxGrains];

    void StartGrain(int g, uint16_t reg, int32_t yKnob)
    {
        Grain &gr = grains_[g];
        if (gr.active) return; // wait for grain to finish

        // Grain size: 20ms to 500ms from Y knob + CV
        int32_t minSize = kSampleRate / 50;   // 20ms
        int32_t maxSize = kSampleRate / 2;    // 500ms
        int32_t cv1 = CVIn1() + 2048; // 0..4095
        int32_t sizeControl = yKnob + cv1 - 2048;
        if (sizeControl < 0) sizeControl = 0;
        if (sizeControl > 4095) sizeControl = 4095;
        gr.size = minSize + ((maxSize - minSize) * sizeControl >> 12);
        if (gr.size < 1) gr.size = 1;

        // Position in buffer: use upper bits of register + CV2 offset
        int32_t cv2 = CVIn2() + 2048;
        int32_t posControl = ((reg >> 8) << 4) + (cv2 >> 3);
        posControl &= 0xFFFF;
        int32_t targetDelay = (posControl * (kBufferSamples - gr.size)) >> 16;
        int32_t pos = writePos_ - targetDelay - gr.size;
        while (pos < 0) pos += kBufferSamples;
        gr.readPos = pos << kFixedPoint;
        gr.increment = kFixedOne; // normal speed
        gr.env = 0;
        gr.envDelta = (kFixedOne * 2) / gr.size;
        gr.active = true;
    }

    int32_t TriangleEnv(int32_t phase, int32_t size)
    {
        // Triangle window: rises then falls over grain duration
        int32_t half = size >> 1;
        if (half < 1) half = 1;
        if (phase < half)
        {
            return (phase << kFixedPoint) / half;
        }
        else
        {
            return (((size - phase) << kFixedPoint) / half);
        }
    }
};

int main()
{
    set_sys_clock_khz(144000, true);
    TuringClouds card;
    card.Run();
}
