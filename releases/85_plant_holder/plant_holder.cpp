/*
 * PlantHolder — Computer Card
 *
 * A nod to the Pocket Scion's roots in plant biosonification and to the
 * Fairfield Circuitry Placeholder's Householder-reflection reverb topology.
 *
 * Connects a Workshop System Computer to an Instruo Pocket Scion via USB
 * MIDI host.
 *
 * Features:
 *  - USB MIDI host: receives Note On/Off and Pitch Bend from the Scion
 *  - 2-channel monophonic CV/Gate (v/Oct + gate) on MIDI channels 1 and 2
 *    (configure routing in the Scion companion app)
 *  - Stereo reverb: Placeholder (EB) feedback delay network
 *  - CV In 1 → DECAY, CV In 2 → SIZE (pedal D / S jacks)
 *
 * Control pages (Placeholder EB panel mapping):
 *  Switch Up   : MIX (main), SIZE (x), RATIO (y)
 *  Switch Mid  : MIX (main), DECAY (x), TONE (y)
 *  Switch Down hold > 1 s (latch, LED 5): wet HI-CUT (main), MOD DEPTH (x),
 *                MOD TYPE (y) — toggle again to exit
 *
 * Hardware mapping:
 *  Audio In 1/2  : Scion left/right audio output
 *  Audio Out 1/2 : Processed reverb audio
 *  CV Out 1/2    : v/Oct + gate — MIDI channels 1 and 2
 *  CV In 1       : DECAY CV
 *  CV In 2       : SIZE CV
 *
 * Board requirement: Rev1_1 or newer (required for automatic USB host mode).
 * On older boards, USB host mode is not available and LED 0 will indicate
 * this limitation.
 */

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "placeholder_reverb.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define MIDI_NOTE_OFF       0x80
#define MIDI_NOTE_ON        0x90
#define MIDI_CONTROLCHANGE  0xB0
#define MIDI_PITCHBEND      0xE0

// Channel-mode controllers that must clear a stuck gate
static constexpr uint8_t MIDI_CC_ALL_SOUND_OFF = 120;
static constexpr uint8_t MIDI_CC_ALL_NOTES_OFF = 123;

// Latch MOD page: hold Down > 1 s toggles (48 kHz)
static constexpr uint32_t MOD_LATCH_HOLD_SAMPLES = 48000;

// CV/Gate outputs: MIDI channel 1 → CV A, MIDI channel 2 → CV B
static constexpr uint8_t kMidiChannelA = 0;
static constexpr uint8_t kMidiChannelB = 1;

// Pitch-bend range: ±2 semitones.
// In millivolts: 1 semitone = 1000/12 mV ≈ 83 mV.
// Full bend (±8192 MIDI units) → ±166 mV.
// Scale factor: 166 / 8192 ≈ (83 * 2) / 8192
static constexpr int32_t BEND_MV_PER_UNIT_NUM = 166;
static constexpr int32_t BEND_MV_PER_UNIT_DEN = 8192;

// CV In smoothing: IIR coefficient (127/128) → ~60 Hz one-pole at 48 kHz.
// The state settles at 16x the input; consumers shift back by 4.
static constexpr int32_t CV_SMOOTH_COEFF = 127;

// Reverb SIZE range, in samples, and the top of the DECAY range
static constexpr int32_t TIME_SPAN = PlaceholderReverb::TIME_MAX
                                   - PlaceholderReverb::TIME_MIN;
static constexpr int32_t DECAY_MAX = PlaceholderReverb::DECAY_MAX;

// Knob → DSP smoothing (~3 ms at 48 kHz) to avoid zipper when a knob catches
static constexpr int PARAM_SMOOTH_SHIFT = 6;

// Knob pickup: ignore X/Y/Main until the physical knob nears the stored value
struct KnobPickup
{
    int32_t value     = 0;
    bool    picked_up = false;
    static constexpr int32_t THRESHOLD = 80;

    int32_t update(int32_t knob_val)
    {
        if (picked_up)
        {
            value = knob_val;
        }
        else
        {
            int32_t diff = knob_val - value;
            if (diff < 0)
                diff = -diff;
            if (diff < THRESHOLD)
            {
                picked_up = true;
                value     = knob_val;
            }
        }
        return value;
    }

    void release() { picked_up = false; }
};

