#include "GridsCard.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

// TinyUSB tusb_types.h: tu_edpt_addr() trips -Wextra (enum vs 0u in ?:) on GCC.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "tusb.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "bsp/board.h"

static void core1_audio_entry() {
  // Must receive the card pointer BEFORE multicore_lockout_victim_init(): the lockout
  // handler replaces the SIO FIFO IRQ and drains non-lockout words without delivering
  // them to multicore_fifo_pop_blocking(), so a pointer pushed from core 0 would be
  // discarded and core 1 would hang here (USB/MIDI on core 0 still works).
  GridsCard* card = reinterpret_cast<GridsCard*>(multicore_fifo_pop_blocking());
  multicore_lockout_victim_init();
  card->EnableNormalisationProbe();
  card->Run();
}

// Same USB role detection as 20_reverb usb_worker() (computer.h + reverb.c).
static bool workshop_usb_is_host_port(GridsCard* card) {
  using HV = ComputerCard::HardwareVersion_t;
  const HV hw = card->HardwareRevision();
  if (hw == HV::Proto1 || hw == HV::Proto2_Rev1) {
    return false;
  }
  if (gpio_get(USB_HOST_STATUS)) {
    return false;  // UFP — use this port to talk to a PC as a USB device
  }
  return true;  // DFP — host port (thumb drive etc.), not Web MIDI to computer
}

static void usb_core0_loop(GridsCard* card) {
  sleep_ms(100);

  const bool is_host = workshop_usb_is_host_port(card);

  if (is_host) {
    tuh_init(0);
  } else {
    tud_init(0);
  }
  board_init();

  for (;;) {
    const absolute_time_t slice_end = make_timeout_time_ms(1);
    while (!time_reached(slice_end)) {
      if (is_host) {
        tuh_task();
      } else {
        tud_task();
      }
    }
    if (!is_host) {
      card->Housekeeping();
    }
  }
}

int main() {
  // 20_reverb main(): certified RP2040 profile for this codebase
  set_sys_clock_khz(200000, true);

  static GridsCard app;
  multicore_launch_core1(core1_audio_entry);
  multicore_fifo_push_blocking(reinterpret_cast<uintptr_t>(&app));

  usb_core0_loop(&app);
}
