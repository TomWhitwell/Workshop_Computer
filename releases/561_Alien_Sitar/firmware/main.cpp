/// @file main.cpp
/// @brief RP2040 boot entry: regulator/clock setup, then the instrument backend.
///
/// The pinned ComputerCard NOTES guidance documents 192 MHz at 1.15 V as
/// supported for the Workshop Computer; 192 MHz is a multiple of the 24 kHz
/// sample rate, keeping the CV-output PWM alias-free per that guidance. The regulator
/// voltage is set first and given 10 ms to settle at the boot clock; the
/// system clock is then raised with the exact-frequency request, and only
/// after that does runInstrument construct the ComputerCard hardware, launch
/// core1 and enter the 48 kHz audio callback (order matters: hardware must
/// not configure SPI/I2C at a clock that is about to change). The firmware is
/// copy-to-RAM linked, so this code runs from SRAM with an SRAM runtime vector
/// table, keeping flash saves serviceable while running (see PERSISTENCE.md).
/// This 10 ms settle applies once at boot and does not delay later plucks.
#include "audio_backend.hpp"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

/// @brief Sets 1.15 V regulator voltage (10 ms settle), raises the clock to
///        192 MHz, then hands over to computer::runInstrument() on core0.
/// @return Unreachable: runInstrument never returns.
int main() {
    // NOTES.md specifies 192 MHz at 1.15 V as supported. Set voltage and allow
    // it to settle before raising the clock, then initialise clocked peripherals.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(10);
    set_sys_clock_khz(192000, true);
    computer::runInstrument();
}
