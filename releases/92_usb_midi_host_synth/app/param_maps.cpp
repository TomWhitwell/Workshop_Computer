#include "param_maps.h"

#include "adsr.h"
#include "config_store.h"
#include "drums.h"
#include "voice_matrix.h"
#include "glyph_leds.h"
#include "runtime_state.h"
#include "voices.h"

uint8_t g_learnKnobArmed = 0;
uint8_t g_setupSlotPending = 0;
uint32_t g_setupSlotDwell = 0;

namespace {

uint16_t g_learnKnobBaseX = 0;
uint16_t g_learnKnobBaseY = 0;
uint8_t g_knobMapXv = 0xFF;
uint8_t g_knobMapYv = 0xFF;
bool g_knobBaselineValid = false;
uint8_t g_knobLastXv = 0xFF;
uint8_t g_knobLastYv = 0xFF;

} // namespace

void resetKnobMappedBaseline()
{
    g_knobMapXv = 0xFF;
    g_knobMapYv = 0xFF;
    g_knobBaselineValid = false;
    g_knobLastXv = 0xFF;
    g_knobLastYv = 0xFF;
}

uint8_t mapCcVoice(uint8_t v)
{
    return mapCcToVoiceId(v);
}

bool slotMatches(const MapSlot &s, uint8_t chan, uint8_t type, uint8_t id)
{
    if (s.sourceType != type)
        return false;
    if (s.channel != kChanOmni && s.channel != chan)
        return false;
    return s.ccOrNote == id;
}

void applySlotValue(uint8_t slot, uint8_t value, bool fromLearn)
{
    switch (slot)
    {
    case kSlotVolume:
        break; // reserved — no master volume (use external mixer)
    case kSlotChA:
    {
        uint8_t ch = (uint8_t)(value & 0x0F);
        if (ch != g_config.channelA)
        {
            silenceAllVoices();
            g_config.channelA = ch;
        }
        if (!fromLearn)
            g_glyph.playNumber(g_config.channelA + 1);
        break;
    }
    case kSlotChB:
    {
        uint8_t ch = (uint8_t)(value & 0x0F);
        if (ch != g_config.channelB)
        {
            silenceAllVoices();
            g_config.channelB = ch;
        }
        if (!fromLearn)
            g_glyph.playNumber(g_config.channelB + 1);
        break;
    }
    case kSlotBend:
    {
        uint8_t b = (uint8_t)(1 + (value * 11) / 127);
        g_config.bendSemitones = b;
        if (!fromLearn)
            g_glyph.playNumber(b);
        break;
    }
    case kSlotVoice:
    {
        uint8_t v = mapCcVoice(value);
        if (v != g_ext.audioVoice)
        {
            g_ext.audioVoice = v;
            if (!fromLearn)
                g_glyph.playNumber(v);
        }
        break;
    }
    case kSlotReserved5:
    case kSlotReserved6:
        break; // legacy arp/reverb slots — ignored
    case kSlotAttack:
        g_ext.attack = value;
        break;
    case kSlotDecay:
        g_ext.decay = value;
        break;
    case kSlotSustain:
        g_ext.sustain = value;
        updateEnvSusLevel(value);
        break;
    case kSlotRelease:
        g_ext.releaseAmp = value;
        break;
    case kSlotCutoff:
        g_ext.cutoff = value;
        break;
    case kSlotPwm:
        g_ext.pwmWidth = value;
        break;
    default:
        break;
    }
}

void learnToSlot(uint8_t slot, uint8_t srcType, uint8_t chan, uint8_t id)
{
    if (slot >= kNumSlots)
        return;
    g_ext.slots[slot].sourceType = srcType;
    g_ext.slots[slot].channel = chan;
    g_ext.slots[slot].ccOrNote = id;
    g_glyph.playNumber(slot);
    g_midiActivity = true;

    // Notify the web editor (device mode) so Setup Monitor can show the map.
    g_learnNotifySlot = slot;
    g_learnNotifySrc = srcType;
    g_learnNotifyChan = chan;
    g_learnNotifyId = id;
    g_learnNotifyPending = true;
}

void applyKnobMappedSlots()
{
    uint8_t xv = (uint8_t)(((uint32_t)g_panelX * 127u) / 4095u);
    uint8_t yv = (uint8_t)(((uint32_t)g_panelY * 127u) / 4095u);
    // Wider deadband — X/Y ADC noise was flipping Attack/Release enough to
    // leave the A=D=R=0 / S=127 ADSR kill-switch after long idle play.
    constexpr int kKnobDeadband = 3;

    // Capture knob position at boot / after config load without overwriting
    // factory ADSR defaults (A=D=R=0, S=127) until the user moves a knob.
    if (!g_knobBaselineValid)
    {
        g_knobMapXv = xv;
        g_knobMapYv = yv;
        g_knobBaselineValid = true;
        g_knobLastXv = xv;
        g_knobLastYv = yv;
        return;
    }

    if (g_knobMapXv == 0xFF)
        g_knobMapXv = xv;
    else
    {
        int d = (int)xv - (int)g_knobMapXv;
        if (d >= kKnobDeadband || d <= -kKnobDeadband)
            g_knobMapXv = xv;
        else
            xv = g_knobMapXv;
    }
    if (g_knobMapYv == 0xFF)
        g_knobMapYv = yv;
    else
    {
        int d = (int)yv - (int)g_knobMapYv;
        if (d >= kKnobDeadband || d <= -kKnobDeadband)
            g_knobMapYv = yv;
        else
            yv = g_knobMapYv;
    }

    const bool xChanged = (xv != g_knobLastXv);
    const bool yChanged = (yv != g_knobLastYv);
    if (!xChanged && !yChanged)
        return;
    g_knobLastXv = xv;
    g_knobLastYv = yv;

    for (uint8_t s = 0; s < kNumSlots; ++s)
    {
        uint8_t t = g_ext.slots[s].sourceType;
        if (t == kSrcKnobX && xChanged)
            applySlotValue(s, xv, true);
        else if (t == kSrcKnobY && yChanged)
            applySlotValue(s, yv, true);
    }
}

