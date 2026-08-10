#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "ComputerCard.h"

namespace {
constexpr uint32_t kControlMask = 31;
constexpr uint32_t kOneShotStep = 20;
constexpr uint32_t kMidiMainActiveTicks = 3000;
constexpr uint32_t kMidiOutPositionQuantum = 2048;
constexpr uint32_t kMidiMainGlideStep = 192;
constexpr int32_t kSemitoneMin = -48;
constexpr int32_t kSemitoneMax = 48;
constexpr int32_t kVoices = 16;
constexpr int32_t kDelaySize = 16384;
constexpr int32_t kReverbSize = 4096;
constexpr int32_t kChord[kVoices] = {26,38,50,57,62,69,74,81,62,69,74,81,86,86,90,90};
constexpr int32_t kStartNotes[kVoices] = {45,52,43,50,47,54,41,48,55,46,53,44,51,42,49,56};
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
constexpr uint32_t kSemitoneRatioQ30 = 1137589835u;
constexpr uint32_t kA4Increment = 39370534u;

constexpr uint32_t mulQ30(uint32_t a, uint32_t b) {
    return uint32_t((uint64_t(a) * b) >> 30);
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

constexpr int32_t positionToMillivolts(uint32_t position) {
    return int32_t((uint64_t(position) * 5000u) >> 16);
}
}

class XHTCardMidi final : public ComputerCard {
public:
    XHTCardMidi() {
        for (int32_t i = 0; i < kVoices; ++i) {
            phase_[i] = 0x9e3779b9u * uint32_t(i + 1);
            startInc_[i] = noteIncrement(kStartNotes[i]);
            targetInc_[i] = noteIncrement(kChord[i]);
        }
    }

