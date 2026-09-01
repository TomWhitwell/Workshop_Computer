// Arpeggiator over voice A held notes (shares g_midiCs with voices).
#pragma once

#include <cstdint>

extern uint8_t g_arpNoteOut;
extern bool g_arpGateOut;

void arpClearUnlocked();
uint32_t arpPeriodSamples(uint8_t mode);
void rebuildArpList();
void setArpMode(uint8_t a, bool announce);

// Advances arp under g_midiCs; updates noteA/gateA when active.
void arpTick(uint8_t &noteA, bool &gateA);
