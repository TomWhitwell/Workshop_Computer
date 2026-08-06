//
//  LoChoVibes.cpp
//
//  Created by Adrian Vos on 25/05/2026.
//

#include "ComputerCard.h"
#include <cstdint>

class LoChoVibes : public ComputerCard
{
public:

    enum LFOShape
    {
        Triangle,
        Sine,
        RandomDrift
    };

    static constexpr int32_t SAMPLE_RATE = 48000;
    static constexpr int32_t DELAY_SIZE = 2048;
    static constexpr uint32_t SINE_TABLE_SIZE = 256;

    int16_t delayBufferL[DELAY_SIZE] = {};
    int16_t delayBufferR[DELAY_SIZE] = {};

    int32_t writeIndex = 0;

    // External clocking.

    uint32_t samplesSincePulse = 0;
    uint32_t lastPulseInterval = 24000;

    bool externalClock = false;

    uint32_t targetIncrement = 900;
    uint32_t currentIncrement = 900;
    int32_t driftCurrent = 0;
    
    uint32_t randomSeed = 12345;
    uint32_t FastRandom()
    {
        randomSeed =
            randomSeed * 1664525u +
            1013904223u;

        return randomSeed;
    }

    // Unsigned overflow is intentional.

    uint32_t lfoPhase = 0;

    LFOShape currentShape = Triangle;

    int32_t overlayTimer = 0;

    static constexpr int16_t sineTable[SINE_TABLE_SIZE] =
    {
        0,   50,  100,  151,  201,  251,  300,  350,
      399,  449,  497,  546,  594,  642,  690,  737,
      783,  830,  875,  920,  965, 1009, 1052, 1095,
     1137, 1179, 1219, 1259, 1299, 1337, 1375, 1411,
     1447, 1483, 1517, 1550, 1582, 1614, 1644, 1674,
     1702, 1729, 1756, 1781, 1805, 1828, 1850, 1871,
     1891, 1910, 1927, 1944, 1959, 1973, 1986, 1997,
     2008, 2017, 2025, 2032, 2037, 2041, 2045, 2046,
     2047, 2046, 2045, 2041, 2037, 2032, 2025, 2017,
     2008, 1997, 1986, 1973, 1959, 1944, 1927, 1910,
     1891, 1871, 1850, 1828, 1805, 1781, 1756, 1729,
     1702, 1674, 1644, 1614, 1582, 1550, 1517, 1483,
     1447, 1411, 1375, 1337, 1299, 1259, 1219, 1179,
     1137, 1095, 1052, 1009,  965,  920,  875,  830,
      783,  737,  690,  642,  594,  546,  497,  449,
      399,  350,  300,  251,  201,  151,  100,   50,
        0,  -50, -100, -151, -201, -251, -300, -350,
     -399, -449, -497, -546, -594, -642, -690, -737,
     -783, -830, -875, -920, -965,-1009,-1052,-1095,
    -1137,-1179,-1219,-1259,-1299,-1337,-1375,-1411,
    -1447,-1483,-1517,-1550,-1582,-1614,-1644,-1674,
    -1702,-1729,-1756,-1781,-1805,-1828,-1850,-1871,
    -1891,-1910,-1927,-1944,-1959,-1973,-1986,-1997,
    -2008,-2017,-2025,-2032,-2037,-2041,-2045,-2046,
    -2047,-2046,-2045,-2041,-2037,-2032,-2025,-2017,
    -2008,-1997,-1986,-1973,-1959,-1944,-1927,-1910,
    -1891,-1871,-1850,-1828,-1805,-1781,-1756,-1729,
    -1702,-1674,-1644,-1614,-1582,-1550,-1517,-1483,
    -1447,-1411,-1375,-1337,-1299,-1259,-1219,-1179,
    -1137,-1095,-1052,-1009, -965, -920, -875, -830,
     -783, -737, -690, -642, -594, -546, -497, -449,
     -399, -350, -300, -251, -201, -151, -100,  -50
    };

