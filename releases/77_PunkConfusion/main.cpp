// Punk Confusion uses the local ComputerCard.h copy in this release folder.
//  this local
// copy carries the newer per-card fixes needed for hardware testing.
//
// ComputerCard credit:
//   Chris Johnson, version 0.3.0 (12 May 2026), MIT licensed.
#include "ComputerCard.h"
#include "SampleBank.h"
#include "VocalSamples.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr int32_t kSampleRate = 48000;
constexpr int32_t kMaxAudio = 2047;
constexpr int32_t kMinAudio = -2048;
constexpr uint32_t kVenueDelaySize = 16384;
constexpr uint32_t kApcPeriodMinSamples = 24;   // about 2 kHz
constexpr uint32_t kApcPeriodMaxSamples = 960;  // about 50 Hz
constexpr uint32_t kApcPulseMinSamples = 4;     // shortest one-shot pulse
constexpr uint32_t kApcPulseMaxSamples = 1100;  // lets the monostable overrun
constexpr int32_t kPickupWindow = 96;

enum VenueType
{
    VenueCBGB = 0,
    VenueClub100,
    VenueMarquee,
    VenueWhisky
};

struct VenueProfile
{
    uint16_t tap1;
    uint16_t tap2;
    uint16_t tap3;
    uint16_t mainDelayBase;
    uint16_t mainDelayRange;
    uint16_t feedbackBase;
    uint16_t feedbackRange;
    uint16_t lowpassBase;
    uint16_t lowpassRange;
    int16_t color;
    uint16_t flutter;
    uint16_t dropout;
    uint16_t noise;
};

constexpr VenueProfile kVenueProfiles[] = {
    // CBGB: cramped, abrasive, overloaded
    {120, 260, 510, 1800, 900, 1200, 420, 360, 130, 420, 120, 50, 120},
    // 100 Club: warm, dense, sweaty
    {360, 780, 1320, 4200, 1300, 1500, 520, 520, 180, -260, 60, 40, 80},
    // Marquee: tight, sharp, punchy
    {70, 160, 330, 920, 520, 900, 300, 120, 60, 260, 40, 25, 35},
    // Whisky a Go Go: larger, splashier, more stage PA
    {640, 1450, 2860, 6800, 1800, 1150, 460, 170, 70, 80, 35, 30, 55},
};

static inline int32_t Clamp12(int32_t value)
{
    if (value > kMaxAudio) return kMaxAudio;
    if (value < kMinAudio) return kMinAudio;
    return value;
}

static inline int32_t Abs32(int32_t value)
{
    return value < 0 ? -value : value;
}

static inline int32_t Lerp(int32_t a, int32_t b, int32_t mix4096)
{
    return (a * (4096 - mix4096) + b * mix4096) >> 12;
}

static inline int32_t SoftClip(int32_t x)
{
    x = Clamp12(x);
    const int32_t ax = Abs32(x);
    const int32_t bend = (ax * ax) >> 12;
    return x >= 0 ? x - bend / 3 : x + bend / 3;
}

static inline int32_t Clamp4095(int32_t value)
{
    if (value < 0) return 0;
    if (value > 4095) return 4095;
    return value;
}

struct SampleVoice
{
    const int16_t *data = nullptr;
    const uint8_t *muLawData = nullptr;
    uint32_t length = 0;
    uint32_t phase = 0; // 24.8 fixed-point sample position.
    uint32_t step = 256;
    uint32_t ageSamples = 0;
    uint32_t fadeSamples = 0;
    int32_t level = 0;
    bool active = false;
    bool reverse = false;
    bool muLaw = false;
};

struct DelayLine
{
    int16_t buffer[kVenueDelaySize] = {};
    uint32_t writeIndex = 0;

    int16_t Read(uint32_t delaySamples) const
    {
        uint32_t readIndex = (writeIndex + kVenueDelaySize - (delaySamples % kVenueDelaySize)) % kVenueDelaySize;
        return buffer[readIndex];
    }

    void Write(int16_t sample)
    {
        buffer[writeIndex] = sample;
        writeIndex++;
        if (writeIndex >= kVenueDelaySize) writeIndex = 0;
    }
};

