// Punk Confusion uses the local ComputerCard.h copy in this release folder.
// The upstream Demonstrations+HelloWorlds copy is left untouched; this local
// copy carries the newer per-card fixes needed for hardware testing.
//
// ComputerCard credit:
//   Chris Johnson, version 0.3.0 (12 May 2026), MIT licensed.
#include "ComputerCard.h"
#include "pico/stdlib.h"
#include <cstdint>

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
    uint32_t apcTriggerCounter_ = 0;
    uint32_t apcPulseTimer_ = 0;
    int32_t venueFilterState_ = 0;
    int32_t venueEnvelope_ = 0;
    int32_t venueNoiseHold_ = 0;
    int32_t vocalLedCounter_ = 0;
    int32_t apcTriggerLedCounter_ = 0;
    uint32_t apcTriggerLedDivider_ = 0;
    uint32_t venueFlutterCounter_ = 0;

    uint32_t NextRandom()
    {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return rngState_;
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
        return 4096 + (((control - 2048) * 28672) >> 11); // 1.0x..8.0x
    }

    VenueType SelectVenue(int32_t roomKnob) const
    {
        if (roomKnob < 1024) return VenueCBGB;
        if (roomKnob < 2048) return VenueClub100;
        if (roomKnob < 3072) return VenueMarquee;
        return VenueWhisky;
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
        // Keep the shout lower and less frantic than the first pass. A 24.8 step
        // below 256 slows playback and pitches it down without extra DSP cost.
        voice_.step = 132 + ((NextRandom() >> 29) * 10); // about 0.52x..0.79x
        voice_.level = 3000;
        voice_.active = true;
    }

    int32_t RenderBrokenVenue(bool downHeld)
    {
        const int32_t gainControl = KnobVal(Knob::Main);
        int32_t in = (static_cast<int32_t>(AudioIn1()) * InputGainQ12(gainControl)) >> 12;
        int32_t sample = ReadSampleVoice();

        // A small duck helps the vocal stab read clearly over a busy patch.
        if (sample != 0)
        {
            in = (in * 3) >> 2;
        }

        int32_t source = SoftClip(in + sample);

        const int32_t roomKnob = KnobVal(Knob::X);
        const int32_t damageRaw = KnobVal(Knob::Y);
        const int32_t damage = (damageRaw * damageRaw) >> 12;
        const VenueType selectedVenue = SelectVenue(roomKnob);
        const VenueProfile &venue = kVenueProfiles[selectedVenue];

        const uint32_t delaySamples = venue.mainDelayBase
            + ((static_cast<uint32_t>(damage) * venue.mainDelayRange) >> 13);

        const int32_t tap1 = venueDelay_.Read(venue.tap1);
        const int32_t tap2 = venueDelay_.Read(venue.tap2);
        const int32_t tap3 = venueDelay_.Read(venue.tap3);
        const int32_t delayed = venueDelay_.Read(delaySamples);

        int32_t early = 0;
        int32_t roomInput = 0;
        int32_t filterDiv = 8;
        int32_t feedback = 0;

        switch (selectedVenue)
        {
        case VenueCBGB:
            early = ((tap1 << 1) - tap2 + tap3) >> 2;
            roomInput = SoftClip(source + (source >> 1));
            filterDiv = 5;
            feedback = 1400 + ((damage * 360) >> 12);
            break;
        case VenueClub100:
            early = (tap1 + (tap2 << 1) + tap3) >> 2;
            roomInput = SoftClip(source - (source >> 3));
            filterDiv = 14;
            feedback = 1500 + ((damage * 420) >> 12);
            break;
        case VenueMarquee:
            early = ((tap1 << 1) - tap2 + tap3) >> 2;
            roomInput = Clamp12(source + (source >> 3));
            filterDiv = 5;
            feedback = 680 + ((damage * 220) >> 12);
            break;
        case VenueWhisky:
            early = (tap1 + tap2 + (tap3 << 1)) >> 2;
            roomInput = SoftClip(source + (source >> 3));
            filterDiv = 7;
            feedback = 1100 + ((damage * 380) >> 12);
            break;
        }

        feedback = (feedback * damage) >> 12;
        if (downHeld) feedback += (damage >> 5);
        if (feedback > 1700) feedback = 1700;

        venueFilterState_ += (delayed - venueFilterState_) / filterDiv;
        int32_t filtered = venueFilterState_;

        int32_t wet = 0;
        switch (selectedVenue)
        {
        case VenueCBGB:
            wet = SoftClip((roomInput >> 1) + early + (filtered >> 1));
            break;
        case VenueClub100:
            wet = SoftClip((roomInput >> 1) + (early >> 1) + filtered);
            break;
        case VenueMarquee:
            wet = SoftClip((roomInput >> 1) + (early >> 1) - (filtered >> 3));
            break;
        case VenueWhisky:
            wet = SoftClip((roomInput >> 1) + (early >> 1) + (filtered >> 1));
            break;
        }

        venueEnvelope_ += (Abs32(source) - venueEnvelope_) >> 6;
        venueNoiseHold_ = (venueNoiseHold_ * 31) >> 5;

        const int32_t drive = 4096 + (damage >> 1);
        wet = SoftClip((wet * drive) >> 12);

        const int32_t writeSample = Clamp12(roomInput + (early >> 1) + ((wet * feedback) >> 12));
        venueDelay_.Write(static_cast<int16_t>(writeSample));

        return Clamp12((source + wet) >> 1);
    }

    void UpdateLeds(Switch sw)
    {
        const bool apcMode = sw == Switch::Up;
        const bool gateOpen = !Connected(Input::Pulse1) || PulseIn1();

        LedOn(0, apcMode);
        LedOn(1, sw == Switch::Middle || sw == Switch::Down);
        LedOn(2, apcMode ? apcTriggerLedCounter_ > 0 : vocalLedCounter_ > 0);
        LedOn(3, false);

        int32_t venueActivity = venueEnvelope_ << 1;
        if (venueActivity > 4095) venueActivity = 4095;
        LedBrightness(4, apcMode && gateOpen ? 4095 : (apcMode ? 0 : venueActivity));

        const int32_t clipIndicator = !apcMode && Abs32(venueFilterState_) > 1400 ? 4095 : 0;
        LedBrightness(5, clipIndicator);

        if (vocalLedCounter_ > 0) vocalLedCounter_--;
        if (apcTriggerLedCounter_ > 0) apcTriggerLedCounter_--;
    }
};
} // namespace

int main()
{
    set_sys_clock_khz(192000, true);

    PunkConfusion card;
    card.EnableNormalisationProbe();
    card.Run();
}