    bool ShouldBootUsbHost() {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void SetUsbRole(bool hostMode) {
        usbRoleKnown_ = true;
        usbHostMode_ = hostMode;
    }

    void ProcessUsbMidiByte(uint8_t byte) {
        if (byte == 0xF8u) {
            return;
        }

        if (byte & 0x80u) {
            midiRunningStatus_ = byte;
            midiDataCount_ = 0;
            return;
        }

        const uint8_t type = midiRunningStatus_ & 0xF0u;
        if (type != 0x80u && type != 0x90u && type != 0xB0u)
            return;

        midiData_[midiDataCount_++] = byte & 0x7Fu;
        if (midiDataCount_ < 2u)
            return;

        midiDataCount_ = 0;
        const uint8_t channel = midiRunningStatus_ & 0x0Fu;
        if (channel != midiChannel_)
            return;

        if (type == 0x90u && midiData_[1] > 0) {
            midiTransposeSemitones_ = int32_t(midiData_[0]) - 60;
            if (midiTransposeSemitones_ < -48) midiTransposeSemitones_ = -48;
            if (midiTransposeSemitones_ > 48) midiTransposeSemitones_ = 48;
            if (!midiNotesDown_[midiData_[0]]) {
                midiNotesDown_[midiData_[0]] = true;
                ++midiHeldNotes_;
            }
            midiPerformanceGateEnabled_ = true;
            midiGateOpen_ = true;
            return;
        }

        if (type == 0x80u || (type == 0x90u && midiData_[1] == 0)) {
            if (midiNotesDown_[midiData_[0]]) {
                midiNotesDown_[midiData_[0]] = false;
                if (midiHeldNotes_ > 0)
                    --midiHeldNotes_;
            }
            midiPerformanceGateEnabled_ = true;
            midiGateOpen_ = midiHeldNotes_ > 0 || sustainPedalDown_;
            return;
        }

        if (type == 0xB0u && midiData_[0] == 1u) {
            midiMainControlTargetQ8_ = ((int32_t(midiData_[1]) * 4095) << 8) / 127;
            midiMainActive_ = true;
            midiMainAge_ = 0;
            return;
        }

        if (type == 0xB0u && midiData_[0] == 64u) {
            sustainPedalDown_ = midiData_[1] >= 64u;
            midiPerformanceGateEnabled_ = true;
            midiGateOpen_ = midiHeldNotes_ > 0 || sustainPedalDown_;
        }
    }

    void SendPendingUsbMidiOutput() {
        if (!midiOutGateOpen_) {
            if (activeChordCount_ == 0)
                return;
            for (uint8_t i = 0; i < activeChordCount_; ++i) {
                uint8_t off[3] = {uint8_t(0x80u | midiChannel_), activeChordNotes_[i], 0};
                tud_midi_stream_write(0, off, sizeof(off));
            }
            activeChordCount_ = 0;
            pendingChordSnapshot_ = false;
            return;
        }

        if (!pendingChordSnapshot_)
            return;

        pendingChordSnapshot_ = false;
        for (uint8_t i = 0; i < activeChordCount_; ++i) {
            if (!contains(desiredChordNotes_, desiredChordCount_, activeChordNotes_[i])) {
                uint8_t off[3] = {uint8_t(0x80u | midiChannel_), activeChordNotes_[i], 0};
                tud_midi_stream_write(0, off, sizeof(off));
            }
        }

        for (uint8_t i = 0; i < desiredChordCount_; ++i) {
            if (!contains(activeChordNotes_, activeChordCount_, desiredChordNotes_[i])) {
                uint8_t on[3] = {uint8_t(0x90u | midiChannel_), desiredChordNotes_[i], 80};
                tud_midi_stream_write(0, on, sizeof(on));
            }
        }

        activeChordCount_ = desiredChordCount_;
        for (uint8_t i = 0; i < desiredChordCount_; ++i)
            activeChordNotes_[i] = desiredChordNotes_[i];
    }

    void __not_in_flash_func(ProcessSample)() override {
        const bool p1 = PulseIn1();
        const bool p2 = PulseIn2();

        if ((sampleCounter_++ & kControlMask) == 0) updateControls(p2);
        if (resetRequested_) resetNote();

        const bool p1Audible = !p1Connected_ || p1;
        const bool midiAudible = !midiPerformanceGateEnabled_ || midiGateOpen_;
        const bool audible = p1Audible && midiAudible;
        midiOutGateOpen_ = audible;
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
        left += AudioIn1() >> 1;
        right += AudioIn2() >> 1;
        processEffects(left, right);

        AudioOut1(int16_t(clamp12(left)));
        AudioOut2(int16_t(clamp12(right)));
        PulseOut1(p1Connected_ ? p1 : audible);
        PulseOut2(p2);
    }

private:
    static bool contains(const uint8_t *notes, uint8_t count, uint8_t note) {
        for (uint8_t i = 0; i < count; ++i) {
            if (notes[i] == note)
                return true;
        }
        return false;
    }

    void updateChordSnapshot() {
        const uint32_t quantizedPosition = (position_ + (kMidiOutPositionQuantum >> 1)) & ~(kMidiOutPositionQuantum - 1);
        uint8_t count = 0;
        for (int32_t i = 0; i < kVoices; ++i) {
            int32_t note = kStartNotes[i] +
                int32_t(((int64_t(kChord[i] - kStartNotes[i]) * quantizedPosition) + 32768) >> 16) +
                midiTransposeSemitones_ + octaveOffset_;
            note = note < 0 ? 0 : (note > 127 ? 127 : note);
            uint8_t midiNote = uint8_t(note);
            if (!contains(desiredChordNotes_, count, midiNote))
                desiredChordNotes_[count++] = midiNote;
        }

        for (uint8_t i = 1; i < count; ++i) {
            uint8_t value = desiredChordNotes_[i];
            uint8_t j = i;
            while (j > 0 && desiredChordNotes_[j - 1] > value) {
                desiredChordNotes_[j] = desiredChordNotes_[j - 1];
                --j;
            }
            desiredChordNotes_[j] = value;
        }

        desiredChordCount_ = count;
        if (!midiOutGateOpen_) {
            pendingChordSnapshot_ = activeChordCount_ != 0;
            return;
        }

        bool changed = count != activeChordCount_;
        for (uint8_t i = 0; i < count && !changed; ++i)
            changed = desiredChordNotes_[i] != activeChordNotes_[i];
        if (changed)
            pendingChordSnapshot_ = true;
    }

    void __not_in_flash_func(updateControls)(bool p2) {
        int32_t main = KnobVal(Knob::Main);
        if (midiMainAge_ < 0xffffffffu)
            ++midiMainAge_;
        if (midiMainAge_ >= kMidiMainActiveTicks)
            midiMainActive_ = false;
        midiMainPositionQ8_ += (midiMainControlTargetQ8_ - midiMainPositionQ8_) >> 3;
        const int32_t smoothedMidiMainControl = midiMainPositionQ8_ >> 8;
        if (midiMainActive_)
            main = smoothedMidiMainControl;
        smoothedCv2_ += (CVIn2() - smoothedCv2_) >> 2;
        smoothedCv1_ += (CVIn1() - smoothedCv1_) >> 2;
        const int32_t cvPos = smoothedCv2_;
        const int32_t manualTarget = (main << 4) + (cvPos << 6);
        int32_t pos = manualTarget;

        p1Connected_ = Connected(Input::Pulse1);
        const bool p2Connected = Connected(Input::Pulse2);
        const bool externalClockMode = p2Connected;
        if (externalClockMode) {
            const bool step = (p2 && !lastP2_);
            if (step)
                clockPosition_ += 4096;
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
            } else if (midiMainActive_) {
                if (position_ < oneShotTarget_) {
                    const uint32_t advanced = position_ + kMidiMainGlideStep;
                    position_ = advanced < oneShotTarget_ ? advanced : oneShotTarget_;
                } else if (position_ > oneShotTarget_) {
                    const uint32_t retreated = position_ > kMidiMainGlideStep ? position_ - kMidiMainGlideStep : 0;
                    position_ = retreated > oneShotTarget_ ? retreated : oneShotTarget_;
                }
            } else {
                pos = manualTarget;
            }
        }
        lastP2_ = p2;
        if ((!oneShotActive_ && !midiMainActive_) || externalClockMode)
            position_ = uint32_t(pos < 0 ? 0 : (pos > 65535 ? 65535 : pos));

        const int32_t semitones = ((smoothedCv1_ * 48) >> 11) + midiTransposeSemitones_;
        pitchRatioQ16_ = ratioForSemitones(semitones);

        delaySamples_ = 64 + ((KnobVal(Knob::X) * (kDelaySize - 65)) >> 12);
        reverbAmount_ = KnobVal(Knob::Y) << 2;

        const Switch sw = SwitchVal();
        octaveOffset_ = sw == Switch::Up ? 12 : 0;
        const bool switchDown = sw == Switch::Down;
        if (switchDown && !lastSwitchDown_) resetRequested_ = true;
        lastSwitchDown_ = switchDown;
        if (octaveOffset_)
            pitchRatioQ16_ <<= 1;
        pitchMillivolts_ = ((semitones + octaveOffset_) * 1000) / 12;

        const int32_t positionMillivolts = positionToMillivolts(position_);
        CVOut1Millivolts(Connected(Input::CV1) ? pitchMillivolts_ : positionMillivolts);
        CVOut2Millivolts(positionMillivolts);

        LedBrightness(0, uint16_t(position_ >> 4));
        LedBrightness(1, uint16_t(main));
        LedBrightness(2, uint16_t(envelope_ >> 3));
        LedBrightness(3, uint16_t(midiMainActive_ ? smoothedMidiMainControl : KnobVal(Knob::Y)));
        LedOn(4, PulseIn1());
        usbRoleLedCounter_ += 1;
        const bool usbRoleLed = usbRoleKnown_ && (usbHostMode_ || (usbRoleLedCounter_ & 512u));
        LedOn(5, externalClockMode ? PulseIn2() : usbRoleLed);

        updateChordSnapshot();
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
        const int32_t main = midiMainActive_ ? (midiMainPositionQ8_ >> 8) : KnobVal(Knob::Main);
        const int32_t manualTarget = (main << 4) + (smoothedCv2_ << 6);
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
    int32_t pitchMillivolts_ = 0;
    int32_t octaveOffset_ = 0, midiTransposeSemitones_ = 0;
    int32_t midiMainControlTargetQ8_ = 0, midiMainPositionQ8_ = 0;
    int32_t smoothedCv1_ = 0, smoothedCv2_ = 0;
    uint8_t midiRunningStatus_ = 0, midiData_[2]{}, midiDataCount_ = 0, midiChannel_ = 0;
    uint8_t desiredChordNotes_[kVoices]{}, activeChordNotes_[kVoices]{};
    uint8_t desiredChordCount_ = 0, activeChordCount_ = 0, midiHeldNotes_ = 0;
    bool midiNotesDown_[128]{};
    bool p1Connected_ = false, lastP2_ = false, resetRequested_ = false;
    bool oneShotActive_ = false, lastSwitchDown_ = false;
    bool pendingChordSnapshot_ = false, midiMainActive_ = false, midiOutGateOpen_ = true;
    bool midiPerformanceGateEnabled_ = false, midiGateOpen_ = true, sustainPedalDown_ = false;
    volatile bool usbRoleKnown_ = false, usbHostMode_ = false;
    uint32_t midiMainAge_ = 0xffffffffu;
    uint32_t usbRoleLedCounter_ = 0;
};

static XHTCardMidi card;
static volatile uint8_t hostMidiDeviceAddress = 0;

extern "C" void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx) {
    (void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;
    if (hostMidiDeviceAddress == 0)
        hostMidiDeviceAddress = dev_addr;
}

extern "C" void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)instance;
    if (dev_addr == hostMidiDeviceAddress)
        hostMidiDeviceAddress = 0;
}

