// MainApp.cpp
#include "MainApp.h"
#include <cstdio>
#include "pico/time.h" // temporary for testing
#include <inttypes.h>  // temporary for testing
#include "tusb.h"

// Config variables in HEX
const int CARD_NUMBER = 93;
const int MAJOR_VERSION = 0x01;
const int MINOR_VERSION = 0x05;
const int POINT_VERSION = 0x00;

MainApp::MainApp()

    // Initialise the Turing machines with variations of the memory card ID, unique but not random
    : turingDAC1(8, MemoryCardID()),
      turingDAC2(8, MemoryCardID() * 2),
      turingPWM1(8, MemoryCardID() * 3),
      turingPWM2(8, MemoryCardID() * 4),
      turingPulseLength1(8, MemoryCardID() * 5),
      turingPulseLength2(8, MemoryCardID() * 6)

{

    ui.init(this, &clk);
}

void MainApp::UpdateNotePools()
{
    // create note pools for PWM precision CV outputs
    bool p = 0;
    int base_note = 48; // C3
    int range = settings->preset[p].range;
    int scale = settings->preset[p].scale;
    turingPWM1.UpdateNotePool(base_note, range, scale);
    turingPWM2.UpdateNotePool(base_note, range, scale);
}

void MainApp::UpdatePulseLengths()
{

    bool p = 0;
    uint8_t lengthMode = settings->preset[p].length;

    switch (lengthMode)
    {
    case 0:
        ui.SetPulseLength(1);
        ui.SetPulseMod(0);
        break;
    case 1:
        ui.SetPulseLength(25);
        ui.SetPulseMod(0);
        break;
    case 2:
        ui.SetPulseLength(50);
        ui.SetPulseMod(0);
        break;
    case 3:
        ui.SetPulseLength(75);
        ui.SetPulseMod(0);
        break;
    case 4:
        ui.SetPulseLength(99);
        ui.SetPulseMod(0);
        break;
    case 5:
        ui.SetPulseLength(15);
        ui.SetPulseMod(12);
        break;
    case 6:
        ui.SetPulseLength(50);
        ui.SetPulseMod(30);
        break;
    default:
        // Optional: Handle out-of-range values for `setting`
        ui.SetPulseLength(1);
        ui.SetPulseMod(0);
        break;
    }
}

void MainApp::LoadSettings(bool reset)
{
    // Load or initialise config
    cfg.load(reset); // 1 = force reset
    settings = &cfg.get();
    CurrentBPM10 = settings->bpm; // load bpm from settings file NB bpm always 10x i.e 1200 = 120.0 bpm.
    clk.setBPM10(CurrentBPM10);
    settings->divide = 5;
    clk.UpdateDivide(5);

    UpdateNotePools();
    UpdatePulseLengths();
    UpdateCh2Lengths();
    UpdateCVRange();
    UpdateVactrolTiming();
}

void __not_in_flash_func(MainApp::ProcessSample)()
{

    // TEST_write_to_Pulse(0, true); // pulse to test on oscilloscope

    // Call tap before ui.tick and before clk.tick, so that reset triggered tap is tapped make it to ui.
    if (tapReceived())
    {
        uint32_t now = clk.GetTicks();
        uint16_t tempBPM = clk.TapTempo(now);
        if (tempBPM > 0 && newBPM10 == 0)
        {
            newBPM10 = tempBPM;
        }
    }
    if (extPulse1Received())
    {
        uint32_t now = clk.GetTicks();
        clk.TapTempo(now);
        clk.ExtPulse1();
    }

    if (extPulse2Received())
    {

        clk.ExtPulse2();
    }

    clk.Tick();

    ui.Tick();
    ProcessVactrolMix();

    // CVOut1((clk.GetPhase() >> 20) - 2048); // just for debugging, remove
    // CVOut2((clk.TEST_subclock_phase >> 20) - 2048);

    // blink(1, 50); // show that Core 1 is alive

    // TEST_write_to_Pulse(0, false); // oscilloscope test
}

