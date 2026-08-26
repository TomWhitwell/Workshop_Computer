#include "computercard.h"
#include "CS80_LUT.h"

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/time.h"

class CS80Card : public ComputerCard
{
public:
    CS80Card()
    {
        voice.enabled = true;
    }

    void ProcessSample() override
    {
        const Switch mode = SwitchVal();
        const bool downNow = mode == Switch::Down;

        int32_t audioPitch = AudioIn1();
        int32_t filterCv = CVIn1();
        int32_t expressionCv = CVIn2();
        bool gateNow = Disconnected(Input::Pulse1) || PulseIn1();

        updateDownSwitch(downNow);

        if (++controlDivider >= ControlIntervalSamples)
        {
            controlDivider = 0;
            updatePanelControls(mode, KnobVal(Knob::Main), KnobVal(Knob::X), KnobVal(Knob::Y));
            updatePitchCache(audioPitch);
            updateLEDs(mode);
        }

        if (gateNow && !lastGate)
            triggerVoice();
        lastGate = gateNow;

        updateGlobalModulation();

        int32_t monoOut = renderVoice(gateNow, filterCv, expressionCv);

        AudioOut1(monoOut);
        AudioOut2(monoOut);

        CVOut1(0);
        CVOut2(0);
        PulseOut1(gateNow);
        PulseOut2(downHeld);
    }

private:
    struct VoiceParams
    {
        int32_t pitchOffsetQ8 = 0;
        int32_t pulseWidth = 2048;
        int32_t pwmDepth = 0;
        int32_t sawPulseMix = 2300;
        int32_t hpCutoff = 300;
        int32_t lpCutoff = 1800;
        int32_t resonance = 1200;
        int32_t attack = 1800;
        int32_t release = 1800;
        uint32_t cachedPhaseIncrement = C2PhaseIncrement;
    };

    struct VoiceState
    {
        bool enabled = false;
        uint32_t phase = 0;
        int32_t ampEnvelopeQ12 = 0;
        int32_t filterEnvelopeQ12 = 0;
        int32_t hpLowpass = 0;
        int32_t lp = 0;
    };

    static constexpr uint32_t C2PhaseIncrement = 5852465u;
    static constexpr uint32_t ControlIntervalSamples = 64u;
    static constexpr uint32_t DownTapSamples = 14400u;
    static constexpr uint32_t DownHoldSamples = 14400u;
    static constexpr uint32_t DownLongHoldSamples = 96000u;
    static constexpr uint32_t LfoBaseIncrement = 89478u;
    static constexpr uint32_t RingBaseIncrement = 1491308u;
    static constexpr int32_t MaxVoicePitchQ8 = 72 * 256;
    static constexpr int32_t MinVoicePitchQ8 = -48 * 256;
    static constexpr int32_t PickupWindow = 96;

    VoiceParams params = {};
    VoiceState voice = {};

    uint32_t controlDivider = 0;
    uint32_t downSamples = 0;
    bool wasDown = false;
    bool downHeld = false;
    bool longHoldSeen = false;
    bool lastGate = false;
    bool pickedUp[3] = {false, false, false};
    Switch lastPanelMode = Switch::Middle;
    bool lastPanelDownHeld = false;

    uint32_t lfoPhase = 0;
    uint32_t ringPhase = 0;
    int32_t lfoValue = 0;
    int32_t ringValue = 0;
    int32_t ringAmount = 0;
    int32_t lfoDepth = 700;
    int32_t performancePitchQ8 = 0;
    int32_t lastPitchInput = 0;

    void updateDownSwitch(bool downNow)
    {
        if (downNow)
        {
            if (downSamples < DownLongHoldSamples + 1u)
                ++downSamples;
            if (downSamples >= DownHoldSamples)
                downHeld = true;
            if (downSamples >= DownLongHoldSamples)
                longHoldSeen = true;
        }
        else
        {
            downSamples = 0;
            downHeld = false;
            longHoldSeen = false;
        }

        wasDown = downNow;
    }

    void updatePanelControls(Switch mode, int32_t main, int32_t x, int32_t y)
    {
        updatePickupContext(mode);

        if (mode == Switch::Up)
        {
            if (pickupReady(0, main, knobValueForPitch(params.pitchOffsetQ8)))
                params.pitchOffsetQ8 = clampPitchQ8((main - 2048) << 2);
            if (pickupReady(1, x, params.pulseWidth - 512))
                params.pulseWidth = clampRange(512 + x, 512, 3584);
            if (pickupReady(2, y, params.sawPulseMix))
            {
                params.pwmDepth = y;
                params.sawPulseMix = y;
            }
            return;
        }

        if (mode == Switch::Middle)
        {
            if (pickupReady(0, main, params.hpCutoff))
                params.hpCutoff = main;
            if (pickupReady(1, x, params.lpCutoff))
                params.lpCutoff = x;
            if (pickupReady(2, y, params.resonance))
                params.resonance = y;
            return;
        }

        if (downHeld)
        {
            if (pickupReady(0, main, clamp12(2048 + performancePitchQ8)))
                performancePitchQ8 = main - 2048;
            if (pickupReady(1, x, ringAmount))
                ringAmount = x;
            if (pickupReady(2, y, lfoDepth))
                lfoDepth = y;
        }
    }

