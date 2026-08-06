//
//  DualQuantPitch.cpp
//
//
//  Created by Adrian Vos on 16/05/2026.
//


#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

class DualQuantPitch : public ComputerCard
{
public:

    // ============================================================
    // Workshop Computer granular pitch shifter
    //
    // The goal here is not perfect transparency.
    // We intentionally embrace a rough granular texture.
    //
    // Everything inside ProcessSample() must remain ISR-safe:
    // - no floats
    // - no dynamic allocation
    // - no expensive library calls
    // - no blocking operations
    //
    // ============================================================

    // ============================================================
    // Fixed-point format
    //
    // Q16.16:
    // upper 16 bits = integer
    // lower 16 bits = fraction
    //
    // Example:
    // 65536 = 1.0
    // 32768 = 0.5
    // 131072 = 2.0
    //
    // ============================================================

    static const int32_t FP_SHIFT = 16;
   
    // ============================================================
    // Circular audio buffer
    //
    // Shared between both pitch shifters.
    //
    // Power-of-two size allows very fast wrapping
    // using bitmask operations instead of modulo.
    //
    // ============================================================

    static const int32_t BUFFER_SIZE = 2048;
    static const int32_t BUFFER_MASK = BUFFER_SIZE - 1;

    // ============================================================
    // Grain settings
    //
    // Larger grains:
    // - smoother
    // - more latency
    //
    // Smaller grains:
    // - rougher
    // - more glitchy
    //
    // ============================================================

    static const int32_t GRAIN_SIZE = 512;
    static const int32_t HALF_GRAIN = 256;

    // ============================================================
    // Scale definitions
    // ============================================================

  
    // Audio state
    // ============================================================

    int16_t audioBuffer[BUFFER_SIZE];

    int32_t writeHead = 0;

    int32_t readHeadA =
        (-GRAIN_SIZE & BUFFER_MASK)
        << FP_SHIFT;
    int32_t readHeadB =
    ((-GRAIN_SIZE - HALF_GRAIN)
     & BUFFER_MASK)
    << FP_SHIFT;

    int32_t grainPhaseA = 0;
    int32_t grainPhaseB = HALF_GRAIN;

    

    // ============================================================
    // Constructor
    // ============================================================