class PunkConfusion : public ComputerCard
{
public:
    PunkConfusion()
    {
        sampleBank_[VenueMarquee] = {kVocalMarqueeOi, nullptr, static_cast<uint32_t>(sizeof(kVocalMarqueeOi) / sizeof(kVocalMarqueeOi[0])), false};
        sampleBank_[VenueCBGB] = {kVocalCbgbHeyHo, nullptr, static_cast<uint32_t>(sizeof(kVocalCbgbHeyHo) / sizeof(kVocalCbgbHeyHo[0])), false};
        sampleBank_[VenueClub100] = {kVocalClub100NoFuture, nullptr, static_cast<uint32_t>(sizeof(kVocalClub100NoFuture) / sizeof(kVocalClub100NoFuture[0])), false};
        sampleBank_[VenueWhisky] = {kVocalWhiskyLetsGo, nullptr, static_cast<uint32_t>(sizeof(kVocalWhiskyLetsGo) / sizeof(kVocalWhiskyLetsGo[0])), false};
        LoadUploadedSampleBank();
    }

    void ShowLoaderEntryLeds()
    {
        for (uint32_t blink = 0; blink < 3; ++blink)
        {
            for (uint32_t i = 0; i < 6; ++i) LedOn(i, true);
            sleep_ms(110);
            for (uint32_t i = 0; i < 6; ++i) LedOff(i);
            sleep_ms(110);
        }

        LedOn(0, true);
        LedOn(2, true);
        LedOn(4, true);
    }

    void ProcessSample() override
    {
        const Switch sw = SwitchVal();
        const bool switchDownEdge = SwitchChanged() && sw == Switch::Down;
        const bool vocalGate = sw == Switch::Down || PulseIn2();
        const bool vocalTrigger = switchDownEdge || PulseIn2RisingEdge();
        const int32_t mainKnob = KnobVal(Knob::Main);

        if (SwitchChanged())
        {
            if (sw == Switch::Down)
            {
                vocalGainPickedUp_ = Abs32(mainKnob - vocalGainControl_) <= kPickupWindow;
            }
            else if (sw == Switch::Middle)
            {
                roomGainPickedUp_ = Abs32(mainKnob - roomGainControl_) <= kPickupWindow;
            }
        }

        if (sw == Switch::Down)
        {
            UpdateSoftPickup(mainKnob, lastMainKnob_, vocalGainControl_, vocalGainPickedUp_);
        }
        else if (sw == Switch::Middle)
        {
            UpdateSoftPickup(mainKnob, lastMainKnob_, roomGainControl_, roomGainPickedUp_);
        }
        lastMainKnob_ = mainKnob;

        if (vocalTrigger)
        {
            TriggerVenueSample();
            vocalLedCounter_ = 4000;
        }
        else if (!vocalGate)
        {
            voice_.active = false;
        }

        int32_t output = 0;
        int32_t outputRight = 0;

        if (sw == Switch::Up)
        {
            output = RenderApc();
            outputRight = output;
        }
        else
        {
            output = RenderBrokenVenue();
            outputRight = roomRightOut_;
        }

        AudioOut1(output);
        AudioOut2(outputRight);
        UpdateLeds(sw);
    }

private:
    struct SampleDef
    {
        const int16_t *data;
        const uint8_t *muLawData = nullptr;
        uint32_t length;
        bool muLaw = false;
    };

    DelayLine venueDelay_{};
    SampleDef sampleBank_[4]{};
    PunkSampleBank::LoadedBank uploadedBank_{};
    SampleVoice voice_{};

    uint32_t rngState_ = 0x13579BDFu;
    uint32_t apcTriggerCounter_ = 0;
    uint32_t apcPulseTimer_ = 0;
    int32_t venueFilterState_ = 0;
    int32_t venueFilterRightState_ = 0;
    int32_t venueEnvelope_ = 0;
    int32_t venueNoiseHold_ = 0;
    int32_t audienceLowState_ = 0;
    int32_t audienceHighState_ = 0;
    int32_t audienceLowRightState_ = 0;
    int32_t audienceHighRightState_ = 0;
    int32_t audienceDryLowState_ = 0;
    int32_t audienceDryHighState_ = 0;
    int32_t roomRightOut_ = 0;
    int32_t vocalLedCounter_ = 0;
    int32_t roomGainControl_ = 2048;
    int32_t vocalGainControl_ = 2048;
    int32_t lastMainKnob_ = 2048;
    bool roomGainPickedUp_ = false;
    bool vocalGainPickedUp_ = false;
    int32_t apcTriggerLedCounter_ = 0;
    uint32_t apcTriggerLedDivider_ = 0;
    uint32_t venueFlutterCounter_ = 0;

