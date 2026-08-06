#include "knots/src/core/midi_worker.h"

#include "tusb.h"

namespace mtws {

namespace {

constexpr uint32_t kMaxMIDIPollBytes = 64U;

// Returns the byte length of one short MIDI message including the status byte.
// SysEx and unknown system messages fall back to 1 so the caller can skip
// forward until the next status byte without building a full general parser.
inline uint32_t MIDIMessageLength(uint8_t status) {
  switch (status & 0xF0U) {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
      return 3U;

    case 0xC0:
    case 0xD0:
      return 2U;

    case 0xF0:
      switch (status) {
        case 0xF1:
        case 0xF3:
          return 2U;

        case 0xF2:
          return 3U;

        default:
          return 1U;
      }

    default:
      return 1U;
  }
}

}  // namespace

MIDIWorker::MIDIWorker() {
  state_.note_active = false;
  state_.current_note = 60;
  state_.last_note = 60;
  state_.cc74_value = 0;
  state_.note_on_counter = 0;
  state_.clock_tick_count = 0;
  pending_bytes_[0] = 0;
  pending_bytes_[1] = 0;
  pending_bytes_[2] = 0;
  pending_count_ = 0;
}

void MIDIWorker::Init() {
  tusb_init();
}

void MIDIWorker::HandleShortMessage(uint8_t status, uint8_t data1, uint8_t data2, MidiState& next_state) const {
  if ((status & 0x80U) == 0U) return;

  // System realtime messages (0xF8-0xFF) carry no channel and must be
  // handled before the channel filter below.
  if (status == 0xF8U) {
    next_state.clock_tick_count++;
    return;
  }

  uint8_t status_nibble = status & 0xF0U;
  uint8_t channel = status & 0x0FU;

  // Channel-1 strict. MIDI channel 1 is zero-based channel 0.
  if (channel != 0U) return;

  switch (status_nibble) {
    case 0x90: {  // note on
      uint8_t note = data1 & 0x7FU;
      uint8_t velocity = data2 & 0x7FU;
      if (velocity == 0U) {
        if (next_state.note_active && note == next_state.current_note) {
          next_state.note_active = false;
        }
      } else {
        next_state.note_active = true;
        next_state.current_note = note;
        next_state.last_note = note;
        next_state.note_on_counter++;
      }
      break;
    }

    case 0x80: {  // note off
      uint8_t note = data1 & 0x7FU;
      if (next_state.note_active && note == next_state.current_note) {
        next_state.note_active = false;
      }
      break;
    }

    case 0xB0: {  // control change
      uint8_t controller = data1 & 0x7FU;
      if (controller == 74U) {
        next_state.cc74_value = data2 & 0x7FU;
      }
      break;
    }

    default:
      break;
  }
}

uint32_t MIDIWorker::ConsumeMIDIBytes(const uint8_t* bytes, uint32_t count, MidiState& next_state) const {
  uint32_t index = 0;
  while (index < count) {
    uint8_t status = bytes[index];
    if ((status & 0x80U) == 0U) {
      ++index;
      continue;
    }

    const uint32_t message_length = MIDIMessageLength(status);
    if (index + message_length > count) {
      break;
    }

    const uint8_t data1 = (message_length > 1U) ? bytes[index + 1U] : 0U;
    const uint8_t data2 = (message_length > 2U) ? bytes[index + 2U] : 0U;
    HandleShortMessage(status, data1, data2, next_state);
    index += message_length;
  }
  return index;
}

void MIDIWorker::Poll() {
  tud_task();

  if (tud_midi_available() == 0U) return;

  MidiState next_state = state_;
  uint8_t buffer[kMaxMIDIPollBytes + 3U];
  uint32_t byte_count = pending_count_;
  for (uint32_t i = 0; i < pending_count_; ++i) {
    buffer[i] = pending_bytes_[i];
  }

  // Read at most one chunk per control tick so MIDI traffic cannot stretch the
  // control loop indefinitely. This still batches multiple short messages.
  const uint32_t bytes_read = tud_midi_stream_read(buffer + byte_count, kMaxMIDIPollBytes);
  byte_count += bytes_read;

  const uint32_t consumed = ConsumeMIDIBytes(buffer, byte_count, next_state);
  pending_count_ = uint8_t(byte_count - consumed);
  for (uint32_t i = 0; i < pending_count_; ++i) {
    pending_bytes_[i] = buffer[consumed + i];
  }
  state_ = next_state;
}

MidiState MIDIWorker::Snapshot() const {
  return state_;
}

}  // namespace mtws
