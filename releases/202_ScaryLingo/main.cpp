// ScaryLingo - ring modulator for the Music Thing
// Modular Workshop Computer.
//
// Copyright 2026 Adrian Vos. MIT licensed.
//
// Attribution:
// - Built on ComputerCard by Chris Johnson, copied here as ComputerCard.h.
// - The fixed-point ring-modulation conventions were informed by Alloy
//   (`Workshop_Computer/releases/97_alloy/dsp/xmod_algorithms.h`), which
//   itself documents its Mutable Instruments Warps/Parasites DSP lineage.
// - The Pico SDK import helper is Raspberry Pi (Trading) Ltd. BSD-3-Clause
//   code, included unchanged as pico_sdk_import.cmake.
// See ATTRIBUTION.md for the fuller repository attribution notes.
//
// Audio In 1 is the programme input: voice, drum machine, radio, oscillator,
// whatever you want to send into the modulator. Audio In 2 is an optional
// external carrier which replaces the internal carrier when patched.
//
// Switch middle is the performance page, with soft pickup:
//   MAIN: carrier frequency
//   X: dry/ring mix
//   Y: input/ring drive
//
// Switch up is a temporary modulation/LFO-waveform mode:
//   MAIN: LFO rate (0.1-25 Hz), soft pickup
//   X: LFO depth into carrier frequency, soft pickup and hard zero at minimum
//   Y: LFO waveform, sine-like through square-like, soft pickup
//
// Tap switch down to cycle three internal carrier types:
//   0 Extermin8: sine-like carrier
//   1 Cyber: square-like carrier
//   2 Blend: halfway carrier waveform

#include <cstdint>

#include "ComputerCard.h"
#include "hardware/clocks.h"