void MainApp::Housekeeping()
{

    pollSysEx();

    // LedOn(2, pendingSave);
    uint64_t nowUs = time_us_64();

    // BPM changed?

    if (newBPM10 > 0 && newBPM10 < 8000 && newBPM10 != CurrentBPM10)
    {
        settings->bpm = newBPM10;
        CurrentBPM10 = newBPM10;
        newBPM10 = 0;

        lastChangeTimeUs = nowUs;
        pendingSave = true;
    }
    else if (newBPM10 > 0 && (newBPM10 >= 8000 || newBPM10 == CurrentBPM10))
    {
        // Clear invalid or duplicate BPM
        newBPM10 = 0;
    }

    // Has 2 seconds passed since last change, and save is pending?
    if (pendingSave && (nowUs - lastChangeTimeUs >= 2000000))
    {
        settings->divide = 5;
        cfg.save();
        pendingSave = false;
    }

    // blink(0, 250); // show that Core 0 is alive

    ui.SlowUI(); // call knob checking etc

    updateLedState();

    ui.UpdatePulseMod(turingPulseLength1.DAC_8(), turingPulseLength2.DAC_8());

    UpdatePulseLengths();

    // Check if external clocks have been unplugged
    if (clk.getExternalClock1() && !PulseInConnected1())
    {

        clk.setExternalClock1(false);
        CurrentBPM10 = settings->bpm;
        clk.setBPM10(CurrentBPM10);
    }

    if (!PulseInConnected2() && clk.getExternalClock2())
    {
        clk.setExternalClock2(false);
    }

    if (!VactrolLayerActive() && Connected(CV2))
    {
        midiOffset = CVtoMidiOffset(CVIn2());
    }
    else
    {
        midiOffset = 0;
    }

    sendViz = false;
}

void MainApp::pollSysEx()
{
    uint8_t packet[64];

    while (tud_midi_available())
    {
        size_t len = tud_midi_stream_read(packet, sizeof(packet));
        for (size_t i = 0; i < len; ++i)
        {
            const uint8_t byte = packet[i];

            if (!sysexActive)
            {
                if (byte == 0xF0)
                {
                    sysexActive = true;
                    sysexLength = 0;
                    sysexBuffer[sysexLength++] = byte;
                }
                continue;
            }

            if (sysexLength < kSysExBufferSize)
            {
                sysexBuffer[sysexLength++] = byte;
            }
            else
            {
                sysexActive = false;
                sysexLength = 0;
                continue;
            }

            if (byte == 0xF7)
            {
                handleSysExMessage(sysexBuffer, sysexLength);
                sysexActive = false;
                sysexLength = 0;
            }
        }
    }
}

void MainApp::PulseLed1(bool status)
{
    pulseLed1_status = status;
}

void MainApp::PulseLed2(bool status)
{

    pulseLed2_status = status;
}

bool MainApp::PulseOutput1(bool requested)
{
    bool emit = false;
    bool isTuringMode = settings->preset[0].pulseMode1;

    if (isTuringMode && requested)
    {
        emit = (turingPWM1.DAC_8() & 0x01);
    }
    else
    {
        emit = requested;
    }

    PulseOut1(emit);
    return emit;
}

bool MainApp::PulseOutput2(bool requested)
{
    bool emit = false;
    bool isTuringMode = settings->preset[0].pulseMode2;

    if (isTuringMode && requested)
    {
        emit = (turingPWM2.DAC_8() & 0x01);
    }
    else
    {
        emit = requested;
    }

    PulseOut2(emit);
    return emit;
}

bool MainApp::PulseInConnected1()
{
    return Connected(Pulse1);
}

bool MainApp::PulseInConnected2()
{
    return Connected(Pulse2);
}

bool(MainApp::tapReceived)()
{
    if (PulseInConnected1())
    {
        return false;
    }
    else
    {
        // clk.setExternalClock1(false); // Remove to check what happens
        return (SwitchChanged() && SwitchVal() == Down);
    }
}

