// MainApp.h
#pragma once
#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include "Clock.h"
#include "UI.h"
#include "Turing.h"
#include "Config.h"

class MainApp : public ComputerCard
{
    Config::Data *settings = nullptr;

public:
    MainApp();
    void ProcessSample() override;

    void PulseLed1(bool status);
    void PulseLed2(bool status);
    bool PulseOutput1(bool status);
    bool PulseOutput2(bool status);
    bool PulseInConnected1();
    bool PulseInConnected2();
    bool tapReceived();
    bool extPulse1Received();
    bool extPulse2Received();
    bool VactrolLayerActive();

    uint16_t KnobMain();
    uint16_t KnobX();
    uint16_t KnobY();
    bool ModeSwitch();
    bool SwitchDown();
    int16_t readInputIfConnected(Input inputType);

    void divideKnobChanged(uint8_t step);
    void lengthKnobChanged(uint8_t length);

    void updateMainTuring();
    void updateDivTuring();

    uint32_t MemoryCardID();

    void Housekeeping();

    void LoadSettings(bool reset);

    uint64_t processTime;     // TESTING
    uint64_t lastProcessTime; // TESTING
    uint64_t processStepTime; // TESTING

    void blink(uint core, uint32_t interval_ms); // TESTING blinks LED related to core at given freq

    void updateLedState();
    void TEST_write_to_Pulse(int i, bool val);
    void UpdateNotePools();
    void UpdatePulseLengths();
    bool switchChanged();
    void IdleLeds();
    void SetTuringRandomness(uint16_t value);
    void UpdateCh2Lengths();
    void UpdateCVRange();
    void SetVactrolControls(uint16_t slew, uint16_t depth1, uint16_t depth2);
    void UpdateVactrolTiming();

private:
    Clock clk;
    UI ui;
    Config cfg;

    Turing turingDAC1;
    Turing turingDAC2;
    Turing turingPWM1;
    Turing turingPWM2;
    Turing turingPulseLength1;
    Turing turingPulseLength2;
    uint16_t maxRange = 4095; // maximum pot value

    volatile uint16_t CurrentBPM10 = 1200; // 10x bpm default
    volatile uint16_t newBPM10 = 0;        // 10x bpm default

    uint32_t lastTap = 0;
    uint32_t debounceTimeout = 480; // 10ms in 48khz clock ticks
    uint64_t lastChangeTimeUs;
    volatile bool pendingSave;

    bool pulseLed1_status = 0;
    bool pulseLed2_status = 0;

    enum LedMode
    {
        STATIC_PATTERN,
        DYNAMIC_PWM
    };
    LedMode ledMode = DYNAMIC_PWM;
    uint64_t lengthChangeStart = 0;
    void showLengthPattern(int length);

    bool oldSwitch = 0;

    void sysexRespond();
    void handleSysExMessage(const uint8_t *data, size_t len);
    void pollSysEx();

    void SendLiveStatus();
    uint8_t midiHi(uint8_t input);
    uint8_t midiLo(uint8_t input);

    bool sendViz = false;

    // To handle CV mapping
    int16_t cv_lut[256];
    void cv_map_build(int32_t low, int32_t high);
    void cv_set_mode(uint8_t mode);
    int16_t cv_map_u8(uint8_t x);

    // Experimental - CV2 to note offset
    int CVtoMidiOffset(int16_t raw);
    uint8_t midiOffset = 0;

    void ProcessVactrolMix();
    uint16_t turingRandomness = 2048;
    uint16_t vactrolSlew = 256;
    uint16_t vactrolDepth1 = 4095;
    uint16_t vactrolDepth2 = 4095;
    int32_t vactrolLevel1 = 2048;
    int32_t vactrolLevel2 = 2048;
    int32_t vactrolTarget1 = 2048;
    int32_t vactrolTarget2 = 2048;
    int32_t vactrolTargetBase1 = 2048;
    int32_t vactrolTargetBase2 = 2048;
    int32_t vactrolRiseStep = 8;
    int32_t vactrolFallStep = 6;

    static constexpr size_t kSysExBufferSize = 512;
    uint8_t sysexBuffer[kSysExBufferSize] = {};
    size_t sysexLength = 0;
    bool sysexActive = false;
};
