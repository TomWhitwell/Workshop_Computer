//
//  Tapegrade.cpp
//
//
//  Created by Adrian Vos on 25/05/2026.
//

#include "ComputerCard.h"

// Tapegrade
// Stereo cassette-warble processor for Workshop Computer.
//
// DSP features:
//
// - interpolated modulated delay
// - wow/flutter transport instability
// - stereo decorrelation
// - cassette tone shaping
// - soft saturation
// - hiss/crackle degradation
// - dynamic tape condition morphing
// - low CPU fixed-point DSP
//
// Designed for:
// - 48kHz ISR operation
// - RP2040 efficiency
// - stable real-time behaviour

class Tapegrade : public ComputerCard
{
public:

    // 4096-sample circular delay buffer.
    //
    // ~85ms at 48kHz.
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int BUFFER_MASK = BUFFER_SIZE - 1;

    int16_t delayBuffer[BUFFER_SIZE];

    int32_t writePos = 0;

    // Slow random wow drift.
    int32_t wowL = 0;
    int32_t wowR = 0;

    // Flutter oscillators.
    uint32_t flutterPhaseL = 0;
    uint32_t flutterPhaseR = 16384;

    // Random generator state.
    uint32_t rng = 1;

    // Tone filter memory.
    int32_t toneL = 0;
    int32_t toneR = 0;

    // Tape-condition modulation smoothing.
    int32_t tapeTypeCV = 0;

    //
    // Additional DSP state.
    //

    int32_t crackleLPFL = 0;
    int32_t crackleLPFR = 0;

    int32_t hissLPFL = 0;
    int32_t hissLPFR = 0;

    // Cheap xorshift RNG.
    inline uint32_t FastRandom()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    // LED brightness clamping
    
    inline uint16_t ClampLED(int32_t x)
    {
        if (x < 0) return 0;
        if (x > 4095) return 4095;
        return x;
    }
    
    // Triangle oscillator.
    inline int32_t Triangle(uint32_t phase)
    {
        int32_t x = (phase >> 20) & 2047;

        if (x > 1023)
        {
            x = 2047 - x;
        }

        return (x << 1) - 1023;
    }

    // Clamp helper.
    inline int32_t Clamp2048(int32_t x)
    {
        if (x > 2047) return 2047;
        if (x < -2048) return -2048;
        return x;
    }

    // Cheap tape-style soft saturation.
    inline int32_t SoftClip(int32_t x)
    {
        x = x + (x >> 1);

        int32_t ax = (x < 0) ? -x : x;

        x -= (x * (ax >> 5)) >> 6;

        return Clamp2048(x);
    }

    // Linear interpolated delay read.
    inline int32_t ReadDelayInterpolated(int32_t readPos)
    {
        int32_t indexA = (readPos >> 8) & BUFFER_MASK;
        int32_t indexB = (indexA + 1) & BUFFER_MASK;

        int32_t frac = readPos & 255;

        int32_t a = delayBuffer[indexA];
        int32_t b = delayBuffer[indexB];

        return a + (((b - a) * frac) >> 8);
    }