enum class CtrlPage : uint8_t
{
    Up,
    Mid,
    Mod,
};

// ---------------------------------------------------------------------------
// Shared volatile state (written by Core 1 USB task, read by Core 0 audio)
// ---------------------------------------------------------------------------
static volatile uint8_t  g_noteNumA       = 60;
static volatile uint8_t  g_noteNumB       = 60;
static volatile bool     g_gateA          = false;
static volatile bool     g_gateB          = false;
static volatile int16_t  g_pitchBendA     = 0;   // –8192 .. +8191
static volatile int16_t  g_pitchBendB     = 0;
static volatile bool     g_midiConnected  = false;
static volatile bool     g_midiActivity   = false; // set by Core 1, cleared by Core 0

// USB device address (used by mount/rx callbacks and USBCore)
static uint8_t g_midiDevAddr = 0;

// ---------------------------------------------------------------------------
// PlantHolder class
// ---------------------------------------------------------------------------
class PlantHolder : public ComputerCard
{
public:
    PlantHolder()
    {
        switchHoldTimer_   = 0;
        sampleCount_       = 0;
        midiActivityTimer_ = 0;
        cv1Smoothed_       = 0;
        cv2Smoothed_       = 0;
        modPageLatched_   = false;
        modHoldArmed_     = false;
        sizeSamples_      = 4800;
        ratio_            = 0;
        decay_            = 12000;
        tone_             = 0;
        modDepth_         = 512;
        modTypeKnob_      = 2048;
        hiCutKnob_        = 4095;
        mixLevel_         = 2048;
        ctrlPage_           = CtrlPage::Up;
        modPageLatchedPrev_ = false;
        controlsPrimed_     = false;
        smoothTime_       = sizeSamples_;
        smoothDecay_      = decay_;
        smoothTone_       = tone_;
        smoothRatio_      = ratio_;

        {
            knobSize_.value     = TIME_SPAN > 0
                ? ((sizeSamples_ - PlaceholderReverb::TIME_MIN) << 12) / TIME_SPAN
                : 2048;
            knobRatio_.value    = (ratio_ + 4096) >> 1;
            knobDecay_.value    = (decay_ * 4096) / DECAY_MAX;
            knobTone_.value     = (tone_ + 4096) >> 1;
            knobMix_.value      = mixLevel_;
            knobModDepth_.value = modDepth_;
            knobModType_.value  = modTypeKnob_;
            knobHiCut_.value    = hiCutKnob_;
        }

        usbHostSupported_ = false;
    }

    // -------------------------------------------------------------------------
    // Start the USB MIDI host on Core 1.
    //
    // Call this from main() *after* set_sys_clock_khz(): the card object is a
    // global, so its constructor runs during static init, and launching Core 1
    // there would leave TinyUSB running across the system-clock change.
    //
    // USB host mode needs a Rev1_1 board (Q2 2025+); on anything older there is
    // nothing to talk to, so Core 1 stays parked and LED 0 blinks instead.
    // -------------------------------------------------------------------------
    bool startUsbHost()
    {
        usbHostSupported_ = (HardwareVersion() == Rev1_1);
        if (!usbHostSupported_)
            return false;

        multicore_launch_core1(core1Entry);
        return true;
    }