bool MainApp::extPulse1Received()
{
    if (PulseInConnected1() && PulseIn1RisingEdge())
    {
        clk.setExternalClock1(true);
        return true;
    }
    else
    {
        return false;
    }
}

bool MainApp::extPulse2Received()
{
    if (PulseInConnected2() && PulseIn2RisingEdge())
    {

        clk.setExternalClock2(true);
        return true;
    }
    else
    {
        return false;
    }
}

bool MainApp::VactrolLayerActive()
{
    return SwitchVal() == Up;
}

uint16_t MainApp::KnobMain()
{
    return KnobVal(Main);
}
uint16_t MainApp::KnobX()
{
    return KnobVal(X);
}
uint16_t MainApp::KnobY()
{
    return KnobVal(Y);
}

bool MainApp::ModeSwitch()
{ // 1 = up 0 = middle (or down)
    return VactrolLayerActive();
}

bool MainApp::SwitchDown()
{
    return SwitchVal() == Down;
}

bool MainApp::switchChanged()
{
    // 1 = up 0 = middle (or down)
    bool result = false;
    bool newSwitch = ModeSwitch();
    if (newSwitch != oldSwitch)
    {
        result = true;
        oldSwitch = newSwitch;
    }
    return result;
}

void MainApp::SetTuringRandomness(uint16_t value)
{
    turingRandomness = value;
}

void MainApp::divideKnobChanged(uint8_t step)
{
    clk.UpdateDivide(step);
};

void MainApp::lengthKnobChanged(uint8_t length)
{

    bool p = 0;

    int lengthPlus = settings->preset[p].looplen - 1; // Because 1-1 = 0, 0-1 = -1

    turingDAC1.updateLength(length);
    turingDAC2.updateLength(length + lengthPlus);
    turingPWM1.updateLength(length);
    turingPWM2.updateLength(length + lengthPlus);
    turingPulseLength1.updateLength(length);
    turingPulseLength2.updateLength(length + lengthPlus);

    // This is where to place the LED animation for length changes
    showLengthPattern(length);
    UpdatePulseLengths();
}

void MainApp::UpdateCh2Lengths()
{
    bool p = 0;
    int lengthPlus = settings->preset[p].looplen - 1; // Because 1-1 = 0, 0-1 = -1
    uint16_t length = turingPWM1.returnLength();
    turingDAC2.updateLength(length + lengthPlus);
    turingPWM2.updateLength(length + lengthPlus);
    turingPulseLength2.updateLength(length + lengthPlus);
}

void MainApp::UpdateCVRange()
{
    bool p = 0;
    int cvRange = settings->preset[p].cvRange;
    cv_set_mode(cvRange);
}

void MainApp::updateMainTuring()
{

    // Update Turing Machines
    turingDAC1.Update(turingRandomness, maxRange);
    turingPWM1.Update(turingRandomness, maxRange);
    turingPulseLength1.Update(turingRandomness, maxRange);

    // Scaled CV out on CV/Audio 1
    uint8_t dac8 = turingDAC1.DAC_8();
    vactrolTargetBase1 = int32_t(dac8) << 4;

    int midi_note = turingPWM1.MidiNote() + midiOffset;
    CVOut1MIDINote(midi_note);
}

void MainApp::updateDivTuring()
{
    turingDAC2.Update(turingRandomness, maxRange);
    turingPWM2.Update(turingRandomness, maxRange);
    turingPulseLength2.Update(turingRandomness, maxRange);

    // Scaled CV out on CV/Audio 2
    uint8_t dac8 = turingDAC2.DAC_8();
    vactrolTargetBase2 = int32_t(dac8) << 4;

    int midi_note = turingPWM2.MidiNote() + midiOffset;
    CVOut2MIDINote(midi_note);
}

uint32_t MainApp::MemoryCardID()
{
    return static_cast<uint32_t>(UniqueCardID());
}