    uint32_t NextRandom()
    {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return rngState_;
    }

    void LoadUploadedSampleBank()
    {
        uploadedBank_ = PunkSampleBank::load();
        if (!uploadedBank_.valid)
        {
            return;
        }

        sampleBank_[VenueMarquee] = UploadedSample(0);
        sampleBank_[VenueCBGB] = UploadedSample(1);
        sampleBank_[VenueClub100] = UploadedSample(2);
        sampleBank_[VenueWhisky] = UploadedSample(3);
    }

    SampleDef UploadedSample(uint32_t index) const
    {
        const PunkSampleBank::SampleInfo &info = uploadedBank_.header->samples[index];
        return {nullptr, uploadedBank_.payload + info.offset, info.length, true};
    }

    void UpdateSoftPickup(
        int32_t knob,
        int32_t previousKnob,
        int32_t &storedControl,
        bool &pickedUp)
    {
        if (!pickedUp)
        {
            const bool closeEnough = Abs32(knob - storedControl) <= kPickupWindow;
            const bool crossedUp = previousKnob < storedControl && knob >= storedControl;
            const bool crossedDown = previousKnob > storedControl && knob <= storedControl;
            pickedUp = closeEnough || crossedUp || crossedDown;
        }

        if (pickedUp)
        {
            storedControl = knob;
        }
    }

    uint32_t ControlToApcPeriod(int32_t control) const
    {
        control = Clamp4095(control);

        // Invert the knob so clockwise means faster, then square it for a denser
        // useful range in the middle of the pot travel.
        const uint32_t inverse = static_cast<uint32_t>(4095 - control);
        const uint32_t shaped = (inverse * inverse) >> 12;
        return kApcPeriodMinSamples
            + ((shaped * (kApcPeriodMaxSamples - kApcPeriodMinSamples)) >> 12);
    }

    uint32_t ControlToApcPulseLength(int32_t control) const
    {
        control = Clamp4095(control);

        // The second 555 in a classic APC is a monostable, not a duty-cycle
        // control. Higher hardware resistance gives a longer one-shot, but this
        // pot reads opposite on the card, so invert it to match the useful feel.
        const uint32_t inverse = static_cast<uint32_t>(4095 - control);
        const uint32_t step = inverse >> 6; // 64 steps
        const uint32_t shaped = (step * step) >> 6;
        return kApcPulseMinSamples
            + ((shaped * (kApcPulseMaxSamples - kApcPulseMinSamples)) >> 6);
    }

    int32_t InputGainQ12(int32_t control) const
    {
        control = Clamp4095(control);

        // Broken Venue needs to accept both hot modular signals and quieter
        // line-level sources. Main is unity around noon, attenuates below noon,
        // and gives enough lift above noon to make line-level gear usable.
        if (control <= 2048)
        {
            return 1024 + ((control * 3072) >> 11); // 0.25x..1.0x
        }
        const int32_t boost = control - 2048;
        const int32_t shapedBoost = (boost * boost) >> 11;
        return 4096 + ((shapedBoost * 12288) >> 11); // 1.0x..4.0x
    }

    VenueType SelectVenue(int32_t roomKnob) const
    {
        if (roomKnob < 1024) return VenueMarquee;
        if (roomKnob < 2048) return VenueCBGB;
        if (roomKnob < 3072) return VenueClub100;
        return VenueWhisky;
    }

