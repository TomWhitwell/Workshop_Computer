#include "ComputerCard.h"

#include <stdint.h>

class F1shnet : public ComputerCard
{
public:
    void ProcessSample() override
    {
        int32_t input = AudioIn1();
        int32_t auxAudio = AudioIn2();
        int32_t absInput = input >= 0 ? input : -input;
        int32_t absAux = auxAudio >= 0 ? auxAudio : -auxAudio;

        int32_t rawMainKnob = KnobVal(Knob::Main);
        int32_t rawXKnob = KnobVal(Knob::X);
        int32_t rawYKnob = KnobVal(Knob::Y);

        int32_t cv1 = CVIn1();
        int32_t cv2 = CVIn2();

        bool clockIn = PulseIn1();
        bool shGateIn = PulseIn2();
        bool shGateActive = UpdateSampleHoldGate(shGateIn);
        bool clockEdge = clockIn && !lastClockIn_;
        lastClockIn_ = clockIn;

        Switch switchPos = SwitchVal();
        bool altPage = (switchPos == Switch::Up);
        bool downPage = (switchPos == Switch::Down);
        bool shGesture = downPage || shGateActive;

        if (!controlsInitialised_)
        {
            pageMain_[0] = rawMainKnob;
            pageX_[0] = rawXKnob;
            pageY_[0] = rawYKnob;
            pageMain_[1] = 0;
            pageX_[1] = 1536;
            pageY_[1] = 2048;
            downMain_ = 2048;
            controlsInitialised_ = true;
            previousAltPage_ = altPage;
            previousDownPage_ = downPage;
        }

        if (!downPage && previousAltPage_ != altPage)
        {
            ArmPickup(mainPickup_, pageMain_[altPage ? 1 : 0], rawMainKnob);
            ArmPickup(xPickup_, pageX_[altPage ? 1 : 0], rawXKnob);
            ArmPickup(yPickup_, pageY_[altPage ? 1 : 0], rawYKnob);
            previousAltPage_ = altPage;
        }
        if (previousDownPage_ != downPage)
        {
            if (downPage)
            {
                ArmPickup(downMainPickup_, downMain_, rawMainKnob);
            }
            else
            {
                ArmPickup(mainPickup_, pageMain_[altPage ? 1 : 0], rawMainKnob);
                ArmPickup(xPickup_, pageX_[altPage ? 1 : 0], rawXKnob);
                ArmPickup(yPickup_, pageY_[altPage ? 1 : 0], rawYKnob);
            }
            previousDownPage_ = downPage;
        }

        const int pageIndex = altPage ? 1 : 0;
        int32_t mainKnob = pageMain_[pageIndex];
        int32_t xKnob = pageX_[pageIndex];
        int32_t yKnob = pageY_[pageIndex];
        if (downPage)
        {
            ApplyPickup(downMainPickup_, downMain_, rawMainKnob);
        }
        else
        {
            mainKnob = ApplyPickup(mainPickup_, pageMain_[pageIndex], rawMainKnob);
            xKnob = ApplyPickup(xPickup_, pageX_[pageIndex], rawXKnob);
            yKnob = ApplyPickup(yPickup_, pageY_[pageIndex], rawYKnob);
        }

        const int32_t filterMainKnob = (altPage || downPage) ? pageMain_[0] : mainKnob;
        const int32_t filterXKnob = (altPage || downPage) ? pageX_[0] : xKnob;
        const int32_t filterYKnob = (altPage || downPage) ? pageY_[0] : yKnob;
        const int32_t setupMainKnob = altPage ? mainKnob : pageMain_[1];
        const int32_t setupXKnob = altPage ? xKnob : pageX_[1];
        const int32_t setupYKnob = altPage ? yKnob : pageY_[1];

        Parameters params = ComputeParameters(filterMainKnob,
            filterXKnob,
            filterYKnob,
            setupMainKnob,
            setupXKnob,
            setupYKnob,
            downMain_,
            cv1,
            cv2,
            auxAudio,
            absAux);

        UpdateEnvelope(absInput, params.envelopeSensitivity, params.envelopeRelease, clockEdge);
        UpdateSampleHold(params.sampleRate, clockEdge);

        int32_t upCutoff = EnvelopeCutoff(params.rangeBase, params.depth, false);
        int32_t downCutoff = EnvelopeCutoff(params.rangeBase, params.depth, true);
        if (shGesture)
        {
            upCutoff = SampleHoldCutoff(params.rangeBase, params.depth);
            downCutoff = upCutoff;
        }
        upCutoff = Clamp(upCutoff, 64, 3900);
        downCutoff = Clamp(downCutoff, 64, 3900);
        int32_t filterResonance = shGesture ? SampleHoldResonance(params.resonance) : params.resonance;

        int32_t filteredUp = ProcessFilter(input, upCutoff, filterResonance, lowUp_, bandUp_);
        int32_t filteredDown = ProcessFilter(input, downCutoff, filterResonance, lowDown_, bandDown_);
        int32_t wetUp = (filteredUp * params.outputGain) >> 12;
        int32_t wetDown = (filteredDown * params.outputGain) >> 12;

        AudioOut1(SoftClip12(wetUp));
        AudioOut2(SoftClip12(wetDown));

        CVOut1(heldValue_);
        CVOut2(envelope_);

        PulseOut1(clockIn);
        PulseOut2(gateOutCounter_ > 0);
        if (gateOutCounter_ > 0)
        {
            --gateOutCounter_;
        }

        UpdateLeds(altPage, shGesture, downPage ? downMain_ : mainKnob, xKnob, yKnob);
    }

private:
    struct Parameters
    {
        int32_t rangeBase;
        int32_t depth;
        int32_t resonance;
        int32_t envelopeSensitivity;
        int32_t envelopeRelease;
        int32_t sampleRate;
        int32_t outputGain;
    };