    void ProcessSample() override
    {
        samplesSincePulse++;
        
        // Mono input normalization.

        int32_t mono =
            (AudioIn1() + AudioIn2()) >> 1;

        int32_t inL = mono;
        int32_t inR = mono;
        

        // Controls.

        int32_t rateKnob =
            KnobVal(Knob::Main);

        int32_t depthKnob =
            KnobVal(Knob::X);

        int32_t characterKnob =
            KnobVal(Knob::Y);
        
        // CV modulation.

        int32_t depthCV =
            CVIn1() - 2048;

        int32_t characterCV =
            CVIn2() - 2048;

        // Apply attenuated modulation.

        depthKnob += depthCV >> 1;
        characterKnob += characterCV >> 1;

        // Clamp ranges.

        if (depthKnob < 0)
            depthKnob = 0;

        if (depthKnob > 4095)
            depthKnob = 4095;

        if (characterKnob < 0)
            characterKnob = 0;

        if (characterKnob > 4095)
            characterKnob = 4095;

        // Switch handling.

        if (SwitchChanged() &&
            SwitchVal() == Switch::Down)
        {
            currentShape =
                static_cast<LFOShape>(
                    (static_cast<int>(currentShape) + 1) % 3
                );

            overlayTimer =
                SAMPLE_RATE / 2;
        }

        bool vibratoMode =
            (SwitchVal() == Switch::Up);
        
        // External clock detection.

        if (PulseIn1RisingEdge())
        {
            lastPulseInterval =
                samplesSincePulse;

            samplesSincePulse = 0;

            externalClock = true;

            if (lastPulseInterval > 0)
            {
                targetIncrement =
                    0xFFFFFFFFu /
                    lastPulseInterval;
            }
        }
        
        // Return to internal clock
        // if pulses disappear.

        if (samplesSincePulse >
            SAMPLE_RATE * 2)
        {
            externalClock = false;
        }
        

        // Character control.
        // CCW = lo-fi.
        // Center = neutral.
        // CW = compression.

        int32_t centered =
            characterKnob - 2048;

        if (centered > -128 &&
            centered < 128)
        {
            centered = 0;
        }

        // LO-FI SIDE.

        if (centered < 0)
        {
            int32_t lofi =
                -centered;

            // Level reduction.

            inL =
                (inL * (4096 - (lofi >> 1))) >> 12;

            inR =
                (inR * (4096 - (lofi >> 1))) >> 12;

            // Progressive bit reduction.

            int32_t crush =
                1 + (lofi >> 9);

            if (crush > 6)
                crush = 6;

            inL =
                (inL >> crush) << crush;

            inR =
                (inR >> crush) << crush;

            // Stronger tape saturation.

            inL =
                SoftClip(inL, lofi);

            inR =
                SoftClip(inR, lofi);
        }

        // COMPRESSED SIDE.

        else if (centered > 0)
        {
            int32_t comp =
            centered;
            
            // More drive.
            
            inL =
            (inL * (4096 + (comp << 1))) >> 12;
            
            inR =
            (inR * (4096 + (comp << 1))) >> 12;
            
            inL =
            SoftLimit(inL);
            
            inR =
            SoftLimit(inR);
            
            inL =
            SoftClip(inL, comp >> 3);
            
            inR =
            SoftClip(inR, comp >> 3);
            
            // Makeup gain.
            
            inL =
            (inL * 7) >> 2;
            
            inR =
            (inR * 7) >> 2;
        }
            //centre position
            
        else
        {
                inL = SoftClip(inL, 0);
                inR = SoftClip(inR, 0);
        }
        

        // Generate LFO.

        int32_t lfo =
            GenerateLFO(rateKnob);

        // Reduced modulation depth.

        int32_t depth =
            8 + (depthKnob >> 3);

        int32_t modulation =
            (lfo * depth) >> 11;

        // Stereo delay offsets.

        int32_t delayL =
            40 + modulation;

        int32_t delayR =
            104 - modulation;

        if (delayL < 1)
            delayL = 1;

        if (delayR < 1)
            delayR = 1;

        // Read delay lines.

        int32_t outL =
            ReadDelay(delayBufferL, delayL);

        int32_t outR =
            ReadDelay(delayBufferR, delayR);

        // Chorus mix.
        // Vibrato remains fully wet.

        if (!vibratoMode)
        {
            outL = (inL + outL) >> 1;
            outR = (inR + outR) >> 1;

            outL = (outL * 9) >> 3;
            outR = (outR * 9) >> 3;
        }

        // Write delay buffers.

        delayBufferL[writeIndex] = inL;
        delayBufferR[writeIndex] = inR;

        writeIndex++;
        writeIndex &= (DELAY_SIZE - 1);

        // LEDs.

        UpdateLEDs(rateKnob,
                   depthKnob,
                   characterKnob,
                   lfo);

        // Outputs.
        
        // CV modulation outputs.

        CVOut1(ClampLED(lfo + 2048));
        CVOut2(ClampLED(2048 - lfo));
        
        // Audio outputs
        
        AudioOut1(ClampAudio(outL));
        AudioOut2(ClampAudio(outR));
    }

private:

    int16_t ClampAudio(int32_t v)
    {
        if (v > 2047)
            return 2047;

        if (v < -2048)
            return -2048;

        return static_cast<int16_t>(v);
    }

    int32_t SoftClip(int32_t input,
                     int32_t amount)
    {
        int32_t drive =
            2048 + (amount >> 2);

        int32_t x =
            (input * drive) >> 11;

        // Tape-style soft knee.

        if (x > 1400)
        {
            x =
                1400 + ((x - 1400) >> 3);
        }

        if (x < -1400)
        {
            x =
                -1400 + ((x + 1400) >> 3);
        }

        return x;
    }

    int32_t SoftLimit(int32_t x)
    {
        if (x > 1024)
        {
            x =
                1024 + ((x - 1024) >> 2);
        }

        if (x < -1024)
        {
            x =
                -1024 + ((x + 1024) >> 2);
        }

        return x;
    }