    // --------------------------------------------------------------------------
    // Boilerplate for Core 1 entry
    // --------------------------------------------------------------------------
    static void core1Entry()
    {
        // Wait 150 ms for USB power state to settle before initialising TinyUSB
        sleep_us(150000);
        board_init();
        tuh_init(TUH_OPT_RHPORT);

        while (true)
        {
            tuh_task();
        }
    }

protected:
    // -------------------------------------------------------------------------
    // 48 kHz audio processing — runs on Core 0 under DMA interrupt
    // -------------------------------------------------------------------------
    virtual void __not_in_flash_func(ProcessSample)() override
    {
        ++sampleCount_;

        // -----------------------------------------------------------------
        // 1. Read audio inputs from Scion
        // -----------------------------------------------------------------
        int32_t inL = AudioIn1();
        int32_t inR = AudioIn2();

        // -----------------------------------------------------------------
        // 2. Smooth CV inputs (one-pole IIR, ~60 Hz at 48 kHz)
        // -----------------------------------------------------------------
        cv1Smoothed_ = (CV_SMOOTH_COEFF * cv1Smoothed_ + 16 * CVIn1()) >> 7;
        cv2Smoothed_ = (CV_SMOOTH_COEFF * cv2Smoothed_ + 16 * CVIn2()) >> 7;

        // The IIR above settles at 16x its input, so shift back down to
        // recover CVIn's native +/-2048 range before scaling anything by it.
        const int32_t cv1 = cv1Smoothed_ >> 4;
        const int32_t cv2 = cv2Smoothed_ >> 4;

        // -----------------------------------------------------------------
        // 3. Knobs → FX parameters (page depends on switch / MOD latch)
        // -----------------------------------------------------------------
        Switch sw = SwitchVal();
        primeControlsIfNeeded(sw);
        updateCtrlPagePickups(sw);

        if (modPageLatched_)
        {
            // Main carries the wet hi-cut: like MIX, it is an output-section
            // control, so it belongs on the big knob. X and Y are the pedal's
            // two MOD switches.
            hiCutKnob_   = knobHiCut_.update(KnobVal(Main));
            modDepth_    = knobModDepth_.update(KnobVal(X));
            modTypeKnob_ = knobModType_.update(KnobVal(Y));
        }
        else
        {
            mixLevel_ = knobMix_.update(KnobVal(Main));
            if (sw == Switch::Up)
            {
                int32_t xKnob = knobSize_.update(KnobVal(X));
                int32_t yKnob = knobRatio_.update(KnobVal(Y));
                sizeSamples_ = PlaceholderReverb::TIME_MIN
                             + ((xKnob * TIME_SPAN) >> 12);
                ratio_ = (yKnob * 2) - 4096;
            }
            else
            {
                int32_t xKnob = knobDecay_.update(KnobVal(X));
                int32_t yKnob = knobTone_.update(KnobVal(Y));
                decay_ = (xKnob * DECAY_MAX) >> 12;
                tone_  = toneFromKnob(yKnob);
            }
        }

        int32_t wetLevel = mixLevel_;
        int32_t dryLevel = 4095 - mixLevel_;

        // CV In 2 → SIZE (bipolar trim; full-scale CV covers the whole span)
        int32_t timeSamples = sizeSamples_ + ((cv2 * TIME_SPAN) >> 11);
        if (timeSamples < PlaceholderReverb::TIME_MIN)
            timeSamples = PlaceholderReverb::TIME_MIN;
        if (timeSamples > PlaceholderReverb::TIME_MAX)
            timeSamples = PlaceholderReverb::TIME_MAX;

        // CV In 1 → DECAY (bipolar trim, pedal D useful range ±5 V)
        int32_t decayLevel = decay_ + ((cv1 * DECAY_MAX) >> 11);
        if (decayLevel < 0) decayLevel = 0;
        if (decayLevel > DECAY_MAX) decayLevel = DECAY_MAX;

        // -----------------------------------------------------------------
        // 4. Placeholder EB feedback delay network (smoothed targets)
        // -----------------------------------------------------------------
        smoothToward(smoothTime_, timeSamples);
        smoothToward(smoothDecay_, decayLevel);
        smoothToward(smoothTone_, tone_);
        smoothToward(smoothRatio_, ratio_);

        reverb_.setTime(smoothTime_);
        reverb_.setRatio(smoothRatio_);
        reverb_.setFeedback(smoothDecay_);
        reverb_.setTone(smoothTone_);
        reverb_.setModDepth(modDepth_);
        reverb_.setModType(modTypeKnob_);
        reverb_.setHiCut(hiCutKnob_);

        int32_t wetL, wetR;
        reverb_.process(inL, inR, wetL, wetR);

        // Final dry/wet mix (Q12 scale factors)
        int32_t outL = ((inL * dryLevel) + (wetL * wetLevel)) >> 12;
        int32_t outR = ((inR * dryLevel) + (wetR * wetLevel)) >> 12;

        AudioOut1(clampAudio(outL));
        AudioOut2(clampAudio(outR));

        // -----------------------------------------------------------------
        // 5. CV / Gate outputs from MIDI state
        // -----------------------------------------------------------------
        {
            // Convert MIDI note + pitch bend to millivolts (1V/oct, A4=0V)
            // millivolts = (noteNum - 69) * 1000/12 + bend_mV
            int32_t mvA = ((int32_t)(g_noteNumA - 69) * 1000) / 12
                          + ((int32_t)g_pitchBendA * BEND_MV_PER_UNIT_NUM)
                            / BEND_MV_PER_UNIT_DEN;
            int32_t mvB = ((int32_t)(g_noteNumB - 69) * 1000) / 12
                          + ((int32_t)g_pitchBendB * BEND_MV_PER_UNIT_NUM)
                            / BEND_MV_PER_UNIT_DEN;

            CVOut1Millivolts(mvA);
            CVOut2Millivolts(mvB);
            PulseOut1(g_gateA);
            PulseOut2(g_gateB);
        }

        // -----------------------------------------------------------------
        // 6. MOD page latch (hold switch Down > 1 s)
        // -----------------------------------------------------------------
        updateModPageLatch(sw);

        // -----------------------------------------------------------------
        // 7. LEDs
        // -----------------------------------------------------------------
        updateLEDs();
    }

private:
    static int16_t __not_in_flash_func(clampAudio)(int32_t x)
    {
        if (x > 2047)
            return 2047;
        if (x < -2048)
            return -2048;
        return (int16_t)x;
    }