    int32_t ApplyAudienceAttenuation(
        int32_t signal,
        int32_t audience,
        int32_t &lowState,
        int32_t &highState)
    {
        audience = Clamp4095(audience);

        // Inspired by Rummler/Green/Jurkiewicz/Kahle 2025 ARTF measurements:
        // grazing sound over audience seating loses broadband energy from about
        // 200 Hz upward, with the strongest loss around 400 Hz-3 kHz.
        lowState += (signal - lowState) / 20;  // about 400 Hz
        highState += (signal - highState) / 3; // about 3 kHz

        const int32_t low = lowState;
        const int32_t mid = highState - lowState;
        const int32_t high = signal - highState;

        const int32_t lowGain = 4096 - ((audience * 650) >> 12);   // about -1.5 dB
        const int32_t midGain = 4096 - ((audience * 2200) >> 12);  // about -6.5 dB
        const int32_t highGain = 4096 - ((audience * 1700) >> 12); // about -4.5 dB

        return Clamp12(((low * lowGain) + (mid * midGain) + (high * highGain)) >> 12);
    }

    int32_t RenderApc()
    {
        // 555-style APC model: an astable clock (X/CV1) triggers a monostable
        // one-shot (Y/CV2). If a trigger arrives while the one-shot is still
        // high, it is ignored, which creates the hardware-like skip/step zones.
        int32_t cv1 = KnobVal(Knob::X) + CVIn1();
        int32_t cv2 = KnobVal(Knob::Y) + CVIn2();
        const uint32_t periodSamples = ControlToApcPeriod(cv1);

        if (apcTriggerCounter_ == 0)
        {
            if (apcPulseTimer_ == 0)
            {
                apcPulseTimer_ = ControlToApcPulseLength(cv2);
            }
            apcTriggerCounter_ = periodSamples;
            apcTriggerLedDivider_++;
            if ((apcTriggerLedDivider_ & 0x3Fu) == 0)
            {
                apcTriggerLedCounter_ = 2400;
            }
        }
        else
        {
            apcTriggerCounter_--;
        }

        int32_t apcSample = 0;
        if (apcPulseTimer_ > 0)
        {
            apcPulseTimer_--;
            apcSample = 1900;
        }
        else
        {
            apcSample = -1900;
        }

        if (Connected(Input::Pulse1) && !PulseIn1())
        {
            apcSample = 0;
        }

        const int32_t volume = KnobVal(Knob::Main);
        apcSample = (apcSample * volume) >> 12;

        return SoftClip(apcSample);
    }

    int32_t ReadSampleVoice()
    {
        if (!voice_.active || (!voice_.data && !voice_.muLawData) || voice_.length == 0)
        {
            return 0;
        }

        const uint32_t index = voice_.phase >> 8;
        if (index >= voice_.length)
        {
            voice_.active = false;
            return 0;
        }

        const uint32_t nextIndex = voice_.reverse
            ? (index > 0 ? index - 1 : index)
            : ((index + 1 < voice_.length) ? index + 1 : index);
        const int32_t a = voice_.muLaw
            ? static_cast<int32_t>(PunkSampleBank::decodeMuLaw(voice_.muLawData[index]))
            : voice_.data[index];
        const int32_t b = voice_.muLaw
            ? static_cast<int32_t>(PunkSampleBank::decodeMuLaw(voice_.muLawData[nextIndex]))
            : voice_.data[nextIndex];
        const int32_t frac = static_cast<int32_t>(voice_.phase & 0xFFu);
        const int32_t sample = ((a * (256 - frac)) + (b * frac)) >> 8;
        const uint32_t remaining = voice_.reverse ? index + 1 : voice_.length - index;
        uint32_t envelope = 4096;
        if (voice_.ageSamples < voice_.fadeSamples)
        {
            envelope = (voice_.ageSamples * 4096) / voice_.fadeSamples;
        }
        if (remaining < voice_.fadeSamples && remaining < envelope)
        {
            envelope = (remaining * 4096) / voice_.fadeSamples;
        }

        voice_.ageSamples++;
        if (voice_.reverse)
        {
            if (voice_.phase <= voice_.step)
            {
                voice_.active = false;
            }
            else
            {
                voice_.phase -= voice_.step;
            }
        }
        else
        {
            voice_.phase += voice_.step;
            if ((voice_.phase >> 8) >= voice_.length)
            {
                voice_.active = false;
            }
        }

        return (((sample * voice_.level) >> 12) * static_cast<int32_t>(envelope)) >> 12;
    }

