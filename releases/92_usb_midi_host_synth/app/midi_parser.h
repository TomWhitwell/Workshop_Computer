// Running-status MIDI byte parser → handleChannelMessage.
#pragma once

#include <cstdint>

void parseMidiByte(uint8_t b);
void parseMidiStream(const uint8_t *data, uint32_t len);
