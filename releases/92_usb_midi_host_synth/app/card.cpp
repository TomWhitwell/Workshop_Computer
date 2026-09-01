#include "card.h"

#include "profile_meter.h"

#include "adsr.h"
#include "config_store.h"
#include "drums.h"
#include "glyph_leds.h"
#include "param_maps.h"
#include "runtime_state.h"
#include "voice_matrix.h"
#include "synth.h"
#include "voices.h"

#include "pico/multicore.h"

UsbMidiHostCard *g_card = nullptr;
volatile ComputerCard::USBPowerState_t g_powerState = ComputerCard::Unsupported;

UsbMidiHostCard::UsbMidiHostCard()
{
    g_card = this;
    g_cvOutsCalibrated = CVOutsCalibrated();
    voicesInit();
    initLuts();
    initAdsrLuts();
    drumsInit();
    loadConfigFromFlash();
    updateEnvSusLevel(g_ext.sustain);
    // Core 0 pauses in RAM during flash writes initiated from core 1.
    multicore_lockout_victim_init();
    multicore_launch_core1(core1Entry);
}

void UsbMidiHostCard::writeMidiCv(uint8_t noteA, int16_t bendA, uint8_t noteB,
                                  int16_t bendB, uint8_t bendSemitones)
{
    auto millivolts = [&](uint8_t note, int16_t bend) -> int32_t {
        int32_t mv = ((int32_t)note - 60) * 1000 / 12;
        mv += (int32_t)(((int64_t)bend * (int64_t)bendSemitones * 1000) /
                        (8192 * 12));
        return mv;
    };
    CVOut1Millivolts(millivolts(noteA, bendA));
    CVOut2Millivolts(millivolts(noteB, bendB));
}

void UsbMidiHostCard::setLedIfChanged(int i, bool on)
{
    uint8_t v = on ? 1 : 0;
    if (ledShadow_[i] != v)
    {
        ledShadow_[i] = v;
        LedOn(i, on);
    }
}

