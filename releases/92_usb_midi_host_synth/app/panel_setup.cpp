#include "card.h"

#include "config_store.h"
#include "glyph_leds.h"
#include "param_maps.h"
#include "runtime_state.h"
#include "voices.h"

bool UsbMidiHostCard::consumeFactoryResetLatch()
{
    if (!factoryResetLatch_)
        return false;
    factoryResetLatch_ = false;
    return true;
}

void UsbMidiHostCard::serviceSetupControls()
{
    // g_panelSwitch is sampled once per ProcessSample() before this runs.
    Switch sw = (Switch)g_panelSwitch;

    // Hold Z Down ~1 s toggles SETUP ↔ PLAY. Must release before the next
    // toggle (edge-armed). Releasing Down often passes through Middle on
    // the hardware switch — that must NOT count as "Middle = save/exit".
    if (sw == Down)
    {
        if (lastSwitch_ != Down)
        {
            zHold_ = 0;
            zToggleArmed_ = true;
        }
        if (zHold_ < 60000)
            ++zHold_;
        if (zToggleArmed_ && zHold_ == 48000 && bootSamples_ >= 4800)
        {
            zToggleArmed_ = false; // one toggle per press
            if (g_appMode == (uint8_t)AppMode::Play)
            {
                silenceAllVoices();
                g_appMode = (uint8_t)AppMode::Setup;
                modeFlashKind_ = 1;
                modeFlash_ = 7200;
                g_setupSlotPending = g_setupSlot;
                g_setupSlotDwell = 0;
                g_glyph.playNumber(g_setupSlot);
                armKnobLearn();
            }
            else
            {
                // Exit without saving (same as before).
                forcePlayModeCleanup();
                modeFlashKind_ = 2;
                modeFlash_ = 9600;
            }
        }
    }
    else if (sw != Down)
        zHold_ = 0;

    // Save + exit: deliberate move to Middle from Up only (not Down→Middle).
    if (g_appMode == (uint8_t)AppMode::Setup && sw == Middle &&
        lastSwitch_ == Up)
    {
        requestSaveToFlash(kCmdSaveFlash);
        forcePlayModeCleanup();
        modeFlashKind_ = 2;
        modeFlash_ = 9600;
        g_glyph.playNumber(0);
    }
    lastSwitch_ = sw;

    if (g_appMode == (uint8_t)AppMode::Setup)
    {
        int32_t main = g_panelMain;
        uint8_t raw =
            (uint8_t)(((uint32_t)main * (uint32_t)kNumSlots) / 4096u);
        if (raw >= kNumSlots)
            raw = kNumSlots - 1;
        if (raw != g_setupSlot)
        {
            if (raw != g_setupSlotPending)
            {
                g_setupSlotPending = raw;
                g_setupSlotDwell = 0;
            }
            else if (++g_setupSlotDwell >= 2400)
            {
                g_setupSlot = raw;
                g_setupSlotDwell = 0;
                g_glyph.playNumber(raw);
                armKnobLearn();
            }
        }
        else
        {
            g_setupSlotPending = raw;
            g_setupSlotDwell = 0;
        }

        serviceKnobLearnGesture();
    }
}
