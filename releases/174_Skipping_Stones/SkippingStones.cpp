// SkippingStones.cpp
// Controlled-random patch conductor for the Music Thing Workshop Computer.
//
// Inspired by the ideas in Mutable Instruments Marbles: related random gates
// and voltages, deja-vu loop memory, bias/spread shaping, and clocked musical
// uncertainty. This is not a source port; it is a fixed-point Workshop System
// adaptation with two CV outs, two pulse outs, and closed/open hi-hat voices.

#include "ComputerCard.h"
#include "Dr55Samples.h"

#include "hardware/clocks.h"

class SkippingStones : public ComputerCard
{
public:
    static constexpr int32_t kLoopSize = 16;
    static constexpr int32_t kScaleSize = 16;
    static constexpr int32_t kPulseSamples = 480;  // 10 ms at 48 kHz
    static constexpr uint32_t kWavDataOffset = 44;
    static constexpr uint32_t kClosedHatFrames = (kClosedHatWav_len - kWavDataOffset) / 2;
    static constexpr uint32_t kOpenHatTailFrames = kOpenHatPcm_len / 2;
    static constexpr uint32_t kOpenHatFrames = kClosedHatFrames + kOpenHatTailFrames;
    static constexpr uint32_t kAttackFadeFrames = 512;
    static constexpr uint32_t kTailFadeFrames = 128;
    // 44.1 kHz source played by the Workshop Computer's 48 kHz audio loop.
    static constexpr uint32_t kSampleStep = 60211;

    // Minor-pentatonic-ish scale degrees over two octaves. CVOut1 uses
    // calibrated millivolts so the analogue oscillator gets a stable pitch
    // stream; the table keeps note selection cheap inside the audio interrupt.
    static constexpr int32_t kScaleMv[kScaleSize] = {
        -1200, -900, -700, -500, -200, 0, 300, 500,
        700, 1000, 1200, 1500, 1700, 1900, 2200, 2400
    };

    uint16_t xLoop[kLoopSize] = {};
    uint16_t yLoop[kLoopSize] = {};
    uint8_t gateLoop[kLoopSize] = {};

    int32_t loopIndex = 0;
    int32_t stepCounter = 0;
    int32_t interval = 24000;
    int32_t lastExternalCounter = 48000;
    int32_t pulse1Timer = 0;
    int32_t pulse2Timer = 0;
    uint32_t closedHatPosition = kClosedHatFrames << 16;
    uint32_t openHatPosition = kOpenHatFrames << 16;
    int32_t smoothY = 0;
    int32_t currentX = 2048;
    int32_t currentY = 0;
    int32_t currentNoteMv = 0;
    int32_t cv1Led = 2048;
    int32_t randomState = 0x1234567;
    int32_t controlDivider = 0;

    inline int32_t Clamp12(int32_t value)
    {
        if (value > 2047) return 2047;
        if (value < -2048) return -2048;
        return value;
    }

    inline int32_t ClampKnob(int32_t value)
    {
        if (value > 4095) return 4095;
        if (value < 0) return 0;
        return value;
    }

    inline uint16_t ClampLed(int32_t value)
    {
        if (value > 4095) return 4095;
        if (value < 0) return 0;
        return static_cast<uint16_t>(value);
    }

    inline uint32_t Random()
    {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return static_cast<uint32_t>(randomState);
    }

    inline int32_t Random12()
    {
        return static_cast<int32_t>((Random() >> 20) & 4095);
    }

    inline int32_t Fold12(int32_t value)
    {
        value &= 8191;
        if (value > 4095) value = 8191 - value;
        return value;
    }

    inline int32_t ShapedRandom(int32_t spread, int32_t bias)
    {
        // A small integer stand-in for Marbles' table-assisted distribution
        // shaping. Averaging two random values makes a centre-heavy source;
        // spread crossfades back toward a flat distribution, and bias pulls
        // the result toward either end without needing floats or division.
        int32_t flat = Random12();
        int32_t centred = (Random12() + Random12()) >> 1;
        int32_t shaped = centred + (((flat - centred) * spread) >> 12);
        shaped += ((bias - 2048) * (2048 - Abs(shaped - 2048))) >> 11;
        return ClampKnob(shaped);
    }