void MainApp::blink(uint core, uint32_t interval_ms)
{

    // uint pin = get_core_num();
    uint pin = core;
    static absolute_time_t next_toggle_time[32]; // indexed by GPIO
    static bool led_state[32] = {false};         // indexed by GPIO

    if (absolute_time_diff_us(get_absolute_time(), next_toggle_time[pin]) < 0)
    {
        led_state[pin] = !led_state[pin];
        LedOn(pin, led_state[pin]);

        next_toggle_time[pin] = make_timeout_time_ms(interval_ms);
    }
}

void MainApp::showLengthPattern(int length)
{
    struct PatternEntry
    {
        int length;
        uint8_t bitmask;
    };

    const PatternEntry patternTable[] = {
        {2, 0b110000},
        {3, 0b111000},
        {4, 0b111100},
        {5, 0b111110},
        {6, 0b111111},
        {8, 0b001111},
        {12, 0b000011},
        {16, 0b110011}};

    uint8_t mask = 0;

    ledMode = STATIC_PATTERN;
    lengthChangeStart = time_us_64();

    for (const auto &entry : patternTable)
    {
        if (entry.length == length)
        {
            mask = entry.bitmask;
            break;
        }
    }

    for (int i = 0; i < 6; ++i)
    {
        if (mask & (1 << (5 - i)))
        {
            LedOn(i);
        }
        else
        {
            LedOff(i);
        }
    }
}

void MainApp::updateLedState()
{

    if (ledMode == DYNAMIC_PWM)
    {
        if (VactrolLayerActive())
        {
            const uint16_t mix1 = CLAMP(vactrolLevel1, 0, 4095);
            const uint16_t mix2 = CLAMP(vactrolLevel2, 0, 4095);
            const uint16_t depth1 = CLAMP(vactrolDepth1, 0, 4095);
            const uint16_t depth2 = CLAMP(vactrolDepth2, 0, 4095);

            LedBrightness(0, mix1 << 4);
            LedBrightness(1, mix2 << 4);
            LedBrightness(2, depth1 << 4);
            LedBrightness(3, depth2 << 4);
        }
        else
        {
            LedBrightness(0, turingDAC1.DAC_8() << 4);
            LedBrightness(1, turingDAC2.DAC_8() << 4);
            LedBrightness(2, turingPWM1.DAC_8() << 4);
            LedBrightness(3, turingPWM2.DAC_8() << 4);
        }
        LedOn(4, pulseLed1_status);
        LedOn(5, pulseLed2_status);
    }
    else if (ledMode == STATIC_PATTERN)
    {

        if (time_us_64() - lengthChangeStart > 1500000)
        { // 1.5 seconds in µs
            ledMode = DYNAMIC_PWM;
        }
    }
}

void MainApp::TEST_write_to_Pulse(int i, bool val)
{
    PulseOut(i, val);
}

void MainApp::sysexRespond()
{
    const uint8_t sysExStart = 0xF0;
    const uint8_t sysExEnd = 0xF7;
    const uint8_t manufacturerId = 0x7D;
    const uint8_t messageType = 0x02;

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(settings);
    const size_t rawLen = sizeof(Config::Data);

    const size_t maxLen = 7 + ((rawLen + 6) / 7) * 8 + 1;
    uint8_t msg[maxLen];
    size_t out = 0;

    msg[out++] = sysExStart;
    msg[out++] = manufacturerId;
    msg[out++] = CARD_NUMBER;
    msg[out++] = messageType;
    msg[out++] = MAJOR_VERSION;
    msg[out++] = MINOR_VERSION;
    msg[out++] = POINT_VERSION;

    // Encodes the entire settings Data file from config.h
    // Includes:

    // Encode in 7-byte chunks with MSB prefix
    for (size_t i = 0; i < rawLen; i += 7)
    {
        uint8_t msb = 0;
        uint8_t block[7] = {0};

        for (size_t j = 0; j < 7; ++j)
        {
            size_t index = i + j;
            if (index >= rawLen)
                break;

            uint8_t byte = raw[index];
            if (byte & 0x80)
                msb |= (1 << j);

            block[j] = byte & 0x7F;
        }

        msg[out++] = msb;
        for (size_t j = 0; j < 7 && (i + j) < rawLen; ++j)
            msg[out++] = block[j];
    }

    msg[out++] = sysExEnd;
    tud_midi_stream_write(0, msg, out);
}