    void TriggerVenueSample()
    {
        const SampleDef &choice = sampleBank_[SelectVenue(KnobVal(Knob::X))];
        const bool sliceCvPatched = Connected(Input::Audio2);
        const int32_t sliceCv = sliceCvPatched ? AudioIn2() : 0;
        const bool reverse = sliceCvPatched && sliceCv < -96;
        const uint32_t cvMagnitude = sliceCvPatched ? static_cast<uint32_t>(Abs32(sliceCv)) : 0;
        uint32_t slice = (cvMagnitude * 8u) >> 11;
        if (slice > 7) slice = 7;

        uint32_t startIndex = 0;
        if (sliceCvPatched && choice.length > 0)
        {
            startIndex = reverse
                ? ((choice.length * (slice + 1u)) / 8u)
                : ((choice.length * slice) / 8u);
            if (startIndex >= choice.length) startIndex = choice.length - 1;
        }

        voice_.data = choice.data;
        voice_.muLawData = choice.muLawData;
        voice_.length = choice.length;
        voice_.phase = startIndex << 8;
        voice_.step = 128; // 24 kHz sample data played at the 48 kHz audio rate.
        voice_.ageSamples = 0;
        voice_.fadeSamples = 96; // about 4 ms at the source sample rate.
        voice_.level = 1536;
        voice_.reverse = reverse;
        voice_.muLaw = choice.muLaw;
        voice_.active = true;
    }