    inline int32_t Abs(int32_t value)
    {
        return value < 0 ? -value : value;
    }

    inline int32_t QuantizeToScale(int32_t value, int32_t spread)
    {
        int32_t index = (value * kScaleSize) >> 12;
        if (index >= kScaleSize) index = kScaleSize - 1;

        // Wide spread opens the register. Narrow spread stays near the middle
        // of the table, which is friendlier for a first Workshop System patch.
        int32_t octaveShift = ((spread - 2048) * 1200) >> 12;
        return kScaleMv[index] + octaveShift;
    }

    inline void FillLoops()
    {
        for (int32_t i = 0; i < kLoopSize; ++i)
        {
            xLoop[i] = static_cast<uint16_t>(Random12());
            yLoop[i] = static_cast<uint16_t>(Random12());
            gateLoop[i] = static_cast<uint8_t>(Random() & 3);
        }
    }

    inline int32_t LoopLengthFromLock(int32_t lock)
    {
        int32_t length = 2 + ((lock * 14) >> 12);
        if (length > kLoopSize) length = kLoopSize;
        return length;
    }

    inline void AdvanceStep(int32_t density, int32_t lock, int32_t spread, int32_t bias, Switch sw)
    {
        int32_t length = LoopLengthFromLock(lock);
        int32_t index = loopIndex % length;

        // Below noon the loop is constantly rewritten. Above noon it tends to
        // replay, with only occasional new stones dropped into the stream.
        int32_t mutation = lock < 2048 ? 4095 - (lock << 1)
                                       : 256 - ((lock - 2048) >> 4);
        if (mutation < 0) mutation = 0;

        bool rewrite = (Random12() < mutation) || sw == Switch::Down;
        if (rewrite)
        {
            xLoop[index] = static_cast<uint16_t>(ShapedRandom(spread, bias));
            yLoop[index] = static_cast<uint16_t>(ShapedRandom(spread, 4095 - bias));
            gateLoop[index] = static_cast<uint8_t>(Random() & 3);
        }

        currentX = xLoop[index];
        int32_t yTarget = static_cast<int32_t>(yLoop[index]) - 2048;

        int32_t gateBits = gateLoop[index];
        int32_t primaryChance = density;
        int32_t secondaryChance = (density + spread) >> 1;

        bool gate1 = ((gateBits & 1) != 0) || Random12() < primaryChance;
        bool gate2 = ((gateBits & 2) != 0) || Random12() < secondaryChance;

        if (sw == Switch::Up)
        {
            gate2 = gate1 && ((index & 1) == 0 || Random12() < spread);
        }
        else if (sw == Switch::Middle)
        {
            gate1 = gate1 && Random12() < density;
            gate2 = gate2 && Random12() < secondaryChance;
        }

        if (gate1)
        {
            pulse1Timer = kPulseSamples;
            closedHatPosition = 0;
        }
        if (gate2)
        {
            pulse2Timer = kPulseSamples;
            openHatPosition = 0;
        }

        currentNoteMv = QuantizeToScale(currentX, spread);
        currentY = yTarget;
        ++loopIndex;
    }

    inline int32_t ReadPcmSample(const unsigned char *pcm, uint32_t index)
    {
        uint32_t byteIndex = index << 1;
        int16_t sample = static_cast<int16_t>(
            static_cast<uint16_t>(pcm[byteIndex]) |
            (static_cast<uint16_t>(pcm[byteIndex + 1]) << 8));
        return sample >> 4;
    }

    inline int32_t PlayClosedHat()
    {
        uint32_t index = closedHatPosition >> 16;
        if (index >= kClosedHatFrames) return 0;
        int32_t sample = ReadPcmSample(kClosedHatWav + kWavDataOffset, index);
        closedHatPosition += kSampleStep;

        // The original peak sits inside the recorded hit. A longer 10.7 ms
        // ramp rounds it into a hat attack instead of a mixer-click transient.
        if (index < kAttackFadeFrames) sample = (sample * static_cast<int32_t>(index)) >> 9;
        return sample;
    }

