#pragma once
#include <stdint.h>

class Clock
{
public:
    void Tick();
    void Reset();
    void SetPhaseIncrement(uint32_t increment);
    uint32_t GetPhase() const;
    bool IsRisingEdge() const;
    bool IsRisingEdgeMult() const;
    uint16_t TapTempo(uint32_t tapTime); // returns BPM10 when tempo is set, otherwise retuns 0
    uint32_t GetTicks() const;
    void UpdateDivide(uint8_t step);
    void setExternalClock1(bool ext);
    void setExternalClock2(bool ext);
    bool getExternalClock1();
    bool getExternalClock2();

    void ExtPulse1();
    void ExtPulse2();
    bool ExtPulseReceived1();
    bool ExtPulseReceived2();
    void setBPM10(uint16_t bpm10);
    uint16_t getBPM10();
    uint32_t GetTicksPerBeat();
    uint32_t GetTicksPerSubclockBeat();
    uint32_t TEST_subclock_phase = 0;

private:
    uint32_t minInterval = 480;    // e.g., 10ms at 48kHz - to lock out double taps and noise
    uint32_t maxInterval = 144000; // 3 seconds
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
    bool rising_edge = false;
    bool rising_edge_mult = false;
    uint32_t lastTapTime = 0;
    uint32_t totalTicks = 0;
    void SetPhaseIncrementFromTicks(uint32_t ticks_per_beat);
    void SetPhaseIncrementFromBPM10(uint16_t BPM10);
    uint16_t GetBPM10FromPhaseIncrement();

    volatile uint32_t subclockDividor = 16;
    const uint16_t subclockDivisions[9] = {512, 256, 128, 64, 32, 16, 8, 4, 2};
    // const uint16_t subclockDivisions[9] = {1024, 512, 256, 128, 64, 32, 16, 8, 4};
    const uint16_t subclockMultiplier = 16; // fastest clock multiplication, from which all other clocks are derived
    const uint8_t subclockShift = 4;        // since 2⁴ = 16

    uint32_t subclockCount = 0;
    bool subclockSync = false;
    bool isExternalClock1 = false;
    bool isExternalClock2 = false;
    bool receivedExtPulse1 = false;
    bool receivedExtPulse2 = false;

    uint32_t PHASE_WRAP_THRESHOLD = 0xF0000000;

    uint32_t clockSpeed = 48000; // just to calculate BPM, you can't change clock speed by changing this
};