    int32_t envelope_ = 0;
    int32_t heldValue_ = 0;
    int32_t sampleCounter_ = 0;
    int32_t samplePeriod_ = 1200;
    int32_t gateOutCounter_ = 0;
    int32_t samplePulseDivider_ = 0;
    int32_t shGateHighCounter_ = 0;
    int32_t rng_ = 0x13579BDF;
    bool lastClockIn_ = false;
    bool controlsInitialised_ = false;
    bool previousAltPage_ = false;
    bool previousDownPage_ = false;
    bool shGateSeenLow_ = false;
    int32_t downMain_ = 2048;

    int32_t lowUp_ = 0;
    int32_t bandUp_ = 0;
    int32_t lowDown_ = 0;
    int32_t bandDown_ = 0;

    static constexpr int32_t kGateLength = 1600;
    static constexpr int32_t kPickupThreshold = 96;
    static constexpr int32_t kGateQualifySamples = 240;

    struct SoftPickup
    {
        bool pickedUp = true;
    };

    SoftPickup mainPickup_ {};
    SoftPickup xPickup_ {};
    SoftPickup yPickup_ {};
    SoftPickup downMainPickup_ {};
    int32_t pageMain_[2] {};
    int32_t pageX_[2] {};
    int32_t pageY_[2] {};

    static void ArmPickup(SoftPickup &pickup, int32_t target, int32_t raw)
    {
        int32_t diff = raw - target;
        if (diff < 0)
        {
            diff = -diff;
        }
        pickup.pickedUp = (diff <= kPickupThreshold);
    }

    static int32_t ApplyPickup(SoftPickup &pickup, int32_t &stored, int32_t raw)
    {
        if (pickup.pickedUp)
        {
            stored = raw;
            return stored;
        }

        int32_t diff = raw - stored;
        if (diff < 0)
        {
            diff = -diff;
        }
        if (diff <= kPickupThreshold)
        {
            pickup.pickedUp = true;
            stored = raw;
        }

        return stored;
    }

    Parameters ComputeParameters(int32_t filterMainKnob,
        int32_t filterXKnob,
        int32_t filterYKnob,
        int32_t setupMainKnob,
        int32_t setupXKnob,
        int32_t setupYKnob,
        int32_t downMainKnob,
        int32_t cv1,
        int32_t cv2,
        int32_t auxAudio,
        int32_t absAux) const
    {
        Parameters params {};

        params.rangeBase = 32 + ((filterMainKnob * 3488) >> 12) + (cv1 >> 2) + (auxAudio >> 3);
        params.depth = 256 + ((filterXKnob * 3200) >> 12) + cv2;
        params.resonance = 1800 - ((filterYKnob * 1500) >> 12) - (absAux >> 4);
        params.envelopeSensitivity = 256 + ((setupXKnob * 2816) >> 12) + (cv1 >> 2);
        params.envelopeRelease = setupYKnob;
        int32_t slowAmount = 4095 - downMainKnob;
        int32_t curvedSlow = (slowAmount * slowAmount) >> 12;
        params.sampleRate = 960 + ((curvedSlow * 23040) >> 12);
        params.outputGain = 1024 + ((setupMainKnob * 3072) >> 12);

        params.rangeBase = Clamp(params.rangeBase, 32, 3900);
        params.depth = Clamp(params.depth, 64, 4095);
        params.resonance = Clamp(params.resonance, 64, 1800);
        params.envelopeSensitivity = Clamp(params.envelopeSensitivity, 64, 3072);
        params.envelopeRelease = Clamp(params.envelopeRelease, 64, 4095);
        params.sampleRate = Clamp(params.sampleRate, 48, 48000);
        params.outputGain = Clamp(params.outputGain, 512, 4095);

        return params;
    }