    int32_t RenderBrokenVenue()
    {
        const int32_t inputGain = InputGainQ12(roomGainControl_);
        int32_t in = (static_cast<int32_t>(AudioIn1()) * inputGain) >> 12;
        const int32_t vocalGain = (inputGain * vocalGainControl_) >> 12;
        int32_t sample = (ReadSampleVoice() * vocalGain) >> 12;

        int32_t source = SoftClip(in + sample);
        const int32_t vocalRoomSend = sample != 0 ? sample >> 1 : 0;

        const int32_t roomKnob = KnobVal(Knob::X);
        const int32_t audienceRaw = KnobVal(Knob::Y);
        const int32_t audience = ((audienceRaw * audienceRaw) >> 13);
        const VenueType selectedVenue = SelectVenue(roomKnob);
        const VenueProfile &venue = kVenueProfiles[selectedVenue];

        const uint32_t delaySamples = venue.mainDelayBase
            + ((static_cast<uint32_t>(audience) * venue.mainDelayRange) >> 14);

        const int32_t tap1 = venueDelay_.Read(venue.tap1);
        const int32_t tap2 = venueDelay_.Read(venue.tap2);
        const int32_t tap3 = venueDelay_.Read(venue.tap3);
        const int32_t delayed = venueDelay_.Read(delaySamples);
        const int32_t tapR1 = venueDelay_.Read(venue.tap1 + 37);
        const int32_t tapR2 = venueDelay_.Read(venue.tap2 + 109);
        const int32_t tapR3 = venueDelay_.Read(venue.tap3 + 211);
        const int32_t delayedRight = venueDelay_.Read(delaySamples + 337);

        int32_t early = 0;
        int32_t earlyRight = 0;
        int32_t roomInput = 0;
        int32_t filterDiv = 8;
        int32_t feedback = 0;
        int32_t combBite = 0;
        int32_t combBiteRight = 0;

        switch (selectedVenue)
        {
        case VenueCBGB:
            early = ((tap1 << 1) - tap2 + tap3) >> 2;
            earlyRight = ((tapR2 << 1) - tapR1 + tapR3) >> 2;
            roomInput = SoftClip(source + (source >> 1));
            filterDiv = 5;
            feedback = 1050;
            break;
        case VenueClub100:
            early = (tap1 + (tap2 << 1) + tap3) >> 2;
            earlyRight = ((tapR1 << 1) + tapR2 + tapR3) >> 2;
            roomInput = SoftClip(source - (source >> 3));
            filterDiv = 14;
            feedback = 1200;
            break;
        case VenueMarquee:
            early = ((tap1 + tap2) >> 2) + (tap3 >> 2);
            earlyRight = ((tapR2 + tapR3) >> 2) + (tapR1 >> 3);
            roomInput = source;
            filterDiv = 8;
            feedback = 620;
            break;
        case VenueWhisky:
            early = (tap1 - tap2 + (tap3 << 1)) >> 2;
            earlyRight = (tapR1 - tapR2 + (tapR3 << 1)) >> 2;
            combBite = (tap1 - tap2) >> 2;
            combBiteRight = (tapR1 - tapR2) >> 2;
            roomInput = SoftClip(source + (source >> 2));
            filterDiv = 4;
            feedback = 1020;
            break;
        }

        filterDiv += 1 + (audience >> 9); // clockwise Y absorbs high end.
        feedback = (feedback * (4096 - (audience >> 2))) >> 12;
        if (feedback > 1200) feedback = 1200;

        venueFilterState_ += (delayed - venueFilterState_) / filterDiv;
        int32_t filtered = venueFilterState_;
        venueFilterRightState_ += (delayedRight - venueFilterRightState_) / filterDiv;
        int32_t filteredRight = venueFilterRightState_;
        early = (early * (4096 - (audience >> 3))) >> 12;
        earlyRight = (earlyRight * (4096 - (audience >> 3))) >> 12;

        int32_t wet = 0;
        int32_t wetRight = 0;
        switch (selectedVenue)
        {
        case VenueCBGB:
            wet = SoftClip((roomInput >> 1) + early + (filtered >> 1));
            wetRight = SoftClip((roomInput >> 1) + earlyRight + (filteredRight >> 1));
            break;
        case VenueClub100:
            wet = SoftClip((roomInput >> 1) + (early >> 1) + filtered);
            wetRight = SoftClip((roomInput >> 1) + (earlyRight >> 1) + filteredRight);
            break;
        case VenueMarquee:
            wet = SoftClip((roomInput >> 1) + (early >> 1) - (filtered >> 3));
            wetRight = SoftClip((roomInput >> 1) + (earlyRight >> 1) + (filteredRight >> 3));
            break;
        case VenueWhisky:
            wet = SoftClip((roomInput >> 1) + early + (filtered >> 1) + combBite);
            wetRight = SoftClip((roomInput >> 1) + earlyRight + (filteredRight >> 1) + combBiteRight);
            break;
        }

        venueEnvelope_ += (Abs32(source) - venueEnvelope_) >> 6;
        venueNoiseHold_ = (venueNoiseHold_ * 31) >> 5;

        wet = SoftClip(wet);
        wet = ApplyAudienceAttenuation(wet, audience, audienceLowState_, audienceHighState_);
        wetRight = SoftClip(wetRight);
        wetRight = ApplyAudienceAttenuation(
            wetRight,
            audience,
            audienceLowRightState_,
            audienceHighRightState_);
        const int32_t dryAudience = audience >> 1;
        const int32_t drySource = ApplyAudienceAttenuation(
            source,
            dryAudience,
            audienceDryLowState_,
            audienceDryHighState_);

        const int32_t writeSample = Clamp12(roomInput + vocalRoomSend + (early >> 1) + ((wet * feedback) >> 12));
        venueDelay_.Write(static_cast<int16_t>(writeSample));

        roomRightOut_ = Clamp12((drySource + wetRight) >> 1);
        return Clamp12((drySource + wet) >> 1);
    }

