// TRACT8 - Core 1 USB host task pump.
//
// Core 1 owns the whole USB stack and does nothing else. Core 0 runs the
// 48 kHz audio interrupt via ComputerCard::Run().
//
// This split is the opposite of what might seem natural (DSP on the second
// core, housekeeping on the first), and it is deliberate. ComputerCard
// installs its audio interrupt on whichever core calls Run(), and every
// working USB-host card on this hardware - the official ComputerCard
// midi_host example included - puts audio on Core 0 and the USB pump on
// Core 1. tuh_task() is a blocking-ish polled loop with no deadline of its
// own; the audio ISR has a hard 20.8 us one. Giving the ISR the core that
// is not also servicing USB interrupts is the safer arrangement.
//
// The MIDI host driver is rppicomidi/usb_midi_host (MIT), vendored
// unmodified in usb_midi_host.c/.h and registered through TinyUSB's
// usbh_app_driver_get_cb() hook in usb_midi_host_app_driver.c. Pico SDK
// 2.2.0 ships TinyUSB 0.18, which has no MIDI host class driver of its own.

#include "shared.h"
#include "midi8mu.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_midi_host.h"

volatile VoderState g_state;

namespace {

// Address of the one MIDI device we listen to. The front jack is a single
// port; a hub could bring more, but the first device to mount wins.
uint8_t s_dev_addr = 0;

// Running-status parser state. tuh_midi_stream_read() hands back a byte
// stream, not aligned messages, so status and data bytes have to be
// reassembled here. Running status - a stream of data bytes with the
// status sent only once - is exactly what an 8mu emits when you sweep a
// fader, so this is not an optional nicety.
uint8_t s_status = 0;
uint8_t s_data1 = 0;
uint8_t s_have_data1 = 0;

// Bytes handed from the USB callback to the parser.
//
// THE CALLBACK MUST NOT PARSE. tuh_midi_rx_cb() is invoked from inside
// midih_xfer_cb(), BEFORE that function re-arms the IN endpoint with
// usbh_edpt_xfer(). Every microsecond spent in the callback is time the
// endpoint is not listening - and if the next packet arrives before the
// re-arm, the transfer errors, the driver's TU_ASSERT returns early, and
// THE ENDPOINT IS NEVER RE-ARMED AGAIN. The host stops polling, the device
// times out, and USB re-enumerates.
//
// That is what "the 8mu keeps dropping out and then seems to power cycle"
// was: not lost messages, a wedged endpoint. Faders 1 and 8 appeared worst
// hit because they are the vowel axes and get moved most, so their CCs are
// the ones most often in flight when it happened.
//
// So the callback now does the least possible - a memcpy into this ring -
// and all parsing happens in the core 1 loop after tuh_task() returns,
// with the endpoint safely re-armed.
//
// 2048 bytes is about 680 MIDI messages, far more than can arrive between
// two turns of a loop that does nothing else. Written by the callback,
// read by the loop, both on core 1, so no atomics are needed - but the
// indices are volatile because the callback runs from interrupt context.
constexpr uint32_t kRxRingSize = 2048;          // power of two
constexpr uint32_t kRxRingMask = kRxRingSize - 1;
uint8_t s_rx_ring[kRxRingSize];
volatile uint32_t s_rx_head = 0;
volatile uint32_t s_rx_tail = 0;

void ParseByte(uint8_t b) {
  if (b >= 0xF8) {
    // System real-time (clock, start, stop) can appear anywhere, even
    // between the data bytes of another message. Ignore without
    // disturbing the running status.
    return;
  }

  if (b & 0x80) {
    if (b >= 0xF0) {
      // System common cancels running status. We do not decode sysex; the
      // bytes of one will be swallowed harmlessly as data with no status.
      s_status = 0;
      s_have_data1 = 0;
      return;
    }
    s_status = b;
    s_have_data1 = 0;
    return;
  }

  if (s_status == 0) return;  // data byte with no status, drop

  if (!s_have_data1) {
    s_data1 = b;
    s_have_data1 = 1;

    // Program change and channel aftertouch are single-data-byte messages.
    // Neither is mapped, but they must be consumed or the parser would
    // treat the next status byte as their second data byte.
    const uint8_t type = s_status & 0xF0;
    if (type == 0xC0 || type == 0xD0) {
      s_have_data1 = 0;
    }
    return;
  }

  tract8::Midi8muMessage(s_status, s_data1, b);
  s_have_data1 = 0;  // stay armed for running status
}

}  // namespace