    void updatePickupContext(Switch mode)
    {
        if (mode == lastPanelMode && downHeld == lastPanelDownHeld)
            return;

        pickedUp[0] = false;
        pickedUp[1] = false;
        pickedUp[2] = false;
        lastPanelMode = mode;
        lastPanelDownHeld = downHeld;
    }

    bool pickupReady(uint32_t index, int32_t knob, int32_t target)
    {
        if (pickedUp[index])
            return true;

        int32_t delta = knob - target;
        if (delta < 0)
            delta = -delta;
        if (delta <= PickupWindow)
            pickedUp[index] = true;

        return pickedUp[index];
    }

    int32_t knobValueForPitch(int32_t pitchQ8) const
    {
        return clamp12(2048 + (pitchQ8 >> 2));
    }

    void updatePitchCache(int32_t pitchInput)
    {
        lastPitchInput = pitchInput;
        int32_t pitchCvQ8 = pitchInput * 9; // Hardware-tested rough 1V/oct scale: 341 counts ~= 12 semitones.

        params.cachedPhaseIncrement =
            phaseIncrementFromSemitoneQ8(clampPitchQ8(params.pitchOffsetQ8 + performancePitchQ8 + pitchCvQ8));
    }

    void triggerVoice()
    {
        voice.phase = 0;
        if (voice.ampEnvelopeQ12 < 96)
            voice.ampEnvelopeQ12 = 96;
        if (voice.filterEnvelopeQ12 < 128)
            voice.filterEnvelopeQ12 = 128;
    }

    void updateGlobalModulation()
    {
        uint32_t lfoIncrement = LfoBaseIncrement + ((uint32_t)lfoDepth << 8);
        uint32_t ringIncrement = RingBaseIncrement + ((uint32_t)ringAmount << 9);

        lfoPhase += lfoIncrement;
        ringPhase += ringIncrement;
        lfoValue = sine64(lfoPhase);
        ringValue = sine64(ringPhase);
    }

    int32_t renderVoice(bool gate, int32_t filterCv, int32_t expressionCv)
    {
        if (!voice.enabled)
            return 0;

        updateEnvelope(voice, params, gate);

        uint32_t pwmOffset = ((uint32_t)lfoDepth * (uint32_t)params.pwmDepth) >> 12;
        int32_t pulseWidth = params.pulseWidth + ((lfoValue * (int32_t)pwmOffset) >> 11);
        pulseWidth = clampRange(pulseWidth, 256, 3840);

        voice.phase += params.cachedPhaseIncrement;

        int32_t phase12 = (int32_t)((voice.phase >> 20) & 4095u);
        int32_t saw = phase12 - 2048;
        int32_t pulse = phase12 < pulseWidth ? 1500 : -1500;
        int32_t sineBody = sine64(voice.phase) >> 2;

        int32_t osc = mix(saw + sineBody, pulse + sineBody, params.sawPulseMix);
        osc = applyRingMod(osc);

        int32_t filtered = filterVoice(osc, voice, params, filterCv, expressionCv);
        return clip12((filtered * voice.ampEnvelopeQ12) >> 12);
    }

    void updateEnvelope(VoiceState& state, const VoiceParams& voiceParams, bool gate)
    {
        int32_t attackRate = rateFromControl(voiceParams.attack);
        int32_t releaseRate = rateFromControl(voiceParams.release);

        if (gate)
        {
            state.ampEnvelopeQ12 = clamp12(state.ampEnvelopeQ12 + attackRate);
            state.filterEnvelopeQ12 = clamp12(state.filterEnvelopeQ12 + (attackRate << 1));
        }
        else
        {
            state.ampEnvelopeQ12 = clamp12(state.ampEnvelopeQ12 - releaseRate);
            state.filterEnvelopeQ12 = clamp12(state.filterEnvelopeQ12 - releaseRate);
        }
    }