    static void __not_in_flash_func(smoothToward)(int32_t &state, int32_t target)
    {
        const int32_t diff = target - state;
        // An arithmetic shift truncates towards -inf, so a rising target would
        // otherwise stall up to (1 << PARAM_SMOOTH_SHIFT) - 1 counts short.
        if (diff < (1 << PARAM_SMOOTH_SHIFT) && diff > -(1 << PARAM_SMOOTH_SHIFT))
            state = target;
        else
            state += diff >> PARAM_SMOOTH_SHIFT;
    }

    CtrlPage __not_in_flash_func(ctrlPageFor)(Switch sw) const
    {
        if (modPageLatched_)
            return CtrlPage::Mod;
        if (sw == Switch::Up)
            return CtrlPage::Up;
        return CtrlPage::Mid;
    }

    static int32_t __not_in_flash_func(toneFromKnob)(int32_t yKnob)
    {
        int32_t t = (yKnob * 2) - 4096;
        // Noon detent — ignore ADC wander around centre
        if (t > -200 && t < 200)
            t = 0;
        return t;
    }

    void __not_in_flash_func(primeControlsIfNeeded)(Switch sw)
    {
        if (controlsPrimed_)
            return;
        controlsPrimed_ = true;
        ctrlPage_       = ctrlPageFor(sw);
        releasePickupsFor(ctrlPage_);
        // MIX has no stored value to catch up to at boot, so let it track the
        // physical knob straight away instead of waiting for a pickup.
        knobMix_.release();
        reverb_.resetToneFilter();
    }

    void __not_in_flash_func(releasePickupsFor)(CtrlPage page)
    {
        switch (page)
        {
        case CtrlPage::Up:
            knobSize_.release();
            knobRatio_.release();
            break;
        case CtrlPage::Mid:
            knobDecay_.release();
            knobTone_.release();
            reverb_.resetToneFilter();
            break;
        case CtrlPage::Mod:
            knobModDepth_.release();
            knobModType_.release();
            knobHiCut_.release();
            break;
        }
    }

    void __not_in_flash_func(updateCtrlPagePickups)(Switch sw)
    {
        const CtrlPage page = ctrlPageFor(sw);

        if (modPageLatched_ != modPageLatchedPrev_)
        {
            releasePickupsFor(page);
            knobMix_.release();
            modPageLatchedPrev_ = modPageLatched_;
            ctrlPage_           = page;
            return;
        }

        if (page != ctrlPage_)
        {
            releasePickupsFor(page);
            if (page == CtrlPage::Mod || ctrlPage_ == CtrlPage::Mod)
                knobMix_.release();
            ctrlPage_ = page;
        }
    }

    void __not_in_flash_func(updateModPageLatch)(Switch sw)
    {
        if (sw == Switch::Down)
        {
            ++switchHoldTimer_;
            if (switchHoldTimer_ >= MOD_LATCH_HOLD_SAMPLES && !modHoldArmed_)
            {
                modPageLatched_ = !modPageLatched_;
                modHoldArmed_   = true;
            }
        }
        else
        {
            switchHoldTimer_ = 0;
            modHoldArmed_    = false;
        }
    }

