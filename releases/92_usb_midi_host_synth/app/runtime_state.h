// Cross-cutting runtime volatiles shared by USB, panel, and audio.
#pragma once

#include <cstdint>

extern volatile uint8_t g_noteNumA;
extern volatile uint8_t g_noteNumB;
extern volatile bool g_gateA;
extern volatile bool g_gateB;
extern volatile int16_t g_pitchBendA;
extern volatile int16_t g_pitchBendB;

extern volatile bool g_midiConnected;
extern volatile bool g_midiActivity;
extern volatile bool g_webLinked;
extern volatile uint32_t g_configSavedFlashTimer;

extern volatile bool g_cvOutsCalibrated;

extern volatile int32_t g_panelMain;
extern volatile int32_t g_panelX;
extern volatile int32_t g_panelY;
extern volatile uint8_t g_panelSwitch;
extern volatile bool g_panelStream;
extern volatile uint8_t g_appMode; // AppMode
extern volatile uint8_t g_setupSlot;

// Device-mode: SETUP learn event waiting to be SysEx'd to the editor.
extern volatile bool g_learnNotifyPending;
extern volatile uint8_t g_learnNotifySlot;
extern volatile uint8_t g_learnNotifySrc;
extern volatile uint8_t g_learnNotifyChan;
extern volatile uint8_t g_learnNotifyId;

extern uint8_t g_midiDevAddr;
extern volatile bool g_isUsbHost;
