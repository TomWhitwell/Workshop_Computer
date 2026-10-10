// Workshop Blueberry 0.3.0, CV keyboard with toggle recording.
//
// CV In 1 receives one output from the Workshop System's 4 Voltages
// module. Its changing analogue value is treated as a small keyboard, then
// quantised to a fixed major-pentatonic scale before being recorded or played.

#include <cstdint>

#include "hardware/clocks.h"
#include "ComputerCard.h"

class WorkshopBlueberry final : public ComputerCard
{
public:
    WorkshopBlueberry()
    {
        // The normalisation probe distinguishes an absent clock from a quiet
        // patch, and lets playback retain recorded timing when unclocked.
        EnableNormalisationProbe();
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        // This card has no audio role. Keep the DAC quiet while the 48 kHz
        // framework services calibrated inputs and accurate CV output.
        AudioOut1(0);
        AudioOut2(0);

        // Framework edge flags last one audio sample. Preserve them until
        // the 1 kHz control tick, including short external gate triggers.
        if (PulseIn1RisingEdge()) clockEdgePending_ = true;
        if (Connected(Input::Pulse2) && PulseIn2()) gateHighSeen_ = true;

        if (++controlDivider_ != kControlDivider) return;
        controlDivider_ = 0;
        ControlTick();
        clockEdgePending_ = false;
        gateHighSeen_ = false;
    }

private:
    static constexpr uint8_t kControlDivider = 48; // 1 kHz control/event rate.
    static constexpr uint16_t kMaximumEvents = 512;
    static constexpr int32_t kOutputMinimumMv = -6000;
    static constexpr int32_t kOutputMaximumMv = 6000;

    struct Event
    {
        int16_t pitchMv;
        uint16_t durationTicks;
        bool gate;
    };

    struct NoteState
    {
        int16_t pitchMv;
        bool gate;
    };

    Event events_[kMaximumEvents] = {};
    uint16_t eventCount_ = 0;
    uint16_t playIndex_ = 0;
    uint16_t playTicksRemaining_ = 0;
    uint8_t controlDivider_ = 0;
    int8_t transposeNotches_ = 0;
    bool mainReady_ = false;
    bool clockEdgePending_ = false;
    bool gateHighSeen_ = false;
    bool recording_ = false;
    bool hadLoop_ = false;
    Switch stableSwitch_ = Switch::Middle;
    Switch candidateSwitch_ = Switch::Middle;
    uint8_t switchTicks_ = 0;
    bool recordArmed_ = false;
    Switch previousSwitch_ = Switch::Middle;
    uint8_t loopPulseTicks_ = 0;

    static int32_t Clamp(int32_t value, int32_t minimum, int32_t maximum)
    {
        if (value < minimum) return minimum;
        if (value > maximum) return maximum;
        return value;
    }

    static int32_t Abs(int32_t value)
    {
        return value < 0 ? -value : value;
    }

    static bool SameState(NoteState left, NoteState right)
    {
        return left.pitchMv == right.pitchMv && left.gate == right.gate;
    }

    // Blueberry's left/right buttons did not move purely in octaves. A step
    // alternates a fifth and a fourth: 0, +7, +12, +19 semitones and likewise
    // downward. Preserve that compact, musically useful behaviour here.
    static int32_t BlueberryNotchesToMillivolts(int32_t notches)
    {
        return (notches * 6 + (notches & 1)) * 1000 / 12;
    }

    // The first pass intentionally uses Blueberry's default major-pentatonic
    // notes at standard 1 V/oct. Pitch and note-off are independent inputs.
    static int32_t QuantiseMajorPentatonic(int32_t inputMv)
    {
        static constexpr int32_t degrees[] = {0, 2, 5, 7, 9};
        int32_t nearest = 0;
        int32_t nearestDistance = 0x7fffffff;
        for (int32_t octave = -6; octave <= 6; ++octave)
        {
            for (int32_t degree : degrees)
            {
                const int32_t candidate = (octave * 12 + degree) * 1000 / 12;
                const int32_t distance = Abs(inputMv - candidate);
                if (distance < nearestDistance)
                {
                    nearest = candidate;
                    nearestDistance = distance;
                }
            }
        }
        return nearest;
    }

    void UpdateTranspose(int32_t main)
    {
        // Main behaves as a springless virtual pair of buttons. A deliberate
        // turn beyond 3 o'clock or 9 o'clock changes one notch; returning to
        // centre arms the next turn, avoiding repeated increments while a pot
        // remains at either extreme.
        const bool centre = main >= 1024 && main <= 3071;
        if (centre)
        {
            mainReady_ = true;
            return;
        }
        if (!mainReady_) return;

        if (main > 3071)
        {
            transposeNotches_ = static_cast<int8_t>(Clamp(transposeNotches_ + 1, -8, 7));
        }
        else
        {
            transposeNotches_ = static_cast<int8_t>(Clamp(transposeNotches_ - 1, -8, 7));
        }
        mainReady_ = false;
    }