void armKnobLearn()
{
    g_learnKnobBaseX = g_panelX;
    g_learnKnobBaseY = g_panelY;
    g_learnKnobArmed = 1;
}

void serviceKnobLearnGesture()
{
    if (g_learnKnobArmed != 1)
        return;
    int dx = (int)g_panelX - (int)g_learnKnobBaseX;
    int dy = (int)g_panelY - (int)g_learnKnobBaseY;
    if (dx > 250 || dx < -250)
    {
        learnToSlot(g_setupSlot, kSrcKnobX, 0, 0);
        g_learnKnobArmed = 2;
    }
    else if (dy > 250 || dy < -250)
    {
        learnToSlot(g_setupSlot, kSrcKnobY, 0, 0);
        g_learnKnobArmed = 2;
    }
}

void handleChannelMessage(const uint8_t *buf)
{
    uint8_t type = buf[0] & 0xF0;
    uint8_t chan = buf[0] & 0x0F;
    bool inSetup = (g_appMode == (uint8_t)AppMode::Setup);

    bool noteOn = (type == MIDI_NOTE_ON) && (buf[2] > 0);
    bool noteOff =
        (type == MIDI_NOTE_OFF) || (type == MIDI_NOTE_ON && buf[2] == 0);

    if (inSetup && noteOn)
    {
        learnToSlot(g_setupSlot, kSrcNote, chan, buf[1]);
        applySlotValue(g_setupSlot, buf[1], true);
        return;
    }

    if (noteOn || noteOff)
    {
        // Ch10 pads 36–43 → drum kit (always; not voice A/B melodic poly).
        if (!inSetup && isDrumPadNote(chan, buf[1]))
        {
            critical_section_enter_blocking(&g_midiCs);
            if (noteOn)
                drumNoteOn(buf[1], buf[2]);
            else
                drumNoteOff(buf[1]);
            critical_section_exit(&g_midiCs);
            g_midiActivity = true;
            return;
        }

        if (noteOn && !inSetup)
        {
            for (uint8_t s = 0; s < kNumSlots; ++s)
            {
                if (slotMatches(g_ext.slots[s], chan, kSrcNote, buf[1]))
                    applySlotValue(s, buf[1], false);
            }
        }

        MonoVoice *voice = nullptr;
        volatile uint8_t *noteOut = nullptr;
        volatile bool *gateOut = nullptr;
        bool bendIsB = false;

        if (chan == g_config.channelA)
        {
            voice = &g_voiceA;
            noteOut = &g_noteNumA;
            gateOut = &g_gateA;
        }
        else if (chan == g_config.channelB)
        {
            voice = &g_voiceB;
            noteOut = &g_noteNumB;
            gateOut = &g_gateB;
            bendIsB = true;
        }

        if (noteOff && voice)
        {
            critical_section_enter_blocking(&g_midiCs);
            voice->noteOff(buf[1]);
            if (voice->gate())
                *noteOut = voice->note();
            *gateOut = voice->gate();
            polyNoteOffUnlocked(buf[1]);
            critical_section_exit(&g_midiCs);
            g_midiActivity = true;
            return;
        }

        if (inSetup)
            return;

        if (voice && noteOn)
        {
            critical_section_enter_blocking(&g_midiCs);
            voice->noteOn(buf[1]);
            if (voice->gate())
                *noteOut = voice->note();
            *gateOut = voice->gate();
            polyNoteOnUnlocked(buf[1], bendIsB);
            critical_section_exit(&g_midiCs);
            g_midiActivity = true;
        }
    }
    else if (type == MIDI_CC)
    {
        if (buf[1] == MIDI_CC_ALL_SOUND_OFF || buf[1] == MIDI_CC_ALL_NOTES_OFF)
        {
            critical_section_enter_blocking(&g_midiCs);
            if (chan == g_config.channelA)
            {
                g_voiceA.allOff();
                g_gateA = false;
            }
            if (chan == g_config.channelB)
            {
                g_voiceB.allOff();
                g_gateB = false;
            }
            polyAllOffUnlocked();
            if (chan == kDrumMidiChannel)
                drumsAllOff();
            critical_section_exit(&g_midiCs);
            g_midiActivity = true;
            return;
        }

        if (inSetup)
        {
            learnToSlot(g_setupSlot, kSrcCc, chan, buf[1]);
            applySlotValue(g_setupSlot, buf[2], true);
            return;
        }

        for (uint8_t s = 0; s < kNumSlots; ++s)
        {
            if (slotMatches(g_ext.slots[s], chan, kSrcCc, buf[1]))
                applySlotValue(s, buf[2], false);
        }
        g_midiActivity = true;
    }
    else if (type == MIDI_PITCHBEND)
    {
        if (inSetup)
            return;
        int16_t bend =
            (int16_t)((buf[1] | ((uint16_t)buf[2] << 7)) - 8192);
        if (chan == g_config.channelA)
            g_pitchBendA = bend;
        if (chan == g_config.channelB)
            g_pitchBendB = bend;
        g_midiActivity = true;
    }
}
