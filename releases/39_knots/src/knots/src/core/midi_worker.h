#pragma once

#include <cstdint>

#include "knots/src/core/shared_state.h"

namespace mtws {

class MIDIWorker {
 public:
  MIDIWorker();

  void Init();
  void Poll();
  MidiState Snapshot() const;

 private:
  // Handles one short MIDI message that has already been length-decoded from
  // the USB-MIDI stream. Only channel-1 note on/off and CC74 are used by mtws.
  void HandleShortMessage(uint8_t status, uint8_t data1, uint8_t data2, MidiState& next_state) const;

  // Parses one bounded chunk of raw MIDI bytes and returns how many bytes were
  // consumed into complete messages so any trailing partial message can be kept
  // for the next poll.
  uint32_t ConsumeMIDIBytes(const uint8_t* bytes, uint32_t count, MidiState& next_state) const;

  MidiState state_;
  uint8_t pending_bytes_[3];
  uint8_t pending_count_;
};

}  // namespace mtws