namespace scarylingo
{
constexpr int32_t kSampleMax = 2047;
constexpr int32_t kSampleMin = -2048;
constexpr int32_t kParamMax = 4095;

inline int32_t Clip(int32_t x)
{
    if (x > kSampleMax) return kSampleMax;
    if (x < kSampleMin) return kSampleMin;
    return x;
}

inline int32_t Abs(int32_t x)
{
    return x < 0 ? -x : x;
}

inline int32_t Clamp(int32_t x, int32_t lo, int32_t hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

inline int32_t Triangle(uint32_t phase)
{
    uint32_t x = phase >> 20; // 0..4095
    if (x >= 2048) x = 4095 - x;
    return static_cast<int32_t>(x) * 2 - 2048;
}

inline int32_t Sineish(uint32_t phase)
{
    int32_t tri = Triangle(phase);
    int32_t bend = (tri * Abs(tri)) >> 11;
    return Clip((tri * 3 - bend) >> 1);
}

inline int32_t Square(uint32_t phase)
{
    return (phase & 0x80000000u) ? 2047 : -2048;
}

inline int32_t Crossfade(int32_t a, int32_t b, int32_t amount)
{
    return Clip((a * (kParamMax - amount) + b * amount) >> 12);
}

// Phase increments for logarithmically spaced MF-102 carrier frequencies at
// 48 kHz: 0.6-80 Hz over control values 0-2047, then 80 Hz-4 kHz over
// 2048-4095. Linear interpolation between table entries keeps the mapping
// compact while retaining fine control at low rates.
inline int32_t CarrierPhaseStep(int32_t control)
{
    static constexpr int32_t kLowRange[17] = {
        53687, 72892, 98966, 134368, 182433, 247693, 336296, 456594,
        619925, 841682, 1142764, 1551548, 2106560, 2860109, 3883213,
        5272298, 7158279
    };
    static constexpr int32_t kHighRange[17] = {
        7158279, 9141011, 11672929, 14906150, 19034922, 24307301,
        31040046, 39637658, 50616675, 64636709, 82540076, 105402397,
        134597227, 171878573, 219486273, 280280569, 357913941
    };

    control = Clamp(control, 0, kParamMax);
    const int32_t *table = control <= 2047 ? kLowRange : kHighRange;
    const int32_t position = control <= 2047 ? control : control - 2048;
    constexpr int32_t kRangeSpan = 2047;
    const int32_t scaled = static_cast<int32_t>(
        (static_cast<int64_t>(position) * 16 * 4096) / kRangeSpan);
    const int32_t index = scaled >> 12;
    if (index >= 16) return table[16];
    return table[index] + static_cast<int32_t>(
        (static_cast<int64_t>(table[index + 1] - table[index]) *
         (scaled & 4095)) >> 12);
}

// Converts a signed Q12 octave offset into a Q12 frequency multiplier. The
// LFO amount is calibrated to +/-1.5 octaves at full scale: three octaves
// peak-to-peak, matching the MF-102 specification.
inline int32_t PitchMultiplierQ12(int32_t octavesQ12)
{
    static constexpr int32_t kOctave[17] = {
        4096, 4277, 4467, 4664, 4871, 5087, 5312, 5547, 5793,
        6049, 6317, 6597, 6889, 7194, 7512, 7845, 8192
    };

    bool negative = octavesQ12 < 0;
    int32_t magnitude = negative ? -octavesQ12 : octavesQ12;
    int32_t wholeOctaves = magnitude >> 12;
    int32_t fraction = magnitude & 4095;
    int32_t scaled = fraction << 4;
    int32_t index = scaled >> 12;
    int32_t multiplier = kOctave[index] + static_cast<int32_t>(
        (static_cast<int64_t>(kOctave[index + 1] - kOctave[index]) *
         (scaled & 4095)) >> 12);
    multiplier <<= wholeOctaves;

    if (negative)
    {
        return static_cast<int32_t>((static_cast<int64_t>(4096) * 4096) / multiplier);
    }
    return multiplier;
}

inline int32_t SoftLimit(int32_t x)
{
    if (x > 4095) x = 4095;
    if (x < -4096) x = -4096;

    int32_t x2 = (x * x) >> 12;
    int32_t x3 = (x2 * x) >> 12;
    return Clip((x - x3 / 3) >> 1);
}

inline int32_t AnalogRing(int32_t input, int32_t carrier, int32_t gain)
{
    // Four-quadrant multiplication guarantees silence when either input is
    // silent, avoiding carrier feedthrough. Saturation adds the analogue-style
    // character without introducing an independent digital ring path.
    int32_t clean = (input * carrier) >> 11;
    int32_t stageGain = 4096 + gain * 6;
    int32_t saturated = SoftLimit(static_cast<int32_t>(
        (static_cast<int64_t>(clean) * stageGain) >> 12));
    return Crossfade(clean, saturated, Clamp(gain, 0, kParamMax));
}

class DcBlock
{
public:
    int32_t Process(int32_t x)
    {
        int32_t y = x - lastIn_ + ((lastOut_ * 4088) >> 12);
        lastIn_ = x;
        lastOut_ = Clip(y);

        if (Abs(lastOut_) < 10) return 0;
        return lastOut_;
    }

private:
    int32_t lastIn_ = 0;
    int32_t lastOut_ = 0;
};

inline int32_t LevelToLed(int32_t x)
{
    int32_t b = Abs(x) << 1;
    return b > 4095 ? 4095 : b;
}

} // namespace scarylingo

class ScaryLingo : public ComputerCard
{
public:
    ScaryLingo()
    {
        EnableNormalisationProbe();
    }