    void UpdateLeds(Switch sw)
    {
        const bool apcMode = sw == Switch::Up;
        const bool vocalEditMode = sw == Switch::Down;
        const bool roomMode = sw == Switch::Middle || sw == Switch::Down;
        const bool gateOpen = !Connected(Input::Pulse1) || PulseIn1();
        const VenueType ledVenue = SelectVenue(KnobVal(Knob::X));
        const bool shoutLed0 = vocalEditMode && vocalGainControl_ > 512;
        const bool shoutLed2 = vocalEditMode && vocalGainControl_ > 1700;
        const bool shoutLed4 = vocalEditMode && vocalGainControl_ > 2900;

        LedOn(0, apcMode || shoutLed0 || (!vocalEditMode && roomMode && (ledVenue == VenueMarquee || ledVenue == VenueWhisky)));
        LedOn(1, sw == Switch::Middle || sw == Switch::Down);
        LedOn(2, apcMode ? apcTriggerLedCounter_ > 0 : shoutLed2 || (!vocalEditMode && roomMode && (ledVenue == VenueCBGB || ledVenue == VenueWhisky)));
        LedOn(3, !apcMode && vocalLedCounter_ > 0);

        int32_t venueActivity = venueEnvelope_ << 1;
        if (venueActivity > 4095) venueActivity = 4095;
        LedBrightness(4, apcMode && gateOpen ? 4095 : (apcMode ? 0 : (shoutLed4 || (!vocalEditMode && roomMode && (ledVenue == VenueClub100 || ledVenue == VenueWhisky)) ? 4095 : 0)));

        const int32_t clipIndicator = !apcMode && Abs32(venueFilterState_) > 1400 ? 4095 : 0;
        LedBrightness(5, clipIndicator);

        if (vocalLedCounter_ > 0) vocalLedCounter_--;
        if (apcTriggerLedCounter_ > 0) apcTriggerLedCounter_--;
    }
};

int ReadByteBlocking()
{
    int c = getchar_timeout_us(0);
    while (c < 0)
    {
        tight_loop_contents();
        c = getchar_timeout_us(1000);
    }
    return c;
}

uint32_t ReadU32Blocking()
{
    uint8_t bytes[4] = {};
    for (uint32_t i = 0; i < 4; ++i)
    {
        bytes[i] = static_cast<uint8_t>(ReadByteBlocking());
    }
    return static_cast<uint32_t>(bytes[0])
        | (static_cast<uint32_t>(bytes[1]) << 8)
        | (static_cast<uint32_t>(bytes[2]) << 16)
        | (static_cast<uint32_t>(bytes[3]) << 24);
}

constexpr uint32_t kLoaderCommandUpload = 0x444C4350u; // "PCLD" little-endian.
constexpr uint32_t kLoaderCommandClear = 0x524C4350u;  // "PCLR" little-endian.

uint32_t WaitForLoaderCommand()
{
    uint8_t window[4] = {};
    uint32_t count = 0;

    while (true)
    {
        const int c = getchar_timeout_us(1000);
        if (c < 0)
        {
            tight_loop_contents();
            continue;
        }

        window[count & 0x03u] = static_cast<uint8_t>(c);
        count++;
        if (count < 4)
        {
            continue;
        }

        const uint32_t start = count & 0x03u;
        const uint32_t command = static_cast<uint32_t>(window[start])
            | (static_cast<uint32_t>(window[(start + 1) & 0x03u]) << 8)
            | (static_cast<uint32_t>(window[(start + 2) & 0x03u]) << 16)
            | (static_cast<uint32_t>(window[(start + 3) & 0x03u]) << 24);

        if (command == kLoaderCommandUpload || command == kLoaderCommandClear)
        {
            return command;
        }
    }
}

bool ValidateBankHeader(const PunkSampleBank::Header &header, uint32_t length)
{
    if (length != sizeof(PunkSampleBank::Header) + header.payloadBytes)
    {
        return false;
    }
    if (header.magic != PunkSampleBank::kMagic
        || header.version != PunkSampleBank::kVersion
        || header.format != PunkSampleBank::kFormatMuLaw8
        || header.sampleRate != PunkSampleBank::kSampleRate
        || header.sampleCount != PunkSampleBank::kSampleCount
        || header.payloadBytes > PunkSampleBank::kMaxPayloadBytes)
    {
        return false;
    }

    for (uint32_t i = 0; i < PunkSampleBank::kSampleCount; ++i)
    {
        const uint32_t end = header.samples[i].offset + header.samples[i].length;
        if (end < header.samples[i].offset || end > header.payloadBytes)
        {
            return false;
        }
    }

    return true;
}