// --- rppicomidi callbacks ----------------------------------------------

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx) {
  (void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;

  // Always take the newest device, rather than only accepting one when no
  // address is held.
  //
  // The old form ignored a mount whenever s_dev_addr was already set. That
  // sounds harmless - the front jack is a single port - but it makes a
  // stale address unrecoverable: if the card ever holds an address the
  // device no longer has, every subsequent mount is refused and every
  // message is dropped by the address check in tuh_midi_rx_cb(). The 8mu
  // sits there with its lights on, responding to its own faders, changing
  // nothing.
  s_dev_addr = dev_addr;
  g_state.midi_connected = 1;

  // A fresh device means a fresh stream. Any half-parsed running-status
  // message from the previous session would otherwise consume the first
  // byte of this one.
  s_status = 0;
  s_have_data1 = 0;
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)instance;

  if (dev_addr == s_dev_addr) {
    s_dev_addr = 0;
    g_state.midi_connected = 0;

    // Leave band_gain where it is. Unplugging the 8mu mid-phrase should
    // not slam every formant shut - the panel knobs pick up from whatever
    // the faders last set, which is the least surprising behaviour.
    s_status = 0;
    s_have_data1 = 0;
  }
}

// Maximum bytes drained per callback.
//
// ONE endpoint's worth, not four. The IN endpoint delivers at most
// ep_in_max - 64 bytes on a full-speed bulk endpoint - per transfer, and
// this callback runs once per transfer, so 64 is all that can have arrived
// since the last one. The previous 256 was chosen to "drain generously"
// and that was simply wrong: the extra iterations only ran
// tuh_midi_stream_read() again on an empty FIFO, paying its packet-parsing
// overhead three more times for nothing.
//
// That overhead is the point. tuh_midi_stream_read() is not a memcpy - it
// peeks the FIFO, decodes USB-MIDI packets four bytes at a time, tracks
// sysex state and copies out. All of that happens inside midih_xfer_cb(),
// BEFORE it re-arms the IN endpoint with usbh_edpt_xfer(). If the next
// packet arrives first the transfer errors, the driver's TU_ASSERT returns
// early, and the endpoint is never re-armed - the host stops polling and
// the device re-enumerates.
//
// The 8mu was still occasionally cycling its LEDs as though it had just
// been plugged in, which is what a re-enumeration looks like from the
// outside. Moving the MIDI parsing out of the callback was necessary but
// not sufficient; the driver's own packet parsing had to be bounded too.
static constexpr uint32_t kMaxRxBytesPerCallback = 64;

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
  if (num_packets == 0) return;

  // ADOPT whatever device is actually sending, rather than dropping
  // everything that does not match the address we think we hold.
  //
  // This is the recovery path for the failure above. If the card's idea of
  // the address ever diverges from reality - a spurious umount, a missed
  // mount, an enumeration that half-completed - the old check turned that
  // into permanent silence: data was arriving and being thrown away, with
  // the device's own lights still on to say it was working.
  //
  // Taking the address from the data that is genuinely arriving cannot be
  // wrong: something is talking to us on a MIDI IN endpoint, and this card
  // listens to exactly one device.
  if (s_dev_addr != dev_addr) {
    s_dev_addr = dev_addr;
    g_state.midi_connected = 1;
    s_status = 0;
    s_have_data1 = 0;
  }

  // Copy into the ring and return. No parsing here - see the note on
  // s_rx_ring for why this callback has to be as short as possible.
  uint8_t cable;
  uint8_t buf[64];
  uint32_t drained = 0;

  while (drained < kMaxRxBytesPerCallback) {
    const uint32_t n = tuh_midi_stream_read(dev_addr, &cable, buf, sizeof(buf));
    if (n == 0) return;

    uint32_t head = s_rx_head;
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t next = (head + 1) & kRxRingMask;
      if (next == s_rx_tail) break;   // full: drop rather than block
      s_rx_ring[head] = buf[i];
      head = next;
    }
    s_rx_head = head;
    drained += n;
  }
}

void tuh_midi_tx_cb(uint8_t dev_addr) {
  (void)dev_addr;  // TRACT8 never transmits
}

// --- Core 1 entry -------------------------------------------------------

extern "C" void core1_entry(void) {
  // Settling delay before touching the USB controller. The working
  // USB-host cards on this hardware all do this; without it the controller
  // does not always come up cleanly after a reset.
  sleep_ms(150);

  board_init();
  tusb_init();

  while (true) {
    tuh_task();

    // Parse whatever the callback left, now that tuh_task() has returned
    // and the IN endpoint has been re-armed. Doing it here rather than in
    // the callback is the whole point - see s_rx_ring.
    while (s_rx_tail != s_rx_head) {
      ParseByte(s_rx_ring[s_rx_tail]);
      s_rx_tail = (s_rx_tail + 1) & kRxRingMask;
    }
  }
}
