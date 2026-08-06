/************************************************************
 *  Core-split bootstrap for ComputerCard on RP2040
 *
 *  • Core 0 – USB-CDC stdio + flash-save service
 *  • Core 1 – ComputerCard audio engine (48 kHz ISR)
 *
 *  Requires:
 *    - CMakeLists.txt links pico_stdlib & pico_multicore
 *    - PICO_COPY_TO_RAM 1  (so flash stalls don’t hurt audio)
 ***********************************************************/

#include "pico/version.h"
#pragma message("SDK = " PICO_SDK_VERSION_STRING)

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "Clock.h"
#include "UI.h"
#include "MainApp.h"
#include "pico/stdlib.h"
#include <cstdio>
#include "tusb.h"

/* Global handle published by Core 1 after MainApp is constructed */
static MainApp *volatile gApp = nullptr;

static void core1_entry()
{
    multicore_lockout_victim_init();
    // prinft("Core 1 running on core %d\n", get_core_num());

    static MainApp app; // all ComputerCard work lives here
    gApp = &app;        // publish pointer for Core 0
    multicore_fifo_push_blocking(reinterpret_cast<uintptr_t>(gApp));
    app.EnableNormalisationProbe();
    app.Run(); // never returns
}

int main()
{
    set_sys_clock_khz(192000, true);

    stdio_usb_init(); // Initialize USB serial // Claims for Core 0
    sleep_ms(10);
    tusb_init();
    // launch the audio engine on core 1
    multicore_launch_core1(core1_entry);

    // wait until Core 1 has published MainApp* (rarely more than 100 µs)
    while (multicore_fifo_rvalid())
    { // do nothing
    }

    uintptr_t ptr = multicore_fifo_pop_blocking();
    gApp = reinterpret_cast<MainApp *>(ptr);

    absolute_time_t next = make_timeout_time_ms(1);
    sleep_ms(10); // allow switch readings to settle
    // Reload settings - initialised to defaults, then wait until the switch is released
    gApp->LoadSettings(gApp->SwitchDown());
    while (gApp->SwitchDown())
    {
        // do nothing
        gApp->IdleLeds();
    }

    while (true)
    {
        gApp->Housekeeping();
        tud_task(); // Process USB MIDI on Core 0

        // ------------- pace the loop  ---------------
        sleep_until(next); // keeps 1-ms period
        next = delayed_by_ms(next, 1);
    }
}