    static int32_t Clamp(int32_t value, int32_t minValue, int32_t maxValue)
    {
        if (value < minValue)
        {
            return minValue;
        }
        if (value > maxValue)
        {
            return maxValue;
        }
        return value;
    }

    static int16_t SoftClip12(int32_t sample)
    {
        if (sample > 3072)
        {
            sample = 3072 + ((sample - 3072) >> 2);
        }
        if (sample < -3072)
        {
            sample = -3072 + ((sample + 3072) >> 2);
        }

        if (sample > 2047)
        {
            sample = 2047;
        }
        if (sample < -2048)
        {
            sample = -2048;
        }

        return static_cast<int16_t>(sample);
    }

    int32_t SampleHoldCutoff(int32_t baseCutoff, int32_t depth) const
    {
        baseCutoff = Clamp(baseCutoff, 256, 3900);
        int32_t holdUnipolar = heldValue_ + 2048;
        int32_t ratio = 2048 + holdUnipolar;
        int32_t octaveCutoff = (baseCutoff * ratio) >> 12;
        int32_t pitchDepth = 512 + ((depth * 3584) >> 12);
        return baseCutoff + (((octaveCutoff - baseCutoff) * pitchDepth) >> 12);
    }

    int32_t EnvelopeCutoff(int32_t baseCutoff, int32_t depth, bool inverted) const
    {
        int32_t shapedEnvelope = envelope_;
        shapedEnvelope = Clamp(shapedEnvelope, 0, 4095);
        if (inverted)
        {
            shapedEnvelope = 4095 - shapedEnvelope;
        }

        int32_t maxRatio = 4096 + ((depth * 12288) >> 12);
        int32_t ratio = 4096 + ((shapedEnvelope * (maxRatio - 4096)) >> 12);
        return (baseCutoff * ratio) >> 12;
    }

    int32_t SampleHoldResonance(int32_t resonance) const
    {
        return Clamp(resonance + 192, 384, 1800);
    }

    bool UpdateSampleHoldGate(bool gateIn)
    {
        if (!gateIn)
        {
            shGateSeenLow_ = true;
            shGateHighCounter_ = 0;
            return false;
        }

        if (!shGateSeenLow_)
        {
            return false;
        }

        if (shGateHighCounter_ < kGateQualifySamples)
        {
            ++shGateHighCounter_;
        }
        return shGateHighCounter_ >= kGateQualifySamples;
    }

    void UpdateEnvelope(int32_t absInput, int32_t sensitivityControl, int32_t releaseControl, bool clockEdge)
    {
        if (absInput < 4)
        {
            absInput = 0;
        }
        int32_t driven = (absInput * sensitivityControl) >> 10;
        driven = Clamp(driven, 0, 4095);

        if (driven > envelope_)
        {
            envelope_ += (driven - envelope_) >> 3;
        }
        else
        {
            int32_t difference = envelope_ - driven;
            int32_t releaseShift = 3 + (((4095 - releaseControl) * 5) >> 12);
            int32_t fall = difference >> releaseShift;
            if (fall < 1 && difference > 0)
            {
                fall = 1;
            }
            envelope_ -= fall;
        }

        if (clockEdge && envelope_ < 512)
        {
            envelope_ = 512;
        }
    }

    void UpdateSampleHold(int32_t sampleRateControl, bool clockEdge)
    {
        samplePeriod_ = sampleRateControl;
        if (samplePeriod_ < 48)
        {
            samplePeriod_ = 48;
        }

        ++sampleCounter_;
        if (clockEdge || sampleCounter_ >= samplePeriod_)
        {
            sampleCounter_ = 0;
            heldValue_ = NextRandomSigned();
            ++samplePulseDivider_;
            if (samplePulseDivider_ >= 4)
            {
                samplePulseDivider_ = 0;
                gateOutCounter_ = kGateLength;
            }
        }
    }

    int32_t NextRandomSigned()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<int32_t>((rng_ >> 20) & 4095) - 2048;
    }

    int32_t ProcessFilter(int32_t input, int32_t cutoff, int32_t resonance, int32_t &low, int32_t &band)
    {
        int32_t notch = input - low - ((band * resonance) >> 12);
        band += (cutoff * notch) >> 12;
        band = Clamp(band, -32768, 32767);

        low += (cutoff * band) >> 12;
        low = Clamp(low, -32768, 32767);

        return low;
    }

    void UpdateLeds(bool altPage, bool shGesture, int32_t mainKnob, int32_t xKnob, int32_t yKnob)
    {
        LedOn(0, altPage);
        LedOn(2, shGesture);
        LedOn(4, !altPage);

        LedBrightness(1, mainKnob);
        LedBrightness(3, xKnob);
        LedBrightness(5, yKnob);
    }
};

int main()
{
    set_sys_clock_khz(192000, true);

    F1shnet card;
    card.Run();
}
