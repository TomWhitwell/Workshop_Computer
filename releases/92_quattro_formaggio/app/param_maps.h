// Parameter maps, MIDI learn, and channel-message routing.
#pragma once

#include "protocol.h"

#include <cstdint>

extern uint8_t g_learnKnobArmed;
extern uint8_t g_setupSlotPending;
extern uint32_t g_setupSlotDwell;

bool slotMatches(const MapSlot &s, uint8_t chan, uint8_t type, uint8_t id);
void applySlotValue(uint8_t slot, uint8_t value, bool fromLearn);
void learnToSlot(uint8_t slot, uint8_t srcType, uint8_t chan, uint8_t id);
void applyKnobMappedSlots();
void resetKnobMappedBaseline();
void armKnobLearn();
// SETUP: if armed and X/Y moved enough, learn knob source to current slot.
void serviceKnobLearnGesture();
void handleChannelMessage(const uint8_t *buf);

uint8_t mapCcVoice(uint8_t v);