bool ReceiveAndProgramBank(uint32_t length)
{
    if (length < sizeof(PunkSampleBank::Header) || length > PunkSampleBank::kBankSize)
    {
        printf("ERR BAD_LENGTH\n");
        return false;
    }

    PunkSampleBank::Header header{};
    auto *headerBytes = reinterpret_cast<uint8_t *>(&header);
    for (uint32_t i = 0; i < sizeof(header); ++i)
    {
        headerBytes[i] = static_cast<uint8_t>(ReadByteBlocking());
    }

    if (!ValidateBankHeader(header, length))
    {
        const uint32_t discard = length - sizeof(header);
        for (uint32_t i = 0; i < discard; ++i)
        {
            ReadByteBlocking();
        }
        printf("ERR INVALID_HEADER\n");
        return false;
    }

    printf("OK WRITING\n");

    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(FLASH_PAGE_SIZE)));
    memset(page, 0xFF, sizeof(page));
    uint32_t pageFill = 0;
    uint32_t flashOffset = 0;
    uint32_t checksum = 2166136261u;

    auto programPage = [&]() {
        const uint32_t ints = save_and_disable_interrupts();
        flash_range_program(PunkSampleBank::kBankOffset + flashOffset, page, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
        flashOffset += FLASH_PAGE_SIZE;
        pageFill = 0;
        memset(page, 0xFF, sizeof(page));
    };

    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PunkSampleBank::kBankOffset, PunkSampleBank::kBankSize);
    restore_interrupts(ints);

    for (uint32_t i = 0; i < sizeof(header); ++i)
    {
        page[pageFill++] = headerBytes[i];
        if (pageFill == FLASH_PAGE_SIZE) programPage();
    }

    for (uint32_t i = 0; i < header.payloadBytes; ++i)
    {
        const uint8_t byte = static_cast<uint8_t>(ReadByteBlocking());
        checksum ^= byte;
        checksum *= 16777619u;
        page[pageFill++] = byte;
        if (pageFill == FLASH_PAGE_SIZE) programPage();
    }

    if (pageFill > 0)
    {
        programPage();
    }

    if (checksum != header.checksum)
    {
        printf("ERR CHECKSUM\n");
        return false;
    }

    printf("OK DONE\n");
    return true;
}

void ClearUploadedBank()
{
    printf("OK RESTORE_FACTORY\n");
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PunkSampleBank::kBankOffset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    printf("OK FACTORY_DONE\n");
}

void RunWebSerialLoader()
{
    stdio_usb_init();
    sleep_ms(1200);
    printf("PUNKCONF LOADER READY\n");
    printf("FLASH_BYTES %lu\n", static_cast<unsigned long>(PICO_FLASH_SIZE_BYTES));
    printf("SAMPLE_BANK_BYTES %lu\n", static_cast<unsigned long>(PunkSampleBank::kBankSize));
    printf("Send PCLD plus length/bank image, or PCLR to restore factory samples.\n");

    while (true)
    {
        const uint32_t command = WaitForLoaderCommand();
        if (command == kLoaderCommandClear)
        {
            ClearUploadedBank();
            printf("Restart the card to use factory samples, or send PCLD for an upload.\n");
            continue;
        }

        const uint32_t length = ReadU32Blocking();
        if (length == 0 || length > PunkSampleBank::kBankSize)
        {
            printf("ERR BAD_LENGTH\n");
            continue;
        }

        printf("OK SEND %lu\n", static_cast<unsigned long>(length));
        ReceiveAndProgramBank(length);
        printf("Send PCLD for another upload or restart the card.\n");
    }
}

bool BootSwitchHeldDown()
{
    // The three-position switch is on mux position 3 of ADC2, sharing the same
    // path ComputerCard later uses for KnobVal(3). Sample it once before audio
    // starts so the WebSerial loader has a deliberate hardware gesture.
    gpio_put(MX_A, true);
    gpio_put(MX_B, true);
    sleep_us(250);
    adc_select_input(2);

    uint32_t sum = 0;
    for (uint32_t i = 0; i < 32; ++i)
    {
        sum += adc_read();
        sleep_us(20);
    }

    const uint32_t average = sum >> 5;
    return average < 1000;
}
} // namespace

int main()
{
    set_sys_clock_khz(192000, true);

    PunkConfusion card;
    if (BootSwitchHeldDown())
    {
        card.ShowLoaderEntryLeds();
        RunWebSerialLoader();
    }

    card.EnableNormalisationProbe();
    card.Run();
}