    DualQuantPitch()
    {
        // 144MHz is a good balance between:
        // - CPU headroom
        // - ADC stability
        // - reduced noise artefacts

        set_sys_clock_khz(144000, true);
        
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            audioBuffer[i] = 0;
        }
    }

    // ============================================================
    // Main audio ISR
    //
    // Runs at 48kHz.
    //
    // This must complete in under ~20 microseconds.
    // ============================================================

    virtual void ProcessSample()
    {
        // ========================================================
        // Switch handling
        // ========================================================

        

        bool quantised =
            (SwitchVal() == Switch::Middle);

        // ========================================================
        // Audio input
        // ========================================================

        int32_t input = AudioIn1();

        // Store incoming audio in circular delay buffer.

        audioBuffer[writeHead] = input;

        // ========================================================
        // Read controls
        // ========================================================

        int32_t mainPitch =
            KnobToSemitones(
                KnobVal(Knob::Main)
            );

        int32_t pitchA =
            mainPitch +
            KnobToSemitones(
                KnobVal(Knob::X)
            ) +
            CVToSemitones(CVIn1());

        int32_t pitchB =
            mainPitch +
            KnobToSemitones(
                KnobVal(Knob::Y)
            ) +
            CVToSemitones(CVIn2());

        // ========================================================
        // Quantisation
        // ========================================================

        if (quantised)
        {
            pitchA = QuantisePitch(pitchA);
            pitchB = QuantisePitch(pitchB);
        }

        // ========================================================
        // Convert semitones to playback ratios
        // ========================================================

        int32_t ratioA =
            SemitoneToRatio(pitchA);

        int32_t ratioB =
            SemitoneToRatio(pitchB);

        // ========================================================
        // Generate audio
        // ========================================================

        int32_t outA =
            ReadInterpolated(readHeadA,
                             grainPhaseA);

        int32_t outB =
            ReadInterpolated(readHeadB,
                             grainPhaseB);

        // Prevent overflow/clipping

        if (outA > 32767) outA = 32767;
        if (outA < -32768) outA = -32768;

        if (outB > 32767) outB = 32767;
        if (outB < -32768) outB = -32768;

        AudioOut1(outA);
        AudioOut2(outB);
        

        // ========================================================
        // Advance read heads
        // ========================================================

        readHeadA += ratioA;
        readHeadB += ratioB;

        grainPhaseA++;
        grainPhaseB++;

        // Restart grains periodically.
        // This prevents runaway drift and creates
        // the granular overlap texture.

        if (grainPhaseA >= GRAIN_SIZE)
        {
            grainPhaseA = 0;

            readHeadA =
                ((writeHead - GRAIN_SIZE)
                & BUFFER_MASK)
                << FP_SHIFT;
        }

        if (grainPhaseB >= GRAIN_SIZE)
        {
            grainPhaseB = 0;

            readHeadB =
                ((writeHead - GRAIN_SIZE - HALF_GRAIN)
                & BUFFER_MASK)
                << FP_SHIFT;
        }

        // ========================================================
        // Calibrated pitch CV outputs
        // ========================================================

        int32_t mvA =
            ((pitchA >> 8) * 1000) / 12;
        
        int32_t mvB =
            ((pitchB >> 8) * 1000) / 12;

        CVOut1Millivolts(mvA);
        CVOut2Millivolts(mvB);

        // ========================================================
        // LED UI
        // ========================================================

        UpdateLEDs(quantised);

        // ========================================================
        // Advance circular buffer
        // ========================================================

        writeHead =
            (writeHead + 1)
            & BUFFER_MASK;
    }

    // ============================================================
    // Granular read function
    //
    // Reads interpolated samples from the delay buffer
    // and applies a triangle window.
    //
    // The window smooths grain transitions and reduces
    // hard clicks.
    // ============================================================

    int32_t ReadInterpolated(
        int32_t readHead,
        int32_t phase)
    {
        int32_t index =
            (readHead >> FP_SHIFT)
            & BUFFER_MASK;

        int32_t frac =
            readHead & 0xFFFF;

        int32_t s1 = audioBuffer[index];

        int32_t s2 =
            audioBuffer[
                (index + 1)
                & BUFFER_MASK
            ];

        // ========================================================
        // Linear interpolation
        //
        // This smooths movement between samples.
        // ========================================================

        int32_t sample =
            s1 +
            ((int64_t)(s2 - s1) * frac
            >> FP_SHIFT);

        // ========================================================
        // Triangle envelope
        //
        // Prevents abrupt grain edges.
        // ========================================================

        int32_t env;

        if (phase < HALF_GRAIN)
        {
            env = (phase * 4096) / HALF_GRAIN;
        }
        else
        {
            env =
                ((GRAIN_SIZE - phase)
                * 4096)
                / HALF_GRAIN;
        }

        return ((int64_t)sample * env) >> 12;
    }

    // ============================================================
    // Convert knob position to semitones
    //
    // 0      -> -12
    // 2048   -> 0
    // 4095   -> +12
    // ============================================================

    int32_t KnobToSemitones(int32_t value)
    {
        return (((value << 8) * 24) / 4095) - (12 << 8);
    }
    // ============================================================
    // CV mapping
    //
    // Approximate:
    // 1V = 12 semitones
    // ============================================================

    int32_t CVToSemitones(int32_t cv)
    {
        return ((cv << 8) * 48) / 2048;
    }

    // ============================================================
    // Quantiser
    //

    int32_t QuantisePitch(int32_t semitone)
    {
        // Q8.8 fixed-point chromatic quantiser.
        //
        // Simply rounds to nearest semitone.

        int32_t q =
               (semitone >= 0)
               ? (semitone + 128)
               : (semitone - 128);

           int32_t semi = q / 256;

           return semi * 256;
    
    }

    // ============================================================
    // Fixed-point playback ratios
    //
    // Precomputed for speed.
    //
    // Values are intentionally approximate and
    // musically tuned rather than mathematically exact.
    //
    // Avoids expensive powf() in ISR.
    // ============================================================

    int32_t SemitoneToRatio(int32_t semitone)
    {
        static const int32_t ratios[49] =
        {
            16384, 17358, 18390, 19484,
            20642, 21870, 23170, 24548,
            26008, 27554, 29193, 30929,
            32768,

            34716, 36780, 38967, 41285,
            43740, 46341, 49096, 52016,
            55109, 58386, 61858, 65536,

            69433, 73561, 77935, 82570,
            87480, 92682, 98192, 104032,
            110218, 116772, 123716, 131072,

            138865, 147122, 155870, 165140,
            174960, 185364, 196384, 208064,
            220436, 233544, 247432, 262144
        };

        int32_t semi = semitone / 256;
        int32_t frac = semitone % 256;

        if (frac < 0)
        {
            frac += 256;
            semi -= 1;
        }

        if (semi < -24)
        {
            semi = -24;
            frac = 0;
        }

        if (semi >= 24)
        {
            semi = 23;
            frac = 255;
        }

        int32_t r1 = ratios[semi + 24];
        int32_t r2 = ratios[semi + 25];

        return r1 + (((r2 - r1) * frac) >> 8);
    }

    // ============================================================
    // LED UI
    // ============================================================

    void UpdateLEDs(bool quantised)
    {
        for (int i = 0; i < 6; i++)
        {
            LedOff(i);
        }

        if (quantised)
        {
            for (int i = 0; i < 6; i++)
            {
                LedOn(i);
            }
        }
        else
        {
            for (int i = 0; i < 6; i++)
            {
                LedBrightness(i, 64);
            }
        }
    }

    // ==========================================================

    
};

int main()
{
    DualQuantPitch card;
    card.Run();
}
