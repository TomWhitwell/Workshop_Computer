#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "ComputerCard.h"

namespace {
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kControlMask = 31;       // controls at 1.5 kHz
constexpr uint32_t kOneShotStep = 20;       // about 2.2 s for a full-scale sweep
constexpr int32_t kSemitoneMin = -48;
constexpr int32_t kSemitoneMax = 48;
constexpr int32_t kVoices = 16;
constexpr int32_t kDelaySize = 16384;       // 341 ms maximum at 48 kHz
constexpr int32_t kReverbSize = 4096;
constexpr int32_t kChord[kVoices] = {26,38,50,57,62,69,74,81,62,69,74,81,86,86,90,90};
constexpr uint32_t kPitchRatioQ16Lut[] = {
    4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732, 8192, 8679, 9195, 9742,
    10321, 10935, 11585, 12274, 13004, 13777, 14596, 15464, 16384, 17358, 18390, 19484, 20643, 21870,
    23170, 24548, 26008, 27554, 29193, 30929, 32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097,
    52016, 55109, 58386, 61858, 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218,
    116772, 123715, 131072, 138866, 147123, 155872, 165140, 174960, 185364, 196386, 208064, 220436,
    233544, 247431, 262144, 277732, 294247, 311744, 330281, 349920, 370728, 392772, 416128, 440872,
    467088, 494862, 524288, 555464, 588493, 623487, 660561, 699841, 741455, 785544, 832255, 881744,
    934175, 989724, 1048576
};

// Q0.32 phase increments for MIDI notes 0..127 at 48 kHz. Generated once at
// compile time without putting floating point anywhere in the audio loop.
constexpr uint32_t kSemitoneRatioQ30 = 1137589835u; // 2^(1/12) * 2^30
constexpr uint32_t kA4Increment = 39370534u;        // 440 * 2^32 / 48000

constexpr uint32_t mulQ30(uint32_t a, uint32_t b) {
    return uint32_t((uint64_t(a) * b) >> 30);
}

constexpr int32_t positionToMillivolts(uint32_t position) {
    return int32_t((uint64_t(position) * 5000u) >> 16);
}

uint32_t __not_in_flash_func(noteIncrement)(int32_t note) {
    note = note < 0 ? 0 : (note > 127 ? 127 : note);
    uint32_t inc = kA4Increment;
    if (note >= 69) {
        for (int32_t n = 69; n < note; ++n) inc = mulQ30(inc, kSemitoneRatioQ30);
    } else {
        for (int32_t n = note; n < 69; ++n) inc = uint32_t((uint64_t(inc) << 30) / kSemitoneRatioQ30);
    }
    return inc;
}

inline int32_t __not_in_flash_func(saw)(uint32_t phase) {
    return int32_t(phase >> 20) - 2048;
}

inline int32_t __not_in_flash_func(clamp12)(int32_t x) {
    return x < -2048 ? -2048 : (x > 2047 ? 2047 : x);
}
}

class XHTCard final : public ComputerCard {
public:
    XHTCard() {
        for (int32_t i = 0; i < kVoices; ++i) {
            phase_[i] = 0x9e3779b9u * uint32_t(i + 1);
            startInc_[i] = noteIncrement(45 + ((i * 7) & 15));
            targetInc_[i] = noteIncrement(kChord[i]);
        }
    }

