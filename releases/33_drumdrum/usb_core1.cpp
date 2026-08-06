// Core 1 entry — USB host or device task loop.
//
// The role is decided synchronously in main() by reading USBPowerState()
// on the USB-C CC pins, and stashed in gUsbHostMode before Core 1 is
// launched. Initialisation is split to match MLRws (a known-working
// reference on this hardware):
//
//   Device mode: main() calls board_init() + tud_init() on Core 0 *before*
//                launching Core 1, so the host sees the device immediately.
//                Core 1 here just runs the task pump.
//
//   Host mode:   Core 1 calls board_init() + tusb_init() itself. tusb_init
//                (not tuh_init) is required because our tusb_config has
//                CFG_TUSB_RHPORT0_MODE = HOST | DEVICE — going through
//                tusb_init configures the USB hardware for the chosen role.

#include "usb_core1.h"
#include "shared_state.h"
#include "midi_sysex.h"
#include "monome_mext.h"
#include "grid_ui.h"
#include "midi_host.h"

#include "tusb.h"
#include "bsp/board_api.h"
#include "pico/multicore.h"

volatile uint8_t gUsbHostMode = 0;

static void run_host_loop(void)
{
    board_init();
    // Both host paths coexist: a Grid plugged in fires CDC mount cbs
    // into mext, an 8mu (or any class-compliant USB MIDI device) fires
    // our app-registered MIDI driver. Whichever shows up wins; the
    // other path stays idle. mext_task() drives tuh_task(), which is
    // what dispatches the MIDI driver's xfer callbacks.
    mext_init(MEXT_TRANSPORT_HOST, 0);
    midi_host_init();
    tusb_init();
    grid_ui_init();
    while (true) {
        mext_task();
        grid_ui_process_input();
        grid_ui_render();
    }
}

static void run_device_loop(void)
{
    // board_init + tud_init were called from Core 0 in main(), so Core 1
    // just pumps the task here.
    while (true) {
        tud_task();
        midi_device_task();
    }
}

extern "C" void core1_entry(void)
{
    if (gUsbHostMode) {
        run_host_loop();    // never returns
    } else {
        run_device_loop();  // never returns
    }
}
