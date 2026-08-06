#pragma once
#include <stdint.h>

class MainApp;
class Clock;
#define CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

class UI
{
public:
    void Tick();
    void init(MainApp *app, Clock *clock);
    void SlowUI();
    void SetPulseLength(uint8_t lenPercent);
    void SetPulseMod(uint8_t lenPercent);
    void UpdatePulseMod(uint8_t turing1, uint8_t turing2);

private:
    int threshold = 48; // how many ticks before calling slow UI = 1ms
    MainApp *app = nullptr;
    Clock *clk = nullptr;
    bool led1Status = false;
    bool led2Status = false;
    bool pulse1Status = false;
    bool pulse2Status = false;
    int ledPulseLength = 480;   // number of ticks at 48khz = 10ms
    int outputPulseLength = 96; // 8ms = just for testing should be more like 2ms
    int outputDivideLength = 96;
    int ledPulseTicksRemaining1 = 0;
    int ledPulseTicksRemaining2 = 0;
    int outputPulseTicksRemaining1 = 0;
    int outputPulseTicksRemaining2 = 0;
    bool ledPulseActive1 = false;
    bool ledPulseActive2 = false;
    bool outputPulseActive1 = false;
    bool outputPulseActive2 = false;
    void TriggerPulse1();
    void EndPulse1();
    void TriggerPulse2();
    void EndPulse2();
    uint8_t pulseModLevel = 0;
    int outputPulseMod1 = 0; // NB must be signed
    int outputPulseMod2 = 0;

    uint8_t lastDivideStep = 5;
    uint8_t numDivideSteps = 9;
    uint8_t QuantiseToStep(uint32_t knobVal, uint8_t steps, uint32_t range);

    uint8_t lastLength = 0;
    uint8_t const numLengthSteps = 8;
    uint8_t const lengths[8] = {2, 3, 4, 5, 6, 8, 12, 16};

    struct KnobPickup
    {
        uint16_t entry = 0;
        bool pickedUp = false;
    };

    KnobPickup middleMain;
    KnobPickup middleX;
    KnobPickup middleY;
    KnobPickup upperMain;
    KnobPickup upperX;
    KnobPickup upperY;

    bool pickupInitialised = false;
    bool pickupArmed = false;
    bool lastLayerUpper = false;
    uint16_t pickupDeadband = 48;
    uint16_t pickupMoveThreshold = 64;
    uint16_t pickupCatchThreshold = 96;
    uint16_t currentMiddleMainValue = 2048;
    uint16_t currentMiddleXValue = 2925;
    uint16_t currentMiddleYValue = 2559;
    uint16_t currentUpperMainValue = 256;
    uint16_t currentUpperXValue = 4095;
    uint16_t currentUpperYValue = 4095;

    void ResetPickup(KnobPickup &pickup, uint16_t raw);
    bool ApplyPickup(KnobPickup &pickup, uint16_t raw, uint16_t target);
    uint16_t StepToKnobValue(uint8_t step, uint8_t steps, uint32_t range);
};