    // -------------------------------------------------------------------------
    // LED indicators
    // -------------------------------------------------------------------------
    void __not_in_flash_func(updateLEDs)()
    {
        // Decrement activity timer
        if (midiActivityTimer_ > 0) --midiActivityTimer_;

        // Latch activity signal from Core 1
        if (g_midiActivity)
        {
            g_midiActivity    = false;
            midiActivityTimer_ = 3000;
        }

        // LED 0: solid = Scion mounted. On a pre-Rev1_1 board there is no USB
        // host at all, so blink at ~1.3 Hz to distinguish "unsupported board"
        // from "nothing plugged in".
        if (usbHostSupported_)
            LedOn(0, g_midiConnected);
        else
            LedOn(0, (sampleCount_ & 0x4000) != 0);

        LedOn(1, midiActivityTimer_ > 0);
        LedOn(2, g_gateA);
        LedOn(3, g_gateB);
        LedOn(4, false);   // unused since the clock bridge was removed
        LedOn(5, modPageLatched_);
    }

    // -------------------------------------------------------------------------
    // Member data
    // -------------------------------------------------------------------------
    PlaceholderReverb reverb_;

    uint32_t switchHoldTimer_;

    uint32_t sampleCount_;

    uint32_t midiActivityTimer_;
    int32_t  cv1Smoothed_;
    int32_t  cv2Smoothed_;

    bool     modPageLatched_;
    bool     modHoldArmed_;
    bool     usbHostSupported_;

    int32_t  sizeSamples_;
    int32_t  ratio_;
    int32_t  decay_;
    int32_t  tone_;
    int32_t  modDepth_;
    int32_t  modTypeKnob_;
    int32_t  hiCutKnob_;
    int32_t  mixLevel_;

    CtrlPage ctrlPage_;
    bool     modPageLatchedPrev_;
    bool     controlsPrimed_;

    int32_t  smoothTime_;
    int32_t  smoothDecay_;
    int32_t  smoothTone_;
    int32_t  smoothRatio_;

    KnobPickup knobMix_;
    KnobPickup knobSize_;
    KnobPickup knobRatio_;
    KnobPickup knobDecay_;
    KnobPickup knobTone_;
    KnobPickup knobModDepth_;
    KnobPickup knobModType_;
    KnobPickup knobHiCut_;
};

// ---------------------------------------------------------------------------
// Monophonic last-note-priority voice.
//
// Held keys are tracked so that a note-off for a key that is no longer the
// sounding one does not drop the gate (play C, play D, release C — D keeps
// sounding), and so releasing the newest key falls back to one still held.
// Lives on Core 1 only; it publishes to the volatile globals above.
// ---------------------------------------------------------------------------
struct MonoVoice
{
    static constexpr int STACK_MAX = 8;

    uint8_t held[STACK_MAX] = {0};
    int     count           = 0;

    void noteOn(uint8_t note)
    {
        release(note);
        if (count == STACK_MAX)   // stack full: forget the oldest key
        {
            for (int i = 1; i < STACK_MAX; ++i)
                held[i - 1] = held[i];
            --count;
        }
        held[count++] = note;
    }

    void noteOff(uint8_t note) { release(note); }
    void allOff()              { count = 0; }

    bool    gate() const { return count > 0; }
    uint8_t note() const { return count > 0 ? held[count - 1] : 0; }

private:
    void release(uint8_t note)
    {
        for (int i = 0; i < count; ++i)
        {
            if (held[i] != note)
                continue;
            for (int j = i + 1; j < count; ++j)
                held[j - 1] = held[j];
            --count;
            return;
        }
    }
};

static MonoVoice g_voiceA;
static MonoVoice g_voiceB;

