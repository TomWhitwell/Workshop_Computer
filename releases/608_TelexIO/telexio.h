#pragma once
#include <cstdint>

// I2C Adresses are set here:
constexpr uint8_t I2C_TELEXI_ADDRESS = 0x68;
constexpr uint8_t I2C_TELEXO_ADDRESS = 0x60;

// 1,0 turns profiling on,off in the 48khz callback
#define BENCHMARK 0
#define STRESSTEST 0

#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"

// i2c forward declarations
void i2c_receive_handler(uint8_t data, bool is_address);
void i2c_request_handler(uint8_t address);
void i2c_stop_handler(uint8_t length);

class TelexIO : public ComputerCard
{
public:

    TelexIO();

    static void core1();

    void StartupAnimation();
    void SlowProcessingCore();

    void ProcessSample() override;

    // TELEXI
    void telexIread();
    void InitTelexI();
    int telexIinput(int address);
    void telexIParse();
    void telexIActOnCommand(uint8_t cmd, uint8_t out, int16_t value);

    // TELEXO
    void InitTelexO();
    void telexOParse();
    void telexOActOnCommand(uint8_t cmd, uint8_t out, int16_t value);

    // Utility
    void SetLed(int led, bool onoff);
    void SetPulse(int out, bool onoff);
    int32_t mv2analogread(int32_t millivolts); 
};

__attribute__((always_inline))
inline void __not_in_flash_func(TelexIO::SetLed)(int led, bool onoff) {
    if (led<6) LedOn(led, onoff);
}
__attribute__((always_inline))
inline void __not_in_flash_func(TelexIO::SetPulse)(int out, bool onoff) {
    if (out<2) PulseOut(out, onoff);
}

__attribute__((always_inline))
inline int32_t __not_in_flash_func(TelexIO::mv2analogread)(int32_t millivolts)
{
    // no overflow for int32 with eurorack voltages
    constexpr int32_t scaleQ16 = 107374;
    int32_t value = (millivolts * scaleQ16) >> 16;

    value = (value > 16383)  ? 16383  : value;
    value = (value < -16384) ? -16384 : value;

    return value;
}