    void __not_in_flash_func(ProcessSample)() override {
        const bool p1 = PulseIn1();
        const bool p2 = PulseIn2();

        if ((sampleCounter_++ & kControlMask) == 0) updateControls(p2);
        if (resetRequested_) resetNote();

        // P1 unpatched: sustain. Patched: its high-time is the note length.
        const bool audible = !p1Connected_ || p1;
        envelope_ += ((audible ? 32767 : 0) - envelope_) >> 7;

        int32_t left = 0;
        int32_t right = 0;
        const uint32_t position = position_;
        for (int32_t i = 0; i < kVoices; ++i) {
            const int64_t delta = int64_t(targetInc_[i]) - int64_t(startInc_[i]);
            uint32_t inc = uint32_t(int64_t(startInc_[i]) + ((delta * position) >> 16));
            inc = uint32_t((uint64_t(inc) * pitchRatioQ16_) >> 16);
            phase_[i] += inc;
            const int32_t voice = saw(phase_[i]);
            if (i & 1) right += voice; else left += voice;
        }

        left = (left * envelope_) >> 18;
        right = (right * envelope_) >> 18;

        // Both audio inputs are true stereo effect returns/mix inputs.
        left += AudioIn1() >> 1;
        right += AudioIn2() >> 1;
        processEffects(left, right);

        AudioOut1(int16_t(clamp12(left)));
        AudioOut2(int16_t(clamp12(right)));

        // Pitch mirrors: CV1 is one octave above CV2.
        PulseOut1(p1Connected_ ? p1 : audible);
        PulseOut2(p2);
    }

private:
    void __not_in_flash_func(updateControls)(bool p2) {
        const int32_t main = KnobVal(Knob::Main);
        smoothedCv2_ += (CVIn2() - smoothedCv2_) >> 2;
        smoothedCv1_ += (CVIn1() - smoothedCv1_) >> 2;
        const int32_t cvPos = smoothedCv2_;
        const int32_t manualTarget = (main << 4) + (cvPos << 6);
        int32_t pos = manualTarget;

        p1Connected_ = Connected(Input::Pulse1);
        const bool p2Connected = Connected(Input::Pulse2);
        if (p2Connected) {
            if (p2 && !lastP2_) clockPosition_ += 4096; // sixteen clock steps
            pos = int32_t(clockPosition_);
            oneShotActive_ = false;
        } else {
            oneShotTarget_ = uint32_t(manualTarget < 0 ? 0 : (manualTarget > 65535 ? 65535 : manualTarget));
            if (oneShotActive_) {
                if (position_ >= oneShotTarget_) {
                    position_ = oneShotTarget_;
                    oneShotActive_ = false;
                } else {
                    const uint32_t advanced = position_ + kOneShotStep;
                    position_ = advanced < oneShotTarget_ ? advanced : oneShotTarget_;
                    oneShotActive_ = position_ < oneShotTarget_;
                }
            } else {
                pos = manualTarget;
            }
        }
        lastP2_ = p2;
        if (!oneShotActive_ || p2Connected) {
            position_ = uint32_t(pos < 0 ? 0 : (pos > 65535 ? 65535 : pos));
        }

        // About +/- 4 octaves. Quantised semitones tame the ADC's effective resolution.
        const int32_t semitones = (smoothedCv1_ * 48) >> 11;
        pitchRatioQ16_ = ratioForSemitones(semitones);

        delaySamples_ = 64 + ((KnobVal(Knob::X) * (kDelaySize - 65)) >> 12);
        reverbAmount_ = KnobVal(Knob::Y) << 2;

        const Switch sw = SwitchVal();
        octaveOffset_ = sw == Switch::Up ? 12 : 0;
        const bool switchDown = sw == Switch::Down;
        if (switchDown && !lastSwitchDown_) resetRequested_ = true;
        lastSwitchDown_ = switchDown;
        if (octaveOffset_) pitchRatioQ16_ <<= 1;
        pitchMillivolts_ = ((semitones + octaveOffset_) * 1000) / 12; // control-rate only

        // Mirror the resolved note-position state for external modulation and monitoring.
        const int32_t positionMillivolts = positionToMillivolts(position_);
        CVOut1Millivolts(Connected(Input::CV1) ? pitchMillivolts_ : positionMillivolts);
        CVOut2Millivolts(positionMillivolts);

        LedBrightness(0, uint16_t(position_ >> 4));
        LedBrightness(1, uint16_t(KnobVal(Knob::X)));
        LedBrightness(2, uint16_t(envelope_ >> 3));
        LedBrightness(3, uint16_t(KnobVal(Knob::Y)));
        LedOn(4, PulseIn1());
        LedOn(5, p2);
    }

    static uint32_t __not_in_flash_func(ratioForSemitones)(int32_t semitones) {
        if (semitones < kSemitoneMin) semitones = kSemitoneMin;
        if (semitones > kSemitoneMax) semitones = kSemitoneMax;
        return kPitchRatioQ16Lut[semitones - kSemitoneMin];
    }

    void __not_in_flash_func(processEffects)(int32_t &left, int32_t &right) {
        const uint32_t read = (delayWrite_ - uint32_t(delaySamples_)) & (kDelaySize - 1);
        const int32_t dl = delayL_[read];
        const int32_t dr = delayR_[read];
        delayL_[delayWrite_] = int16_t(clamp12(left + (dr >> 1)));
        delayR_[delayWrite_] = int16_t(clamp12(right + (dl >> 1)));
        delayWrite_ = (delayWrite_ + 1) & (kDelaySize - 1);
        left += dl >> 1;
        right += dr >> 1;

        const uint32_t rr = (reverbWrite_ - 3067u) & (kReverbSize - 1);
        const int32_t wet = reverb_[rr];
        const int32_t mono = (left + right) >> 1;
        reverb_[reverbWrite_] = int16_t(clamp12(mono + ((wet * 3) >> 2)));
        reverbWrite_ = (reverbWrite_ + 1) & (kReverbSize - 1);
        left += (wet * reverbAmount_) >> 15;
        right -= (wet * reverbAmount_) >> 15;
    }

    void __not_in_flash_func(resetNote)() {
        position_ = 0;
        clockPosition_ = 0;
        oneShotActive_ = !Connected(Input::Pulse2);
        const int32_t manualTarget = (KnobVal(Knob::Main) << 4) + (smoothedCv2_ << 6);
        oneShotTarget_ = uint32_t(manualTarget < 0 ? 0 : (manualTarget > 65535 ? 65535 : manualTarget));
        resetRequested_ = false;
    }

    uint32_t phase_[kVoices]{};
    uint32_t startInc_[kVoices]{};
    uint32_t targetInc_[kVoices]{};
    int16_t delayL_[kDelaySize]{};
    int16_t delayR_[kDelaySize]{};
    int16_t reverb_[kReverbSize]{};
    uint32_t delayWrite_ = 0, reverbWrite_ = 0, sampleCounter_ = 0;
    uint32_t position_ = 0, clockPosition_ = 0, pitchRatioQ16_ = 65536, oneShotTarget_ = 0;
    int32_t envelope_ = 32767, delaySamples_ = 4000, reverbAmount_ = 0;
    int32_t pitchMillivolts_ = 0, octaveOffset_ = 0, smoothedCv1_ = 0, smoothedCv2_ = 0;
    bool p1Connected_ = false, lastP2_ = false, resetRequested_ = false;
    bool oneShotActive_ = false, lastSwitchDown_ = false;
};

int main() {
    set_sys_clock_khz(192000, true);
    static XHTCard card;
    card.EnableNormalisationProbe();
    card.Run();
}