extern "C" void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    if (dev_addr != hostMidiDeviceAddress || num_packets == 0)
        return;
    uint8_t cable = 0;
    uint8_t bytes[128];
    while (true) {
        uint32_t count = tuh_midi_stream_read(dev_addr, &cable, bytes, sizeof(bytes));
        if (count == 0)
            break;
        for (uint32_t i = 0; i < count; ++i)
            card.ProcessUsbMidiByte(bytes[i]);
    }
}

extern "C" void tuh_midi_tx_cb(uint8_t dev_addr) {
    (void)dev_addr;
}

void usbMidiWorker() {
    sleep_ms(100);
    bool hostMode = card.ShouldBootUsbHost();
    card.SetUsbRole(hostMode);
    if (hostMode)
        tuh_init(0);
    else
        tud_init(0);

    while (true) {
        if (hostMode) {
            tuh_task();
        } else {
            tud_task();
            card.SendPendingUsbMidiOutput();
            uint8_t bytes[64];
            uint32_t count = tud_midi_stream_read(bytes, sizeof(bytes));
            for (uint32_t i = 0; i < count; ++i)
                card.ProcessUsbMidiByte(bytes[i]);
        }
    }
}

int main() {
    set_sys_clock_khz(192000, true);
    multicore_launch_core1(usbMidiWorker);
    card.EnableNormalisationProbe();
    card.Run();
}
