#include "audio_backend.hpp"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

int main() {
    // ComputerCard recommends 192 MHz at 1.15 V. Set voltage and allow
    // it to settle before raising the clock, then initialise clocked peripherals.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(10);
    set_sys_clock_khz(192000, true);
    computer::runInstrument();
}
