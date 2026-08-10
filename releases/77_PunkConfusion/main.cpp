// Punk Confusion is built against the shared ComputerCard framework in this
// repository:
//   Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h
// Keep that shared header as the source of truth rather than copying a local
// duplicate into this card folder.
//
// ComputerCard credit:
//   Chris Johnson, version 0.3.0 (12 May 2026 in the current repo copy).
#include "ComputerCard.h"
#include "pico/stdlib.h"
#include <cstdint>

namespace
{
constexpr int32_t kSampleRate = 48000;
constexpr int32_t kMaxAudio = 2047;
constexpr int32_t kMinAudio = -2048;
constexpr uint32_t kVenueDelaySize = 16384;
constexpr uint32_t kApcPulseMax = 2048;
constexpr uint32_t kApcPulseMin = 16;

enum VenueType
{
    VenueCBGB = 0,
    VenueMarquee,
    VenueClub100,
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
    {220, 470, 820, 2800, 1400, 1700, 650, 210, 120, 220, 180, 120, 220},
    // Marquee: tight, sharp, punchy
    {140, 310, 560, 1800, 1200, 1450, 450, 150, 80, 120, 80, 70, 80},
    // 100 Club: warm, dense, sweaty
    {260, 540, 930, 3600, 2000, 1900, 700, 300, 140, -120, 90, 90, 120},
    // Whisky a Go Go: larger, splashier, more stage PA
    {340, 760, 1500, 5200, 2400, 1550, 550, 180, 110, 80, 60, 60, 90},
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

// Four tiny built-in shout shapes. These are intentionally synthetic placeholders
// for the first firmware pass so the trigger/routing behavior can be tested on
// hardware before better sample assets are authored.
constexpr int16_t kSampleHey[] = {
    0, 300, 1200, 1800, 1200, 400, -200, -700, -300, 500, 1100, 700, 100, -200, 0
};
constexpr int16_t kSampleOi[] = {
    0, 900, 1600, 1200, 500, -300, -700, -500, 400, 1000, 700, 200, -150, 0
};
constexpr int16_t kSampleNo[] = {
    0, 500, 1300, 1700, 1300, 800, 200, -400, -800, -400, 100, 450, 100, -120, 0
};
constexpr int16_t kSampleGo[] = {
    0, 600, 1400, 2000, 1500, 600, -100, -550, -250, 650, 1200, 800, 250, 0
};

struct SampleVoice
{
    const int16_t *data = nullptr;
    uint32_t length = 0;
    uint32_t phase = 0; // 24.8 fixed-point sample position.
    uint32_t step = 256;
    int32_t level = 0;
    bool active = false;
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
        sampleBank_[0] = {kSampleHey, static_cast<uint32_t>(sizeof(kSampleHey) / sizeof(kSampleHey[0]))};
        sampleBank_[1] = {kSampleOi, static_cast<uint32_t>(sizeof(kSampleOi) / sizeof(kSampleOi[0]))};
        sampleBank_[2] = {kSampleNo, static_cast<uint32_t>(sizeof(kSampleNo) / sizeof(kSampleNo[0]))};
        sampleBank_[3] = {kSampleGo, static_cast<uint32_t>(sizeof(kSampleGo) / sizeof(kSampleGo[0]))};
    }

    void ProcessSample() override
    {
        const Switch sw = SwitchVal();
        const bool switchDownEdge = SwitchChanged() && sw == Switch::Down;
        const bool vocalTrigger = switchDownEdge || PulseIn2RisingEdge();

        if (vocalTrigger)
        {
            TriggerRandomSample();
            vocalLedCounter_ = 4000;
        }

        int32_t output = 0;

        if (sw == Switch::Up)
        {
            output = RenderApc();
        }
        else
        {
            output = RenderBrokenVenue(sw == Switch::Down);
        }

        AudioOut1(output);
        AudioOut2(output);
        UpdateLeds(sw);
    }

private:
    struct SampleDef
    {
        const int16_t *data;
        uint32_t length;
    };

    DelayLine venueDelay_{};
    SampleDef sampleBank_[4]{};
    SampleVoice voice_{};

    uint32_t rngState_ = 0x13579BDFu;
    uint32_t apcCarrierPhase_ = 0;
    uint32_t apcTriggerPhase_ = 0;
    uint32_t apcPulseTimer_ = 0;
    int32_t venueFilterState_ = 0;
    int32_t venueEnvelope_ = 0;
    int32_t venueNoiseHold_ = 0;
    int32_t vocalLedCounter_ = 0;
    uint32_t venueFlutterCounter_ = 0;

