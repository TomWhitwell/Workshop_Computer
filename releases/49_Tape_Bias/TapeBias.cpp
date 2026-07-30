// TapeBias.cpp
// First-pass 1960s-inspired reel-to-reel record/replay model.

#include "ComputerCard.h"

class TapeBias : public ComputerCard
{
public:
    static constexpr int BUFFER_SIZE = 128;
    static constexpr int BUFFER_MASK = BUFFER_SIZE - 1;

    int16_t tape[BUFFER_SIZE] = {};
    int32_t writePosition = 0;
    int32_t recordLowpass = 0;
    int32_t replayLowpass = 0;
    int32_t fluxMemory = 0;
    int32_t noiseState = 1;

    inline int32_t ClampAudio(int32_t value)
    {
        if (value > 2047) return 2047;
        if (value < -2048) return -2048;
        return value;
    }

    inline uint16_t ClampLed(int32_t value)
    {
        if (value < 0) return 0;
        if (value > 4095) return 4095;
        return value;
    }

    inline int32_t Absolute(int32_t value)
    {
        return value < 0 ? -value : value;
    }

    inline int32_t Noise()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return (noiseState & 255) - 128;
    }

    inline int32_t MagneticCurve(int32_t input, int32_t drive, int32_t biasError)
    {
        // This averaged curve stands in for the ultrasonic AC-bias cycle at 48 kHz.
        int32_t x = (input * drive) >> 11;
        int32_t magnitude = Absolute(x);
        x -= (x * magnitude) >> 12;

        // Under-bias leaves a rougher, more asymmetric record characteristic.
        if (biasError < 0)
        {
            x += ((x * magnitude) >> 13) * (-biasError) / 2048;
        }

        return ClampAudio(x);
    }

    virtual void ProcessSample() override
    {
        int32_t recordLevel = KnobVal(Knob::Main);
        int32_t bias = KnobVal(Knob::X);
        int32_t formulation = KnobVal(Knob::Y);

        if (Connected(Input::CV1)) bias += CVIn1() >> 1;
        if (Connected(Input::CV2)) formulation += CVIn2() >> 1;

        if (bias < 0) bias = 0;
        if (bias > 4095) bias = 4095;
        if (formulation < 0) formulation = 0;
        if (formulation > 4095) formulation = 4095;

        int32_t input = AudioIn1();
        int32_t biasError = bias - 2048;
        int32_t underBias = biasError < 0 ? -biasError : 0;
        int32_t overBias = biasError > 0 ? biasError : 0;

        // The three transport speeds determine the record/replay bandwidth.
        int32_t speedShift = 5;
        switch (SwitchVal())
        {
            case Switch::Up: speedShift = 6; break;      // 3.75 ips
            case Switch::Middle: speedShift = 5; break;  // 7.5 ips
            case Switch::Down: speedShift = 4; break;    // 15 ips
        }

        // Record EQ: faster transport and higher-output tape retain more treble.
        recordLowpass += (input - recordLowpass) >> speedShift;
        int32_t highBand = input - recordLowpass;
        int32_t recordEq = input + ((highBand * (1024 + (formulation >> 2))) >> 12);

        int32_t drive = 2048 + (recordLevel >> 1) + (formulation >> 3);
        int32_t recorded = MagneticCurve(recordEq, drive, biasError);

        // A short virtual head-spacing delay separates record and replay.
        tape[writePosition] = recorded;
        int32_t replayed = tape[(writePosition - 48) & BUFFER_MASK];
        writePosition = (writePosition + 1) & BUFFER_MASK;

        // Over-bias reduces replay treble; under-bias retains it but adds roughness.
        int32_t replayShift = speedShift + (overBias >> 10);
        if (replayShift > 8) replayShift = 8;
        replayLowpass += (replayed - replayLowpass) >> replayShift;
        int32_t replayHigh = replayed - replayLowpass;
        int32_t output = replayLowpass + ((replayHigh * (2048 - (overBias >> 1))) >> 11);

        // Magnetic memory gives repeated material a small, slowly changing imprint.
        fluxMemory += (recorded - fluxMemory) >> (7 + (formulation >> 11));
        output += fluxMemory >> 6;

        // Low-output ferric tape is noisier; under-bias raises the residual noise.
        int32_t hiss = (Noise() * (256 + (4095 - formulation) / 6 + (underBias >> 2))) >> 11;
        output += hiss;
        output = ClampAudio(output);

        if (PulseIn1RisingEdge())
        {
            fluxMemory = 0;
            replayLowpass = 0;
            for (int i = 0; i < BUFFER_SIZE; ++i) tape[i] = 0;
        }

        AudioOut1(output);
        AudioOut2(output);
        CVOut1((bias - 2048) >> 1);
        CVOut2(fluxMemory);

        LedBrightness(0, recordLevel);
        LedBrightness(1, bias);
        LedBrightness(2, ClampLed(Absolute(fluxMemory) << 1));
        LedBrightness(3, ClampLed(4095 - (overBias << 1)));
        LedBrightness(4, formulation);
        LedBrightness(5, ClampLed((2 << speedShift) * 16));
    }
};

int main()
{
    set_sys_clock_khz(144000, true);

    TapeBias tapeBias;
    tapeBias.EnableNormalisationProbe();
    tapeBias.Run();
}