void MainApp::handleSysExMessage(const uint8_t *data, size_t len)
{
    if (len < 5 || data[0] != 0xF0 || data[len - 1] != 0xF7)
        return; // not a sysex message

    const uint8_t manufacturerId = data[1];
    const uint8_t deviceId = data[2];
    const uint8_t command = data[3];
    const uint8_t *payload = &data[4];
    const size_t payloadLen = len - 5;

    if (manufacturerId != 0x7D || deviceId != CARD_NUMBER)
        return;

    switch (command)
    {
    case 0x01:
        sysexRespond();
        break;

    case 0x03:
    {
        uint8_t decoded[sizeof(Config::Data)] = {0};
        size_t in = 0, out = 0;

        while (in < payloadLen && out < sizeof(decoded))
        {
            uint8_t msb = payload[in++];
            for (int j = 0; j < 7 && in < payloadLen && out < sizeof(decoded); ++j)
            {
                uint8_t b = payload[in++];
                if (msb & (1 << j))
                    b |= 0x80;
                decoded[out++] = b;
            }
        }

        if (out == sizeof(Config::Data))
        {
            memcpy(settings, decoded, sizeof(Config::Data));
            // BPM and divide stay under panel/live control rather than web config ownership.
            settings->bpm = CurrentBPM10;
            settings->divide = 5;

            cfg.save();
            LoadSettings(0);
        }
        break;
    }

    default:
        break;
    }
}

void MainApp::IdleLeds()
{
    static uint8_t tick = 0;

    // Use XOR to shuffle the pattern unpredictably
    uint8_t scrambled = tick ^ (tick << 1);
    uint8_t index = scrambled % 6;

    LedOn(index);
    sleep_us(20000); // ~20ms flash
    LedOff(index);

    tick++;
}

// Returns the high 7-bit MIDI-safe byte (bit 7 of input)
uint8_t MainApp::midiHi(uint8_t input)
{
    return (input >> 7) & 0x01;
}

// Returns the low 7-bit MIDI-safe byte (bits 0–6 of input)
uint8_t MainApp::midiLo(uint8_t input)
{
    return input & 0x7F;
}

void MainApp::SendLiveStatus()
{
    // No need for encoding, all bytes <127

    const uint8_t sysExStart = 0xF0;
    const uint8_t sysExEnd = 0xF7;
    const uint8_t manufacturerId = 0x7D;
    const uint8_t deviceId = CARD_NUMBER;
    const uint8_t messageType = 0x10;

    uint8_t msg[16]; // CHECK THIS!
    size_t out = 0;

    msg[out++] = sysExStart;
    msg[out++] = manufacturerId;
    msg[out++] = deviceId;
    msg[out++] = messageType;

    msg[out++] = midiHi(turingDAC1.DAC_8());
    msg[out++] = midiLo(turingDAC1.DAC_8());

    msg[out++] = midiHi(turingDAC2.DAC_8());
    msg[out++] = midiLo(turingDAC2.DAC_8());

    msg[out++] = midiHi(turingPWM1.DAC_8());
    msg[out++] = midiLo(turingPWM1.DAC_8());

    msg[out++] = midiHi(turingPWM2.DAC_8());
    msg[out++] = midiLo(turingPWM2.DAC_8());

    msg[out++] = turingRandomness >> 5; // 0-4095 down to 0-127
    msg[out++] = VactrolLayerActive();
    msg[out++] = turingPWM1.returnLength();

    msg[out++] = sysExEnd;

    tud_midi_stream_write(0, msg, out);
}