    uint32_t NextRandom()
    {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return rngState_;
    }

    uint32_t KnobToIncrement(int32_t control) const
    {
        if (control < 0) control = 0;
        if (control > 4095) control = 4095;

        // This is not precision pitch tracking. It maps the Workshop controls into
        // a lively APC-like timing range: low values crawl, high values shriek.
        const uint32_t base = 80u + static_cast<uint32_t>(control) * 10u;
        return base << 6;
    }

    uint32_t KnobToPulseLength(int32_t control) const
    {
        if (control < 0) control = 0;
        if (control > 4095) control = 4095;
        return kApcPulseMin + ((static_cast<uint32_t>(control) * (kApcPulseMax - kApcPulseMin)) >> 12);
    }

    VenueType SelectVenue(int32_t roomKnob) const
    {
        if (roomKnob < 1024) return VenueCBGB;
        if (roomKnob < 2048) return VenueMarquee;
        if (roomKnob < 3072) return VenueClub100;
        return VenueWhisky;
    }

    int32_t RenderApc()
    {
        // The APC model here is deliberately simple: one square-wave timer clocks
        // a second pulse generator. That preserves the stepped, rude feeling of the
        // hardware circuit while keeping CPU cost tiny enough for a first pass.
        int32_t cv1 = KnobVal(Knob::X) + CVIn1();
        int32_t cv2 = KnobVal(Knob::Y) + CVIn2();
        const uint32_t triggerIncrement = KnobToIncrement(cv1);
        const uint32_t carrierIncrement = KnobToIncrement(cv2);

        const uint32_t previousTrigger = apcTriggerPhase_;
        apcTriggerPhase_ += triggerIncrement;
        if (apcTriggerPhase_ < previousTrigger)
        {
            apcPulseTimer_ = KnobToPulseLength(cv2);
        }

        apcCarrierPhase_ += carrierIncrement;
        int32_t apcSample = 0;
        if (apcPulseTimer_ > 0)
        {
            apcPulseTimer_--;
            apcSample = (apcCarrierPhase_ & 0x80000000u) ? 1900 : -1900;
        }

        if (!PulseIn1())
        {
            apcSample = 0;
        }

        const int32_t volume = KnobVal(Knob::Main);
        apcSample = (apcSample * volume) >> 12;

        return SoftClip(apcSample);
    }

    int32_t ReadSampleVoice()
    {
        if (!voice_.active || voice_.data == nullptr || voice_.length == 0)
        {
            return 0;
        }

        const uint32_t index = voice_.phase >> 8;
        if (index >= voice_.length)
        {
            voice_.active = false;
            return 0;
        }

        const uint32_t nextIndex = (index + 1 < voice_.length) ? index + 1 : index;
        const int32_t a = voice_.data[index];
        const int32_t b = voice_.data[nextIndex];
        const int32_t frac = static_cast<int32_t>(voice_.phase & 0xFFu);
        const int32_t sample = ((a * (256 - frac)) + (b * frac)) >> 8;

        voice_.phase += voice_.step;
        if ((voice_.phase >> 8) >= voice_.length)
        {
            voice_.active = false;
        }

        return (sample * voice_.level) >> 12;
    }

    void TriggerRandomSample()
    {
        const SampleDef &choice = sampleBank_[NextRandom() & 0x3u];
        voice_.data = choice.data;
        voice_.length = choice.length;
        voice_.phase = 0;
        voice_.step = 180 + ((NextRandom() >> 28) * 22); // small pitch spread
        voice_.level = 3200;
        voice_.active = true;
    }