    NoteState LiveNote()
    {
        const int32_t keyboardMv = Connected(Input::CV1) ? CVIn1Millivolts() : 0;
        const int32_t quantisedMv = QuantiseMajorPentatonic(keyboardMv);
        const int32_t outputMv = Clamp(quantisedMv + BlueberryNotchesToMillivolts(transposeNotches_),
                                       kOutputMinimumMv, kOutputMaximumMv);
        return {static_cast<int16_t>(outputMv),
                Connected(Input::Pulse2) && (PulseIn2() || gateHighSeen_)};
    }

    void AppendEvent(NoteState state)
    {
        if (eventCount_ == kMaximumEvents)
        {
            // Retain the final state rather than corrupting a full loop.
            return;
        }
        events_[eventCount_++] = {state.pitchMv, 1, state.gate};
    }

    void BeginRecording(NoteState state)
    {
        eventCount_ = 0;
        hadLoop_ = false;
        recording_ = true;
        loopPulseTicks_ = 0;
        AppendEvent(state);
    }

    void StopRecording()
    {
        recording_ = false;
        hadLoop_ = eventCount_ != 0;
        StartPlayback();
    }

    bool RecordPressed(Switch requested)
    {
        if (requested != candidateSwitch_)
        {
            candidateSwitch_ = requested;
            switchTicks_ = 1;
        }
        else if (switchTicks_ < 10)
        {
            ++switchTicks_;
        }
        if (switchTicks_ < 10) return false;

        const bool changed = stableSwitch_ != candidateSwitch_;
        stableSwitch_ = candidateSwitch_;
        if (stableSwitch_ != Switch::Down)
        {
            recordArmed_ = true;
            return false;
        }
        // Require a settled release between presses. Booting with Down held
        // must not accidentally erase the loop or begin a recording.
        if (!changed || !recordArmed_) return false;
        recordArmed_ = false;
        return true;
    }

    void Capture(NoteState state)
    {
        if (eventCount_ == 0)
        {
            AppendEvent(state);
            return;
        }

        Event &last = events_[eventCount_ - 1];
        const NoteState previous = {last.pitchMv, last.gate};
        if (SameState(previous, state) && last.durationTicks < 0xffff)
        {
            ++last.durationTicks;
        }
        else
        {
            AppendEvent(state);
        }
    }

    void StartPlayback()
    {
        playIndex_ = 0;
        playTicksRemaining_ = eventCount_ == 0 ? 0 : events_[0].durationTicks;
    }

    void AdvancePlayback()
    {
        if (eventCount_ == 0) return;
        ++playIndex_;
        if (playIndex_ >= eventCount_)
        {
            playIndex_ = 0;
            loopPulseTicks_ = 2; // 2 ms loop-start marker on Pulse Out 2.
        }
        playTicksRemaining_ = events_[playIndex_].durationTicks;
    }

    NoteState PlaybackNote()
    {
        if (!hadLoop_ || eventCount_ == 0) return {0, false};

        if (Connected(Input::Pulse1))
        {
            // Blueberry's external clock advances one captured note event per
            // rising edge. Recorded timing is used only while unclocked.
            if (clockEdgePending_) AdvancePlayback();
        }
        else
        {
            if (playTicksRemaining_ > 0) --playTicksRemaining_;
            if (playTicksRemaining_ == 0) AdvancePlayback();
        }

        const Event &event = events_[playIndex_];
        return {event.pitchMv, event.gate};
    }

    void UpdateOutputs(NoteState state)
    {
        CVOut1Millivolts(state.pitchMv);
        CVOut2(0);
        PulseOut1(state.gate);
        PulseOut2(loopPulseTicks_ != 0);
        if (loopPulseTicks_ != 0) --loopPulseTicks_;
    }

    void UpdateLeds(Switch mode, NoteState state)
    {
        // Left column: calibration, record, gate. Right column: mode, clock,
        // and current quantised pitch. This keeps the first test observable.
        LedOn(0, InputsCalibrated());
        LedOn(2, recording_);
        LedOn(4, state.gate);
        LedOn(1, mode == Switch::Up && hadLoop_ && !recording_);
        LedOn(3, Connected(Input::Pulse1));
        LedBrightness(5, static_cast<uint16_t>(Clamp(state.pitchMv + 6000, 0, 12000) >> 2));
    }

    void ControlTick()
    {
        UpdateTranspose(KnobVal(Knob::Main));
        const NoteState live = LiveNote();
        const bool recordPressed = RecordPressed(SwitchVal());
        const Switch mode = stableSwitch_;
        bool startedRecording = false;

        if (recordPressed)
        {
            if (recording_) StopRecording();
            else
            {
                BeginRecording(live);
                startedRecording = true;
            }
        }
        // Up is always playback: finish an active take before playing it.
        if (recording_ && mode == Switch::Up) StopRecording();
        if (recording_ && !startedRecording)
        {
            Capture(live);
        }

        if (mode == Switch::Up && previousSwitch_ != Switch::Up)
        {
            StartPlayback();
        }

        const NoteState output = mode == Switch::Up ? PlaybackNote() : live;
        UpdateOutputs(output);
        UpdateLeds(mode, output);
        previousSwitch_ = mode;
    }
};

int main()
{
    // A 48 MHz multiple keeps ADC and 19-bit CV PWM free of avoidable beating
    // tones, as recommended by ComputerCard.
    set_sys_clock_khz(192000, true);

    WorkshopBlueberry card;
    card.Run();
    return 0;
}