    inline int32_t PlayOpenHat()
    {
        uint32_t index = openHatPosition >> 16;
        if (index >= kOpenHatFrames) return 0;
        openHatPosition += kSampleStep;

        if (index < kClosedHatFrames)
        {
            int32_t sample = ReadPcmSample(kClosedHatWav + kWavDataOffset, index);
            if (index < kAttackFadeFrames) sample = (sample * static_cast<int32_t>(index)) >> 9;
            return sample;
        }

        uint32_t tailIndex = index - kClosedHatFrames;
        int32_t sample = ReadPcmSample(kOpenHatPcm, tailIndex) << 1;
        // Ease in the joined tail so its noise begins as continuation, not a
        // second attack after the closed-hat sample ends.
        if (tailIndex < kTailFadeFrames)
        {
            sample = (sample * static_cast<int32_t>(tailIndex)) >> 7;
        }
        return sample;
    }

    virtual void ProcessSample() override
    {
        int32_t density = KnobVal(Knob::Main);
        int32_t lock = KnobVal(Knob::X);
        int32_t spread = KnobVal(Knob::Y);
        Switch sw = SwitchVal();

        if (Connected(Input::CV1))
        {
            // Four Voltages or any CV source can tilt the probability field.
            density = ClampKnob(density + CVIn1());
        }
        if (Connected(Input::CV2))
        {
            spread = ClampKnob(spread + CVIn2());
        }

        int32_t bias = 2048;
        if (Connected(Input::Audio1))
        {
            bias = ClampKnob(2048 + AudioIn1());
        }

        if ((controlDivider++ & 63) == 0)
        {
            interval = 2400 + (((4095 - density) * 96000) >> 12);
        }

        bool externalClock = PulseIn1RisingEdge();
        bool useInternal = lastExternalCounter > 96000;
        if (externalClock)
        {
            lastExternalCounter = 0;
            AdvanceStep(density, lock, spread, bias, sw);
        }
        else
        {
            if (lastExternalCounter < 120000) ++lastExternalCounter;
            if (useInternal)
            {
                --stepCounter;
                if (stepCounter <= 0)
                {
                    int32_t jitter = ((static_cast<int32_t>(Random() & 1023) - 512) * spread) >> 9;
                    stepCounter = interval + jitter;
                    if (stepCounter < 600) stepCounter = 600;
                    AdvanceStep(density, lock, spread, bias, sw);
                }
            }
        }

        if (PulseIn2RisingEdge())
        {
            // A reset input starts the loop from its first stone without
            // erasing the captured pattern.
            loopIndex = 0;
        }

        smoothY += (currentY - smoothY) >> 7;

        CVOut1Millivolts(currentNoteMv);
        CVOut2(Clamp12(smoothY));

        PulseOut1(pulse1Timer > 0);
        PulseOut2(pulse2Timer > 0);
        if (pulse1Timer > 0) --pulse1Timer;
        if (pulse2Timer > 0) --pulse2Timer;

        AudioOut1(Clamp12(PlayClosedHat()));
        AudioOut2(Clamp12(PlayOpenHat()));

        cv1Led += (((currentNoteMv + 2400) - cv1Led) >> 6);
        LedBrightness(0, pulse1Timer > 0 ? 4095 : ClampLed(density));
        LedBrightness(1, pulse2Timer > 0 ? 4095 : ClampLed(lock));
        LedBrightness(2, ClampLed(spread));
        LedBrightness(3, ClampLed(cv1Led));
        LedBrightness(4, ClampLed(smoothY + 2048));
        LedBrightness(5, useInternal ? 1000 : 4095);
    }
};

int main()
{
    set_sys_clock_khz(192000, true);

    SkippingStones card;
    card.EnableNormalisationProbe();
    card.Run();
}