    int32_t GenerateLFO(int32_t rate)
    {
        // Slightly slower overall rate.

        uint32_t internalIncrement =
            900 + (rate << 8);

        if (externalClock)
        {
            int32_t delta =
                static_cast<int32_t>(targetIncrement) -
                static_cast<int32_t>(currentIncrement);

            currentIncrement +=
                delta >> 4;

            lfoPhase += currentIncrement;
        }
        else
        {
            lfoPhase += internalIncrement;
        }

        switch(currentShape)
        {
            case Triangle:
            {
                int32_t tri =
                    (lfoPhase >> 19) & 0x1FFF;

                if (tri > 4095)
                    tri = 8191 - tri;

                return tri - 2048;
            }

            case Sine:
            {
                uint32_t index =
                    (lfoPhase >> 24) & 0xFF;

                return sineTable[index];
            }

            case RandomDrift:
            {
                uint32_t index =
                    (lfoPhase >> 25) & 0xFF;

                // Slow wow component.

                int32_t slowSine =
                    sineTable[index] >> 1;

                // Wandering drift.

                if ((FastRandom() & 255) == 0)
                {
                    driftCurrent +=
                        ((int32_t)(FastRandom() & 31)) - 16;

                    if (driftCurrent > 768)
                        driftCurrent = 768;

                    if (driftCurrent < -768)
                        driftCurrent = -768;
                }

                return
                    slowSine +
                    driftCurrent;
            }
        }

        return 0;
    }

    int32_t ReadDelay(const int16_t* buffer,
                      int32_t delaySamples)
    {
        int32_t readIndex =
            writeIndex - delaySamples;

        while (readIndex < 0)
            readIndex += DELAY_SIZE;

        return
            buffer[
                readIndex &
                (DELAY_SIZE - 1)
            ];
    }

    uint16_t ClampLED(int32_t v)
    {
        if (v < 0)
            return 0;

        if (v > 4095)
            return 4095;

        return static_cast<uint16_t>(v);
    }

    void UpdateLEDs(int32_t rate,
                    int32_t depth,
                    int32_t character,
                    int32_t lfo)
    {
        if (overlayTimer > 0)
        {
            overlayTimer--;

            LedOff(0);
            LedOff(1);
            LedOff(2);
            LedOff(3);
            LedOff(4);
            LedOff(5);

            switch(currentShape)
            {
                case Triangle:

                    LedOn(0);
                    LedOn(1);

                    break;

                case Sine:

                    LedOn(2);
                    LedOn(3);

                    break;

                case RandomDrift:
                {
                    LedOn(4);
                    LedOn(5);
                    break;
                }
            }

            return;
        }

        // Character display.
        // Center = both off.

        int32_t centered =
            character - 2048;

        if (centered > -128 &&
            centered < 128)
        {
            centered = 0;
        }

        int32_t lofiAmount = 0;
        int32_t compAmount = 0;

        if (centered < 0)
        {
            lofiAmount =
                (-centered) << 1;
        }
        else
        {
            compAmount =
                centered << 1;
        }

        // LED 0 = rate

        if (externalClock)
        {
            LedBrightness(0,
                (samplesSincePulse < 2000)
                    ? 4095
                    : 0);
        }
        else
        {
            switch(currentShape)
            {
                case Triangle:
                {
                    uint32_t phase =
                        lfoPhase >> 24;

                    LedBrightness(0,
                        (phase < 128)
                            ? 4095
                            : 0);

                    break;
                }

                case Sine:
                {
                    int32_t sineDisplay =
                        2048 + ((lfo * 3) >> 1);

                    LedBrightness(0,
                        ClampLED(sineDisplay));

                    break;
                }

                case RandomDrift:
                {
                    LedBrightness(0,
                        ClampLED(
                            2048 + (driftCurrent << 2)));

                    break;
                }
            }
        }

        // LED 1 = modulation depth

        int32_t modDisplay =
            ((lfo < 0 ? -lfo : lfo) * depth)
            >> 11;

        LedBrightness(1,
            ClampLED(modDisplay << 1));

        // LED 2 = CCW lo-fi amount

        LedBrightness(2,
            ClampLED(lofiAmount));

        // LED 3 = CW compression amount

        LedBrightness(3,
            ClampLED(compAmount));

        // LED pair = modulation animation

        switch(currentShape)
        {
            // Triangle = pendulum

            case Triangle:
            {
                LedBrightness(4,
                    ClampLED(lfo + 2048));

                LedBrightness(5,
                    ClampLED(2048 - lfo));

                break;
            }

            // Sine = both LEDs breathe together

            case Sine:
            {
                uint16_t brightness =
                    ClampLED(lfo + 2048);

                LedBrightness(4, brightness);
                LedBrightness(5, brightness);

                break;
            }

            // Random Drift = wandering imbalance

            case RandomDrift:
            {
                int32_t driftBrightness =
                    2048 + (driftCurrent << 2);;

                LedBrightness(4,
                    ClampLED(driftBrightness));

                LedBrightness(5,
                    ClampLED(4095 - driftBrightness));

                break;
            }
        }
    }
};

int main()
{
    LoChoVibes card;
    card.Run();
}

