//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Main program, simply calls D_DoomMain high level loop.
//

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#if !LIB_PICO_STDLIB
#include "SDL.h"
#else
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/sem.h"
#include "pico/multicore.h"
#if PICO_ON_DEVICE
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#endif
#endif
#if USE_PICO_NET
#include "piconet.h"
#endif
#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#endif
//
// D_DoomMain()
// Not a globally visible function, just included for source reference,
// calls all startup code, parses command line options.
//

void D_DoomMain (void);

#if PICO_ON_DEVICE
#include "pico/binary_info.h"
#if WS_COMPUTER
#include "pico/ws/ws_hw.h"
#include "pico/ws/ws_stream.h"
bi_decl(bi_program_feature("Workshop System Computer card"));
#else
bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));
#endif
#endif

int main(int argc, char **argv)
{
    // save arguments
#if !NO_USE_ARGS
    myargc = argc;
    myargv = argv;
#endif
#if PICO_ON_DEVICE
#if WS_COMPUTER
    // boot diagnostic ladder, stage 1: we are executing at all.
    // LED 0 hard on before any clock/voltage changes.
    gpio_init(10);
    gpio_set_dir(10, GPIO_OUT);
    gpio_put(10, true);
#endif
#if PICO_RP2350
    uint clkdiv = 3;
    uint rxdelay = 2;
    hw_write_masked(
            &qmi_hw->m[0].timing,
            ((clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS) |
            ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS),
            QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS
    );
#endif
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    busy_wait_us(1000);
    // todo pause? is this the cause of the cold start issue?
    set_sys_clock_khz(270000, true);
#if !USE_PICO_NET
    // debug ?
//    gpio_debug_pins_init();
#endif
#ifdef PICO_SMPS_MODE_PIN
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, 1);
#endif
#endif
#if WS_COMPUTER
    // before stdio: ws_hw_early_init re-clocks clk_peri, which the UART
    // baud divider depends on
    ws_hw_early_init();
#endif
#if LIB_PICO_STDIO
    stdio_init_all();
#endif
#if WS_COMPUTER
    ws_stream_init();
    {
        // Probe the WAD before doom does: report exactly what flash holds
        // at TINY_WAD_ADDR if the magic is missing. Double-read to expose
        // unstable (timing-marginal) flash reads.
        const volatile uint8_t *wad = (const volatile uint8_t *) TINY_WAD_ADDR;
        uint8_t a[8], b[8];
        for (int i = 0; i < 8; i++) a[i] = wad[i];
        busy_wait_us(100);
        for (int i = 0; i < 8; i++) b[i] = wad[i];
        if (a[0] != 'I' || a[1] != 'W' || a[2] != 'H' || a[3] != 'X') {
            extern void __attribute__((noreturn)) ws_panic(const char *msg);
            static char m[160];
            snprintf(m, sizeof(m),
                     "No IWHX at %08x: %02x %02x %02x %02x %02x %02x %02x %02x"
                     " / %02x %02x %02x %02x %02x %02x %02x %02x%s",
                     (unsigned) TINY_WAD_ADDR,
                     a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                     b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                     (a[0] == 0xff && a[1] == 0xff && a[2] == 0xff && a[3] == 0xff)
                         ? " (erased: WAD never reached flash)" : "");
            ws_panic(m);
        }
    }
    {
        // hold in the boot gate (USB serviced, nothing else running)
        // until the player flicks the Z switch down
        extern void ws_gate_wait(void);
        ws_gate_wait();
    }
#endif
#if PICO_BUILD
    I_Init();
#endif
#if USE_PICO_NET
    // do init early to set pulls
    piconet_init();
#endif
//!
    // Print the program version and exit.
    //
    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts(PACKAGE_STRING);
        exit(0);
    }

#if !NO_USE_ARGS
    M_FindResponseFile();
#endif

    #ifdef SDL_HINT_NO_SIGNAL_HANDLERS
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    #endif

    // start doom

    D_DoomMain ();

    return 0;
}