void MainApp::cv_map_build(int32_t low, int32_t high)
{
    const int32_t span = high - low;              // can be negative
    const int32_t lo = (low < high) ? low : high; // for safety clamp
    const int32_t hi = (low < high) ? high : low;

    for (int x = 0; x < 256; ++x)
    {
        // Exact linear map on 0..255 without overflow; signed-safe.
        // No rounding term so x=255 lands exactly on 'high'.
        int32_t y = low + (span * x) / 255;

        // Optional safety clamp to the given endpoints (supports low>high too)
        if (y < lo)
            y = lo;
        if (y > hi)
            y = hi;

        cv_lut[x] = (int16_t)y;
    }
}

void MainApp::cv_set_mode(uint8_t mode)
{
    // Matches your 4 ranges
    switch (mode)
    {
    case 0: /* ±6V  */
        cv_map_build(-2048, 2047);
        break;
    case 1: /* ±3V  */
        cv_map_build(-1024, 1024);
        break;
    case 2: /* 0..6V*/
        cv_map_build(0, 2047);
        break;
    case 3: /* 0..3V*/
        cv_map_build(0, 1024);
        break; // 0..+2.5V
    default:
        cv_map_build(-2048, 2047);
        break; // safe default
    }
}

int16_t MainApp::cv_map_u8(uint8_t x)
{
    return cv_lut[x]; // O(1) in the audio loop
}

int16_t MainApp::readInputIfConnected(Input inputType)
{
    if (Connected(inputType))
    {
        switch (inputType)
        {
        case Audio1:
            return AudioIn1();
        case Audio2:
            return AudioIn2();
        case CV1:
            return CVIn1();
        case CV2:
            return CVIn2();
        default:
            return 0;
        }
    }
    return 0;
}

#include <stdint.h>

// Returns semitone offset above C3 (0..12).
// VERY CRUDE AND UNCALIBRATED, TREAT AS EXPERIMENTAL
int MainApp::CVtoMidiOffset(int16_t raw)
{
    if (raw == 0)
        return 0; // disconnected -> offset 0

    // Measured centers for C3..C4
    static constexpr int16_t NOTE_COUNTS[13] = {
        -10, // C3
        34,  // C#3
        58,  // D3
        85,  // D#3
        104, // E3
        127, // F3
        153, // F#3
        175, // G3
        202, // G#3
        227, // A3
        253, // A#3
        278, // B3
        303  // C4
    };

    // Midpoint thresholds *2 between adjacent notes (integer-only compare)
    static constexpr int16_t MID_2X[12] = {
        (-10 + 34),  // C3|C#3
        (34 + 58),   // C#3|D3
        (58 + 85),   // D3|D#3
        (85 + 104),  // D#3|E3
        (104 + 127), // E3|F3
        (127 + 153), // F3|F#3
        (153 + 175), // F#3|G3
        (175 + 202), // G3|G#3
        (202 + 227), // G#3|A3
        (227 + 253), // A3|A#3
        (253 + 278), // A#3|B3
        (278 + 303)  // B3|C4
    };

    int c2 = int(raw) * 2;
    int semi = 0;
    while (semi < 12 && c2 >= MID_2X[semi])
        ++semi;

    // Clamp to 0..12 (handles e.g. -1500 -> 0, +1500 -> 12)
    if (semi < 0)
        semi = 0;
    if (semi > 12)
        semi = 12;
    return semi;
}

void MainApp::SetVactrolControls(uint16_t slew, uint16_t depth1, uint16_t depth2)
{
    if (!VactrolLayerActive())
    {
        turingRandomness = KnobVal(Main);
        return;
    }

    vactrolSlew = slew;
    vactrolDepth1 = depth1;
    vactrolDepth2 = depth2;
    UpdateVactrolTiming();
}