    int32_t filterVoice(
        int32_t input,
        VoiceState& state,
        const VoiceParams& voiceParams,
        int32_t filterCv,
        int32_t expressionCv)
    {
        int32_t expression = expressionCv;
        int32_t filterMod = filterCv + filterCv;
        int32_t expressionMod = expression + expression;
        int32_t hpControl = clamp12(voiceParams.hpCutoff + filterCv);
        int32_t lpControl = clamp12(voiceParams.lpCutoff + filterMod + expressionMod + (state.filterEnvelopeQ12 >> 3));

        int32_t hpAlpha = curveFromControl(hpControl);
        int32_t lpAlpha = curveFromControl(lpControl);

        state.hpLowpass += (hpAlpha * (input - state.hpLowpass)) >> 12;
        int32_t highpassed = input - state.hpLowpass;

        int32_t resonance = voiceParams.resonance + expression;
        resonance = clamp12(resonance);

        int32_t driven = highpassed - ((state.lp * resonance) >> 11);
        state.lp += (lpAlpha * (driven - state.lp)) >> 12;

        return clip12(state.lp);
    }

    int32_t applyRingMod(int32_t input) const
    {
        if (ringAmount <= 0)
            return input;

        int32_t ringed = (input * ringValue) >> 11;
        return mix(input, ringed, ringAmount);
    }

    void updateLEDs(Switch mode)
    {
        int32_t mainValue = 0;
        int32_t xValue = 0;
        int32_t yValue = 0;

        if (mode == Switch::Up)
        {
            mainValue = knobValueForPitch(params.pitchOffsetQ8);
            xValue = params.pulseWidth - 512;
            yValue = params.sawPulseMix;
        }
        else if (mode == Switch::Middle)
        {
            mainValue = params.hpCutoff;
            xValue = params.lpCutoff;
            yValue = params.resonance;
        }
        else
        {
            mainValue = clamp12(2048 + performancePitchQ8);
            xValue = ringAmount;
            yValue = lfoDepth;
        }

        LedBrightness(0, clamp12(mainValue));
        LedBrightness(1, pickedUp[0] ? 4095 : 384);
        LedBrightness(2, clamp12(xValue));
        LedBrightness(3, pickedUp[1] ? 4095 : 384);
        LedBrightness(4, longHoldSeen ? 4095 : clamp12(yValue));
        LedBrightness(5, pickedUp[2] ? 4095 : 384);
    }

    uint32_t phaseIncrementFromSemitoneQ8(int32_t semitoneQ8) const
    {
        int32_t whole = semitoneQ8 >> 8;
        uint32_t frac = (uint32_t)semitoneQ8 & 255u;
        int32_t octave = 0;

        while (whole < 0)
        {
            whole += 12;
            --octave;
        }

        while (whole >= 12)
        {
            whole -= 12;
            ++octave;
        }

        uint32_t a = cs80SemitoneRatioQ16[whole];
        uint32_t b = whole == 11 ? 131072u : cs80SemitoneRatioQ16[whole + 1];
        uint32_t ratio = a + (((b - a) * frac) >> 8);
        uint32_t inc = (uint32_t)(((uint64_t)C2PhaseIncrement * ratio) >> 16);

        while (octave > 0)
        {
            inc <<= 1;
            --octave;
        }

        while (octave < 0)
        {
            inc >>= 1;
            ++octave;
        }

        return inc < 64u ? 64u : inc;
    }

    int32_t curveFromControl(int32_t control) const
    {
        return cs80FilterAlphaQ12[(uint32_t)clamp12(control) >> 8];
    }

    int32_t rateFromControl(int32_t control) const
    {
        return cs80EnvelopeRateQ12[(uint32_t)clamp12(control) >> 8];
    }

    int32_t sine64(uint32_t phase) const
    {
        return cs80SineLUT[(phase >> 26) & 63u];
    }

    int32_t mix(int32_t a, int32_t b, int32_t amount) const
    {
        amount = clamp12(amount);
        return a + (((b - a) * amount) >> 12);
    }

    int32_t clampPitchQ8(int32_t value) const
    {
        if (value < MinVoicePitchQ8)
            return MinVoicePitchQ8;
        if (value > MaxVoicePitchQ8)
            return MaxVoicePitchQ8;
        return value;
    }

    int32_t clampRange(int32_t value, int32_t low, int32_t high) const
    {
        if (value < low)
            return low;
        if (value > high)
            return high;
        return value;
    }

    int32_t clamp12(int32_t value) const
    {
        if (value < 0)
            return 0;
        if (value > 4095)
            return 4095;
        return value;
    }

    int32_t clip12(int32_t value) const
    {
        if (value < -2048)
            return -2048;
        if (value > 2047)
            return 2047;
        return value;
    }
};

CS80Card card;

void core1Worker()
{
    while (true)
        tight_loop_contents();
}

int main()
{
#if defined(CS80_OVERCLOCK_KHZ) && CS80_OVERCLOCK_KHZ
    set_sys_clock_khz(CS80_OVERCLOCK_KHZ, true);
#endif
    multicore_launch_core1(core1Worker);
    card.EnableNormalisationProbe();
    card.Run();
}