    virtual void ProcessSample() override
    {
        // Controls.
        int32_t mix = KnobVal(Knob::Main);

        int32_t depth =
            KnobVal(Knob::X) +
            (Connected(Input::CV1) ? (CVIn1() >> 1) : 0);

        int32_t instability =
            KnobVal(Knob::Y) +
            (Connected(Input::CV2) ? (CVIn2() >> 1) : 0);

        // Clamp controls.
        if (depth < 0) depth = 0;
        if (depth > 4095) depth = 4095;

        if (instability < 0) instability = 0;
        if (instability > 4095) instability = 4095;

        // Mono audio input.
        int32_t inputL = AudioIn1();
        int32_t inputR = inputL;

        // Tape saturation.
        inputL = SoftClip(inputL);
        inputR = inputL;

        // Audio-rate tape-condition modulation.
        //
        // Audio 2 dynamically morphs tape wear.
        int32_t tapeCV = AudioIn2();

        tapeTypeCV += (tapeCV - tapeTypeCV) >> 8;

        // Base tape condition from switch.
        int32_t tapeType = 0;

        switch (SwitchVal())
        {
            case Switch::Up:
                tapeType = 0;
                break;

            case Switch::Middle:
                tapeType = 1400;
                break;

            case Switch::Down:
                tapeType = 2700;
                break;
        }
        
        // Pulse1 injects temporary tape damage burst.
        if (PulseIn1RisingEdge())
        {
            tapeTypeCV = 4095;
        }

        // Apply Audio2 modulation.
        tapeType += tapeTypeCV >> 1;

        if (tapeType < 0) tapeType = 0;
        if (tapeType > 4095) tapeType = 4095;

        // Dynamic tape darkness.
        //
        // Higher tape wear = darker sound.
        int32_t toneShift =
            3 + (tapeType >> 10);

        // Nonlinear modulation depth scaling.
        int32_t modeDepth =
            (depth * 3) >> 1;
        
        // Older tapes become less stable.
        modeDepth += tapeType >> 4;
        
        //clamp modeDepth
        if (modeDepth > 8192)
        {
            modeDepth = 8192;
        }

        // Mono tape input.
        delayBuffer[writePos] = inputL;

        // Random wow drift.
        if ((writePos & 63) == 0)
        {
            wowL += ((int32_t)(FastRandom() & 31) - 15);
            wowR += ((int32_t)(FastRandom() & 31) - 15);

            wowL -= wowL >> 7;
            wowR -= wowR >> 7;
        }

        // Instability scaling.
        //
        // Y now affects:
        // - flutter speed
        // - flutter depth
        // - wow amount
        // - stereo drift

        int32_t instabilityDepth =
            512 + (instability >> 1);

        // Flutter oscillator speed.
        flutterPhaseL += 2500 + instability;
        flutterPhaseR += 2700 + instability;

        int32_t flutterL = Triangle(flutterPhaseL);
        int32_t flutterR = Triangle(flutterPhaseR);

        // Scale flutter by instability.
        flutterL = (flutterL * instabilityDepth) >> 10;
        flutterR = (flutterR * instabilityDepth) >> 10;

        // Scale wow by instability too.
        int32_t wowScale =
            512 + (instability >> 2);

        int32_t wowModL =
            (wowL * wowScale) >> 9;

        int32_t wowModR =
            (wowR * wowScale) >> 9;

        // Combined modulation.
        int32_t modL =
            wowModL + (flutterL << 1);

        int32_t modR =
            wowModR + (flutterR << 1);

        // Final modulation scaling.
        modL = (modL * modeDepth) >> 13;
        modR = (modR * modeDepth) >> 13;
        if (modL > 12000) modL = 12000;
        if (modL < -12000) modL = -12000;

        if (modR > 12000) modR = 12000;
        if (modR < -12000) modR = -12000;

        // Base tape delay.
        int32_t baseDelay = 1400 << 8;

        // Fractional modulated read positions.
        int32_t readL =
            ((writePos << 8) - baseDelay - (modL << 6));

        int32_t readR =
            ((writePos << 8) - baseDelay - (modR << 6));

        // Interpolated reads.
        int32_t wetL = ReadDelayInterpolated(readL);
        int32_t wetR = ReadDelayInterpolated(readR);

        // Tone shaping.
        toneL += (wetL - toneL) >> toneShift;
        toneR += (wetR - toneR) >> toneShift;

        wetL = toneL;
        wetR = toneR;

        //
        // Continuous degradation scaling.
        //

        // Hiss amount.
        int32_t hissAmount = tapeType >> 8;

        // Crackle only appears on heavily damaged tape.
        int32_t crackleAmount =
            (tapeType > 2048)
                ? (tapeType - 2048)
                : 0;
        
        // Pulse2 forces heavy crackle while high.
        if (PulseIn2())
        {
            crackleAmount = 2048;
        }

        //
        // Tape hiss.
        //

        int32_t noiseL =
            ((int32_t)(FastRandom() & 255) - 128);

        int32_t noiseR =
            ((int32_t)(FastRandom() & 255) - 128);

        hissLPFL += (noiseL - hissLPFL) >> 4;
        hissLPFR += (noiseR - hissLPFR) >> 4;

        int32_t hissL = (noiseL - hissLPFL);
        int32_t hissR = (noiseR - hissLPFR);

        wetL += (hissL * hissAmount) >> 8;
        wetR += (hissR * hissAmount) >> 8;

        //
        // Crackle impulses.
        //

        if (crackleAmount > 0)
        {
            int32_t crackleMask =
                2047 - (crackleAmount >> 1);
            
            if (crackleMask < 15)
            {
                crackleMask = 15;
            }

            if ((FastRandom() & crackleMask) == 0)
            {
                crackleLPFL +=
                    (((int32_t)(FastRandom() & 255) - 128) << 2);
            }

            if ((FastRandom() & crackleMask) == 0)
            {
                crackleLPFR +=
                    (((int32_t)(FastRandom() & 255) - 128) << 2);
            }
        }

        // Crackle decay.
        crackleLPFL -= crackleLPFL >> 4;
        crackleLPFR -= crackleLPFR >> 4;

        wetL += (crackleLPFL * crackleAmount) >> 11;
        wetR += (crackleLPFR * crackleAmount) >> 11;

        // Protect wet path.
        wetL = SoftClip(wetL);
        wetR = SoftClip(wetR);
        

        // Wet/dry mix.
        int32_t dryGain = 4095 - mix;

        int32_t outL =
            ((inputL * dryGain) + (wetL * mix)) >> 12;

        int32_t outR =
            ((inputR * dryGain) + (wetR * mix)) >> 12;

        // Final output protection.
        outL = Clamp2048(outL);
        outR = Clamp2048(outR);

        AudioOut1(outL);
        AudioOut2(outR);
        
        // CV passthrough with knob attenuation.
        int32_t cvOut1 =
            (CVIn1() * KnobVal(Knob::X)) >> 12;

        int32_t cvOut2 =
            (CVIn2() * KnobVal(Knob::Y)) >> 12;

        CVOut1(cvOut1);
        CVOut2(cvOut2);

        // LED feedback.
        LedBrightness(0, ClampLED(modL + 2048));
        LedBrightness(1, instability);
        LedBrightness(2, ClampLED(modR + 2048));
        LedBrightness(3, mix);
        LedBrightness(4, tapeType);

        // Advance circular buffer.
        writePos++;
        writePos &= BUFFER_MASK;
    }
};

int main()
{
    // 144MHz is stable and reduces
    // some converter artefacts.
    set_sys_clock_khz(144000, true);

    Tapegrade tapegrade;

    // Required for jack detection.
    tapegrade.EnableNormalisationProbe();

    tapegrade.Run();
}