void MainApp::UpdateVactrolTiming()
{
    const int32_t riseTime = 32 + (int32_t(settings->vactrol.rise) * 24);
    const int32_t fallTime = 32 + (int32_t(settings->vactrol.fall) * 24);
    const int32_t knobLag = 64 + (int32_t(vactrolSlew) >> 1);

    vactrolRiseStep = 1 + (4096 / (riseTime + knobLag));
    vactrolFallStep = 1 + (4096 / (fallTime + knobLag));
}

void MainApp::ProcessVactrolMix()
{
    if (!VactrolLayerActive())
    {
        AudioOut1(AudioIn1());
        AudioOut2(AudioIn2());
        return;
    }

    const int32_t in1 = AudioIn1();
    const int32_t in2 = AudioIn2();
    const int32_t cvIn1 = CVIn1();
    const int32_t cvIn2 = CVIn2();

    int32_t lane1 = vactrolTargetBase1;
    int32_t lane2 = vactrolTargetBase2;

    if (settings->vactrol.relation == 1)
    {
        lane2 = lane1;
    }
    else if (settings->vactrol.relation == 2)
    {
        lane2 = 4095 - lane1;
    }

    const int32_t min1 = int32_t(settings->vactrol.min1) << 4;
    const int32_t max1 = (int32_t(settings->vactrol.max1) << 4) | 0x0f;
    const int32_t min2 = int32_t(settings->vactrol.min2) << 4;
    const int32_t max2 = (int32_t(settings->vactrol.max2) << 4) | 0x0f;

    lane1 = min1 + ((lane1 * (max1 - min1)) >> 12);
    lane2 = min2 + ((lane2 * (max2 - min2)) >> 12);

    int32_t shaped1 = 2048 + (((lane1 - 2048) * int32_t(vactrolDepth1)) >> 12);
    int32_t shaped2 = 2048 + (((lane2 - 2048) * int32_t(vactrolDepth2)) >> 12);

    shaped1 = CLAMP(shaped1, 0, 4095);
    shaped2 = CLAMP(shaped2, 0, 4095);

    switch (settings->vactrol.law)
    {
    case 1:
        shaped1 = (shaped1 * shaped1) >> 12;
        shaped2 = (shaped2 * shaped2) >> 12;
        break;
    default:
        break;
    }

    if (vactrolLevel1 < shaped1)
    {
        vactrolLevel1 += vactrolRiseStep;
        if (vactrolLevel1 > shaped1)
            vactrolLevel1 = shaped1;
    }
    else if (vactrolLevel1 > shaped1)
    {
        vactrolLevel1 -= vactrolFallStep;
        if (vactrolLevel1 < shaped1)
            vactrolLevel1 = shaped1;
    }

    if (vactrolLevel2 < shaped2)
    {
        vactrolLevel2 += vactrolRiseStep;
        if (vactrolLevel2 > shaped2)
            vactrolLevel2 = shaped2;
    }
    else if (vactrolLevel2 > shaped2)
    {
        vactrolLevel2 -= vactrolFallStep;
        if (vactrolLevel2 < shaped2)
            vactrolLevel2 = shaped2;
    }

    const int32_t g1 = CLAMP(vactrolLevel1, 0, 4095);
    const int32_t g2 = CLAMP(vactrolLevel2, 0, 4095);
    const int32_t out1 = ((in1 * g1) + (in2 * (4095 - g1))) >> 12;
    const int32_t out2 = ((in2 * g2) + (in1 * (4095 - g2))) >> 12;
    const int32_t cvOut1 = ((cvIn1 * g1) + (cvIn2 * (4095 - g1))) >> 12;
    const int32_t cvOut2 = ((cvIn2 * g2) + (cvIn1 * (4095 - g2))) >> 12;

    AudioOut1(CLAMP(out1, -2048, 2047));
    AudioOut2(CLAMP(out2, -2048, 2047));
    CVOut1(CLAMP(cvOut1, -2048, 2047));
    CVOut2(CLAMP(cvOut2, -2048, 2047));
}