// ---------------------------------------------------------------------------
// USB MIDI host callbacks (must be at C linkage / global scope)
// ---------------------------------------------------------------------------

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx)
{
    (void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;
    if (g_midiDevAddr == 0)
    {
        g_midiDevAddr    = dev_addr;
        g_midiConnected  = true;
    }
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;
    if (dev_addr == g_midiDevAddr)
    {
        g_midiDevAddr   = 0;
        g_midiConnected = false;
        g_voiceA.allOff();
        g_voiceB.allOff();
        g_gateA = g_gateB = false;
    }
}

// ---------------------------------------------------------------------------
// MIDI byte parser — called from tuh_midi_rx_cb on Core 1
// ---------------------------------------------------------------------------
static void parseMidiByte(uint8_t b)
{
    static uint8_t runningStatus = 0;
    static uint8_t buf[3];
    static int     bufPos = 0;

    if (b & 0x80)
    {
        // Real-time messages (0xF8–0xFF) are single-byte; ignore here
        if (b >= 0xF8) return;
        // System common — reset running status
        if (b >= 0xF0) { runningStatus = 0; bufPos = 0; return; }

        // New status byte
        runningStatus = b;
        buf[0]  = b;
        bufPos  = 1;
    }
    else
    {
        if (runningStatus == 0) return;

        if (bufPos == 0)
        {
            buf[0]  = runningStatus;
            bufPos  = 1;
        }
        buf[bufPos++] = b;

        // Determine expected message length
        uint8_t type = runningStatus & 0xF0;
        int expected = 0;
        if (type == 0x80 || type == 0x90 || type == 0xA0 ||
            type == 0xB0 || type == 0xE0)
        {
            expected = 3;
        }
        else if (type == 0xC0 || type == 0xD0)
        {
            expected = 2;
        }

        if (bufPos >= expected && expected > 0)
        {
            uint8_t chan = runningStatus & 0x0F;
            bool noteOn  = (type == MIDI_NOTE_ON)  && (buf[2] > 0);
            bool noteOff = (type == MIDI_NOTE_OFF) ||
                           (type == MIDI_NOTE_ON && buf[2] == 0);

            if (noteOn || noteOff)
            {
                MonoVoice        *voice = nullptr;
                volatile uint8_t *noteOut = nullptr;
                volatile bool    *gateOut = nullptr;

                if (chan == kMidiChannelA)
                {
                    voice = &g_voiceA; noteOut = &g_noteNumA; gateOut = &g_gateA;
                }
                else if (chan == kMidiChannelB)
                {
                    voice = &g_voiceB; noteOut = &g_noteNumB; gateOut = &g_gateB;
                }

                if (voice)
                {
                    if (noteOn) voice->noteOn(buf[1]);
                    else        voice->noteOff(buf[1]);

                    // Publish pitch before gate so Core 0 never sees a fresh
                    // gate paired with the previous note. On release the last
                    // pitch is held rather than reset.
                    if (voice->gate())
                        *noteOut = voice->note();
                    *gateOut = voice->gate();
                    g_midiActivity = true;
                }
            }
            else if (type == MIDI_CONTROLCHANGE)
            {
                // All Sound Off / All Notes Off. Without these, a note-off lost
                // to a USB hiccup leaves the gate stuck high with no recovery
                // short of unplugging the Scion.
                if (buf[1] == MIDI_CC_ALL_SOUND_OFF ||
                    buf[1] == MIDI_CC_ALL_NOTES_OFF)
                {
                    if (chan == kMidiChannelA) { g_voiceA.allOff(); g_gateA = false; }
                    if (chan == kMidiChannelB) { g_voiceB.allOff(); g_gateB = false; }
                }
            }
            else if (type == MIDI_PITCHBEND)
            {
                // 14-bit value centred at 8192
                int16_t bend = (int16_t)((buf[1] | ((uint16_t)buf[2] << 7)) - 8192);
                if (chan == kMidiChannelA) g_pitchBendA = bend;
                if (chan == kMidiChannelB) g_pitchBendB = bend;
            }

            // Ready for next message; keep running status
            bufPos = 1;
        }
    }
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != g_midiDevAddr || num_packets == 0) return;

    uint8_t  cableNum;
    uint8_t  buffer[512];
    uint32_t bytesRead;

    while ((bytesRead = tuh_midi_stream_read(dev_addr, &cableNum,
                                             buffer, sizeof(buffer))) > 0)
    {
        for (uint32_t i = 0; i < bytesRead; ++i)
            parseMidiByte(buffer[i]);
    }
}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

// ---------------------------------------------------------------------------
// Global PlantHolder instance (placed in BSS so reverb buffers fit in RAM)
// ---------------------------------------------------------------------------
static PlantHolder g_card;

int main()
{
    // 144 MHz: exact multiple of 48 kHz Nyquist frequency → alias-free ADC
    set_sys_clock_khz(144000, true);

    // Core 1 must not be running across the clock change above.
    g_card.startUsbHost();

    g_card.Run();
}