    int32_t RenderBrokenVenue(bool downHeld)
    {
        int32_t in = AudioIn1();
        int32_t sample = ReadSampleVoice();

        // A small duck helps the vocal stab read clearly over a busy patch.
        if (sample != 0)
        {
            in = (in * 3) >> 2;
        }

        int32_t source = Clamp12(in + sample);

        const int32_t venueAmount = KnobVal(Knob::Main);
        const int32_t roomKnob = KnobVal(Knob::X);
        const int32_t collapse = KnobVal(Knob::Y);
        const VenueProfile &venue = kVenueProfiles[SelectVenue(roomKnob)];

        venueFlutterCounter_++;
        int32_t flutterOffset = 0;
        const int32_t flutterDepth = (static_cast<int32_t>(venue.flutter) * collapse) >> 12;
        if (flutterDepth > 0 && (venueFlutterCounter_ & 0x3F) == 0)
        {
            flutterOffset = static_cast<int32_t>((NextRandom() >> 24) & 0x1F) - 16;
            flutterOffset = (flutterOffset * flutterDepth) >> 4;
        }

        const uint32_t delaySamples = venue.mainDelayBase
            + ((static_cast<uint32_t>(collapse) * venue.mainDelayRange) >> 12)
            + static_cast<uint32_t>(flutterOffset > 0 ? flutterOffset : 0);

        const int32_t tap1 = venueDelay_.Read(venue.tap1);
        const int32_t tap2 = venueDelay_.Read(venue.tap2);
        const int32_t tap3 = venueDelay_.Read(venue.tap3);
        const int32_t delayed = venueDelay_.Read(delaySamples);

        int32_t early = (tap1 + tap2 + tap3) / 3;
        if (SelectVenue(roomKnob) == VenueClub100)
        {
            early = (tap1 + (tap2 << 1) + tap3) >> 2;
        }
        else if (SelectVenue(roomKnob) == VenueMarquee)
        {
            early = ((tap1 << 1) + tap2 + tap3) >> 2;
        }

        const int32_t toneBlend = venue.lowpassBase + ((collapse * venue.lowpassRange) >> 12);
        venueFilterState_ += (delayed - venueFilterState_) / toneBlend;
        int32_t filtered = venueFilterState_;

        filtered = Clamp12(filtered + venue.color);

        int32_t feedback = venue.feedbackBase + ((collapse * venue.feedbackRange) >> 12);
        if (downHeld)
        {
            feedback += 180;
        }
        if (feedback > 3000) feedback = 3000;

        int32_t noiseTarget = 0;
        const int32_t noiseDepth = (static_cast<int32_t>(venue.noise) * collapse) >> 12;
        if (noiseDepth > 0)
        {
            noiseTarget = static_cast<int32_t>((NextRandom() >> 20) & 0x3FF) - 512;
            noiseTarget = (noiseTarget * noiseDepth) >> 8;
        }
        if (downHeld)
        {
            noiseTarget += static_cast<int32_t>((NextRandom() >> 22) & 0x1FF) - 256;
        }
        venueNoiseHold_ = ((venueNoiseHold_ * 29) + (noiseTarget * 3)) >> 5;

        int32_t wet = early + filtered + ((filtered * feedback) >> 12) + venueNoiseHold_;

        // Venue-specific color after the room sum.
        switch (SelectVenue(roomKnob))
        {
        case VenueCBGB:
            wet = SoftClip(wet + (wet >> 2));
            break;
        case VenueMarquee:
            wet = Clamp12(wet + (early >> 1));
            wet = SoftClip(wet);
            break;
        case VenueClub100:
            wet = SoftClip(wet - (wet >> 3));
            break;
        case VenueWhisky:
            wet = SoftClip(wet + (filtered >> 3));
            break;
        }

        venueEnvelope_ += (Abs32(source) - venueEnvelope_) >> 6;
        const int32_t chokeThreshold = 120 + ((collapse * 500) >> 12);
        if (venueEnvelope_ < chokeThreshold && !voice_.active)
        {
            if (SelectVenue(roomKnob) == VenueClub100)
            {
                wet = (wet * 2) >> 1;
            }
            else
            {
                wet = (wet * 3) >> 2;
            }
        }

        // Random dropouts become more likely with collapse, especially in CBGB.
        const int32_t dropoutChance = (static_cast<int32_t>(venue.dropout) * collapse) >> 12;
        if (dropoutChance > 0 && static_cast<int32_t>((NextRandom() >> 20) & 0xFF) < (dropoutChance >> 2))
        {
            wet >>= 1;
        }

        const int32_t writeSample = Clamp12(source + early + ((filtered * feedback) >> 12));
        venueDelay_.Write(static_cast<int16_t>(writeSample));

        const int32_t dryWet = venueAmount;
        return Clamp12(Lerp(source, wet, dryWet));
    }

    void UpdateLeds(Switch sw)
    {
        LedOn(0, sw == Switch::Up);
        LedOn(1, sw == Switch::Middle || sw == Switch::Down);
        LedOn(2, vocalLedCounter_ > 0);
        LedOn(3, sw == Switch::Up && PulseIn1());

        int32_t venueActivity = venueEnvelope_ << 1;
        if (venueActivity > 4095) venueActivity = 4095;
        LedBrightness(4, sw == Switch::Up ? 0 : venueActivity);

        const int32_t clipIndicator = Abs32(venueFilterState_) > 1400 ? 4095 : 0;
        LedBrightness(5, clipIndicator);

        if (vocalLedCounter_ > 0) vocalLedCounter_--;
    }
};
} // namespace

int main()
{
    set_sys_clock_khz(192000, true);

    PunkConfusion card;
    card.Run();
}