    virtual void ProcessSample() override
    {
        if (startupSamples_ > 0)
        {
            ShowStartup();
            return;
        }

        if (PulseIn1RisingEdge())
        {
            lfoPhase_ = 0;
            lfoResetFlash_ = 12000;
        }

        const bool characterPage = SwitchVal() == Switch::Up;
        const bool performancePage = SwitchVal() == Switch::Middle;
        const int32_t main = KnobVal(Knob::Main);
        const int32_t x = KnobVal(Knob::X);
        const int32_t y = KnobVal(Knob::Y);
        const bool switchChanged = SwitchChanged();

        if (SwitchVal() == Switch::Down && switchChanged)
        {
            voice_ = (voice_ + 1) % 3;
            voiceFlash_ = 24000;
        }

        if (!controlPageInitialized_)
        {
            if (characterPage) ArmUpPickups(main, x, y);
            else ArmPerformancePickups(main, x, y);
            controlPageInitialized_ = true;
        }
        else if (characterPage && switchChanged)
        {
            ArmUpPickups(main, x, y);
        }
        else if (performancePage && switchChanged)
        {
            ArmPerformancePickups(main, x, y);
        }

        if (characterPage)
        {
            if (CaptureUpControl(main, lfoRate_, lastUpMain_, lfoRatePickedUp_))
            {
                lfoRate_ += (main - lfoRate_) >> 8;
            }

            // The very bottom of X is an unambiguous LFO-off position. It
            // also arms pickup, so turning X up from zero responds normally.
            if (x <= kLfoDepthOffThreshold)
            {
                lfoDepth_ = 0;
                lfoDepthPickedUp_ = true;
                lastUpX_ = x;
            }
            else if (CaptureUpControl(x, lfoDepth_, lastUpX_, lfoDepthPickedUp_))
            {
                lfoDepth_ += (x - lfoDepth_) >> 8;
            }

            if (CaptureUpControl(y, lfoWave_, lastUpY_, lfoWavePickedUp_))
            {
                lfoWave_ += (y - lfoWave_) >> 8;
            }
        }
        else if (performancePage)
        {
            if (CapturePerformanceControl(main, freq_, lastPerformanceMain_, freqPickedUp_))
            {
                freq_ += (main - freq_) >> 8;
            }
            if (CapturePerformanceControl(x, mix_, lastPerformanceX_, mixPickedUp_))
            {
                mix_ += (x - mix_) >> 8;
            }
            if (CapturePerformanceControl(y, drive_, lastPerformanceY_, drivePickedUp_))
            {
                drive_ += (y - drive_) >> 8;
            }
        }

        const bool programmePatched = Connected(Input::Audio1);
        const bool externalCarrierPatched = Connected(Input::Audio2);
        // Hard-mute unpatched audio inputs. This is deliberately redundant
        // with ComputerCard's probe handling: it keeps floating ADC residue
        // out of the dry path on real hardware.
        int32_t input = programmePatched ? inputBlock_.Process(AudioIn1()) : 0;
        int32_t externalCarrier = externalCarrierPatched
            ? carrierBlock_.Process(AudioIn2()) : 0;

        int32_t driveGain = 4096 + ((drive_ * 7) >> 2);
        input = scarylingo::SoftLimit((input * driveGain) >> 12);

        // 0.1 Hz to about 25 Hz, with useful resolution at slow rates.
        lfoPhase_ += 9000u + static_cast<uint32_t>(
            (static_cast<int64_t>(lfoRate_) * lfoRate_ * 2230000) >> 24);
        int32_t lfoSine = scarylingo::Sineish(lfoPhase_);
        int32_t lfoSquare = scarylingo::Square(lfoPhase_);
        int32_t lfo = scarylingo::Crossfade(lfoSine, lfoSquare, lfoWave_);

        // Middle is the stable performance sound. Up applies LFO pitch
        // modulation over a calibrated three-octave peak-to-peak span.
        int32_t lfoPitchOctaves = characterPage
            ? static_cast<int32_t>((static_cast<int64_t>(lfo) * lfoDepth_ * 3) / 4095)
            : 0;

        int32_t freqControl = scarylingo::Clamp(freq_ + (CVIn1() << 1), 0, 4095);
        int32_t carrierStep = scarylingo::CarrierPhaseStep(freqControl);
        carrierStep = static_cast<int32_t>(
            (static_cast<int64_t>(carrierStep) *
             scarylingo::PitchMultiplierQ12(lfoPitchOctaves)) >> 12);
        carrierPhase_ += static_cast<uint32_t>(
            scarylingo::Clamp(carrierStep, 1, 900000000));

        int32_t sine = scarylingo::Sineish(carrierPhase_);
        int32_t square = scarylingo::Square(carrierPhase_);

        const int32_t carrierTypes[3] = {0, 4095, 2048};
        int32_t carrierShape = carrierTypes[voice_];

        int32_t internalCarrier = scarylingo::Crossfade(
            sine, square, scarylingo::Clamp(carrierShape, 0, 4095));

        // A patched Audio In 2 is the carrier, full stop. This avoids the
        // internal oscillator leaking through external-carrier patches.
        int32_t carrier = externalCarrierPatched ? externalCarrier : internalCarrier;

        int32_t ring = scarylingo::AnalogRing(input, carrier, drive_);

        int32_t mixControl = scarylingo::Clamp(mix_ + CVIn2(), 0, 4095);
        if (PulseIn2())
        {
            int32_t trem = (lfo + 2048) >> 1;
            mixControl = (mixControl * trem) >> 11;
        }

        int32_t out = scarylingo::Crossfade(input, ring, mixControl);
        AudioOut1(out);
        AudioOut2(ring);

        CVOut1(lfo);
        CVOut2(carrier >> 1);
        PulseOut1(lfo > 0);
        PulseOut2(scarylingo::Abs(input) > 768);

        LedBrightness(0, scarylingo::LevelToLed(input));
        LedBrightness(1, scarylingo::LevelToLed(ring));
        LedBrightness(2, freq_);
        LedBrightness(3, mixControl);
        LedBrightness(4, scarylingo::Clamp(lfo + 2048, 0, 4095));

        if (voiceFlash_ > 0)
        {
            voiceFlash_--;
            const int32_t levels[3] = {1000, 2450, 4095};
            LedBrightness(5, levels[voice_]);
        }
        else if (lfoResetFlash_ > 0)
        {
            lfoResetFlash_--;
            LedBrightness(5, 4095);
        }
        else
        {
            LedBrightness(5, characterPage ? lfoWave_ : drive_);
        }
    }

private:
    static constexpr int32_t kPickupThreshold = 64;
    static constexpr int32_t kLfoDepthOffThreshold = 32;