void UsbMidiHostCard::ProcessSample()
{
    g_processSampleMeter.beginSample();

    if (bootSamples_ < 4800)
    {
        ++bootSamples_;
        if (SwitchVal() != Down)
            bootHoldOk_ = false;
        if (bootSamples_ == 4800 && bootHoldOk_)
            factoryResetLatch_ = true;
    }

    g_panelMain = KnobVal(Knob::Main);
    g_panelX = KnobVal(Knob::X);
    g_panelY = KnobVal(Knob::Y);
    g_panelSwitch = (uint8_t)SwitchVal();

    if (g_appMode != (uint8_t)AppMode::Setup)
        applyKnobMappedSlots();

    serviceSetupControls();

    uint8_t bendRange = g_config.bendSemitones;
    if (bendRange < 1)
        bendRange = 1;

    bool setup = g_appMode == (uint8_t)AppMode::Setup;

    // NEVER take g_midiCs here. Blocking the ADC ISR while core1 holds the
    // lock for note/bend handling stalls mux advancing → knobs flutter /
    // stick at mid-scale while MIDI audio still plays.
    uint8_t noteA = g_noteNumA;
    uint8_t noteB = g_noteNumB;
    bool gateA = g_gateA;
    bool gateB = g_gateB;
    int16_t bendA = g_pitchBendA;
    int16_t bendB = g_pitchBendB;

    if (setup)
    {
        gateA = false;
        gateB = false;
    }

    static uint8_t cvNoteA = 0xFF, cvNoteB = 0xFF;
    static int16_t cvBendA = 0x7FFF, cvBendB = 0x7FFF;
    if (noteA != cvNoteA || noteB != cvNoteB || bendA != cvBendA ||
        bendB != cvBendB)
    {
        cvNoteA = noteA;
        cvNoteB = noteB;
        cvBendA = bendA;
        cvBendB = bendB;
        writeMidiCv(noteA, bendA, noteB, bendB, bendRange);
    }
    PulseOut1(gateA);
    PulseOut2(gateB);

    // Soft bypass: A=D=R=0, S=127 → clickless gate (same as 0.8.0).
    // Factory maps Attack→X / Release→Y, so ADC drift can re-enable ADSR
    // while a note is held. Keep envelope state coherent in the clickless
    // path so that transition does not restart Attack from envLevel=0.
    const bool useAdsr =
        kAdsrEnabled &&
        !((g_ext.attack | g_ext.decay | g_ext.releaseAmp) == 0 &&
          g_ext.sustain >= 127);

    static bool wasAdsr = false;
    if (useAdsr && !wasAdsr)
    {
        for (int i = 0; i < kPolyMax; ++i)
        {
            if (!g_poly[i].sounding)
                continue;
            if (g_poly[i].gated)
            {
                g_poly[i].envStage = 3; // sustain
                g_poly[i].envLevel = envSustainLevel();
                if (g_poly[i].envLevel < 1)
                    g_poly[i].envLevel = 65535;
            }
            else
            {
                g_poly[i].envStage = 4; // release from current audible level
                g_poly[i].envLevel = 65535;
            }
        }
    }
    wasAdsr = useAdsr;

    int32_t outA = 0;
    int32_t outB = 0;
    if (!setup)
    {
        int32_t mixL = 0;
        int32_t mixR = 0;
        const VoiceMatrixCoord vm = decodeVoiceMatrix(g_ext.audioVoice);
        // Render unlocked — MIDI briefly locks only while mutating voice
        // alloc; spinning here would stall the ADC mux.
        for (int i = 0; i < kPolyMax; ++i)
        {
            if (!g_poly[i].sounding)
                continue;

            uint32_t env = 65535;
            if (useAdsr)
            {
                env = envTick(g_poly[i].envStage, g_poly[i].envLevel,
                              g_poly[i].gated);
                if (g_poly[i].envStage == 0)
                {
                    g_poly[i].sounding = false;
                    g_poly[i].amp = 0;
                    g_poly[i].envLevel = 0;
                    continue;
                }
                // Keep clickless fade-in even when attack=0.
                if (g_poly[i].amp < 4096)
                {
                    g_poly[i].amp += 128;
                    if (g_poly[i].amp > 4096)
                        g_poly[i].amp = 4096;
                }
            }
            else
            {
                // Mirror clickless audio into ADSR state so a later enable
                // does not treat the voice as "attack from zero".
                if (g_poly[i].gated)
                {
                    g_poly[i].envStage = 3;
                    g_poly[i].envLevel = 65535;
                    if (g_poly[i].amp < 4096)
                    {
                        g_poly[i].amp += 128;
                        if (g_poly[i].amp > 4096)
                            g_poly[i].amp = 4096;
                    }
                }
                else
                {
                    g_poly[i].envStage = 4;
                    g_poly[i].amp -= 64;
                    if (g_poly[i].amp <= 0)
                    {
                        g_poly[i].amp = 0;
                        g_poly[i].sounding = false;
                        g_poly[i].envStage = 0;
                        g_poly[i].envLevel = 0;
                        continue;
                    }
                }
            }

            int16_t bend = g_poly[i].bendIsB ? bendB : bendA;
            uint32_t inc =
                noteBendIncrement(g_poly[i].note, bend, bendRange);
            int32_t sL = 0, sR = 0;
            renderVoiceMatrix(g_poly[i], inc, vm.row, vm.col, sL, sR);
            int32_t voiceL =
                ((sL * (int32_t)env) >> 16) * g_poly[i].amp >> 12;
            int32_t voiceR =
                ((sR * (int32_t)env) >> 16) * g_poly[i].amp >> 12;
            mixL += voiceL >> 2;
            mixR += voiceR >> 2;
        }
        drumsRenderMix(mixL, mixR);

        if (mixL > 2047)
            mixL = 2047;
        if (mixL < -2048)
            mixL = -2048;
        if (mixR > 2047)
            mixR = 2047;
        if (mixR < -2048)
            mixR = -2048;
        outA = mixL;
        outB = mixR;
    }

    AudioOut1(outA);
    AudioOut2(outB);

    if (modeFlash_ > 0)
        --modeFlash_;

    if (g_glyph.active)
        g_glyph.tick();

    // Activity / save-flash timers must advance every sample; LED PWM writes
    // are subsampled below (~1 kHz) with shadow state to skip redundant calls.
    const bool activityLed = (g_config.flags & 1) != 0;
    if (modeFlash_ == 0 && !g_glyph.active && !setup)
    {
        if (activityLed && g_midiActivity)
        {
            midiActivityTimer_ = 2400;
            g_midiActivity = false;
        }
        if (midiActivityTimer_ > 0)
            --midiActivityTimer_;
        if (g_configSavedFlashTimer > 0)
            --g_configSavedFlashTimer;
    }

    if (++ledPhase_ >= kLedSubsample)
    {
        ledPhase_ = 0;

        if (modeFlash_ > 0)
        {
            if (modeFlashKind_ == 1)
            {
                bool on = ((modeFlash_ / 900) & 1) != 0;
                for (int i = 0; i < 6; ++i)
                    setLedIfChanged(i, on);
            }
            else
            {
                int step = (int)((9600 - modeFlash_) / 1400);
                for (int i = 0; i < 6; ++i)
                    setLedIfChanged(i, i == step);
            }
        }
        else if (g_glyph.active)
        {
            for (int i = 0; i < 6; ++i)
                setLedIfChanged(i, (g_glyph.ledMask & (1u << i)) != 0);
        }
        else if (setup)
        {
            uint8_t slot = g_setupSlot;
            bool blink = (sampleCount_ & 0x2000) != 0;
            setLedIfChanged(0, (slot & 1) != 0);
            setLedIfChanged(1, (slot & 2) != 0);
            setLedIfChanged(2, (slot & 4) != 0);
            setLedIfChanged(3, (slot & 8) != 0);
            setLedIfChanged(4, blink);
            setLedIfChanged(5, !blink);
        }
        else
        {
            if (!g_cvOutsCalibrated)
                setLedIfChanged(0, (sampleCount_ & 0x8000) != 0);
            else if (g_powerState == Unsupported)
                setLedIfChanged(0, (sampleCount_ & 0x4000) != 0);
            else if (g_isUsbHost)
                setLedIfChanged(0, g_midiConnected);
            else
                setLedIfChanged(0, (sampleCount_ & 0x2000) != 0);

            setLedIfChanged(1, midiActivityTimer_ > 0);
            setLedIfChanged(2, gateA);
            setLedIfChanged(3, gateB);
            setLedIfChanged(4, !g_isUsbHost && g_webLinked);
            setLedIfChanged(5, g_configSavedFlashTimer > 0);
        }
    }

    g_processSampleMeter.endSample();
    ++sampleCount_;
}