    void ArmUpPickups(int32_t main, int32_t x, int32_t y)
    {
        lfoRatePickedUp_ = false;
        lfoDepthPickedUp_ = false;
        lfoWavePickedUp_ = false;
        lastUpMain_ = main;
        lastUpX_ = x;
        lastUpY_ = y;
    }

    void ArmPerformancePickups(int32_t main, int32_t x, int32_t y)
    {
        freqPickedUp_ = false;
        mixPickedUp_ = false;
        drivePickedUp_ = false;
        lastPerformanceMain_ = main;
        lastPerformanceX_ = x;
        lastPerformanceY_ = y;
    }

    bool CaptureUpControl(int32_t raw, int32_t held, int32_t &lastRaw,
                          bool &pickedUp)
    {
        if (pickedUp)
        {
            lastRaw = raw;
            return true;
        }

        const int32_t previousSide = lastRaw - held;
        const int32_t currentSide = raw - held;
        if (scarylingo::Abs(currentSide) <= kPickupThreshold ||
            (previousSide < 0 && currentSide >= 0) ||
            (previousSide > 0 && currentSide <= 0))
        {
            pickedUp = true;
        }
        lastRaw = raw;
        return pickedUp;
    }

    bool CapturePerformanceControl(int32_t raw, int32_t held, int32_t &lastRaw,
                                   bool &pickedUp)
    {
        return CaptureUpControl(raw, held, lastRaw, pickedUp);
    }

    void ShowStartup()
    {
        constexpr int32_t kSamplesPerLed = 4800;
        int32_t active = (28800 - startupSamples_) / kSamplesPerLed;
        for (int32_t i = 0; i < 6; i++)
        {
            LedOn(i, i == active);
        }
        startupSamples_--;
    }

    scarylingo::DcBlock inputBlock_;
    scarylingo::DcBlock carrierBlock_;

    uint32_t carrierPhase_ = 0;
    uint32_t lfoPhase_ = 0;

    int32_t startupSamples_ = 28800;
    int32_t voiceFlash_ = 0;
    int32_t lfoResetFlash_ = 0;
    int32_t voice_ = 0;

    int32_t freq_ = 1700;
    int32_t mix_ = 3000;
    int32_t drive_ = 1300;
    int32_t lfoRate_ = 900;
    int32_t lfoDepth_ = 0;
    int32_t lfoWave_ = 0;

    bool controlPageInitialized_ = false;
    int32_t lastUpMain_ = 0;
    int32_t lastUpX_ = 0;
    int32_t lastUpY_ = 0;
    bool lfoRatePickedUp_ = false;
    bool lfoDepthPickedUp_ = false;
    bool lfoWavePickedUp_ = false;

    int32_t lastPerformanceMain_ = 0;
    int32_t lastPerformanceX_ = 0;
    int32_t lastPerformanceY_ = 0;
    bool freqPickedUp_ = false;
    bool mixPickedUp_ = false;
    bool drivePickedUp_ = false;
};

int main()
{
    set_sys_clock_khz(144000, true);

    ScaryLingo card;
    card.Run();
}
