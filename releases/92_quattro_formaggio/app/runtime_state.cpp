#include "runtime_state.h"

volatile uint8_t g_noteNumA = 60;
volatile uint8_t g_noteNumB = 60;
volatile bool g_gateA = false;
volatile bool g_gateB = false;
volatile int16_t g_pitchBendA = 0;
volatile int16_t g_pitchBendB = 0;

volatile bool g_midiConnected = false;
volatile bool g_midiActivity = false;
volatile bool g_webLinked = false;
volatile uint32_t g_configSavedFlashTimer = 0;

volatile bool g_cvOutsCalibrated = false;

volatile int32_t g_panelMain = 0;
volatile int32_t g_panelX = 0;
volatile int32_t g_panelY = 0;
volatile uint8_t g_panelSwitch = 1;
volatile bool g_panelStream = false;
volatile uint8_t g_appMode = 0;
volatile uint8_t g_setupSlot = 0;

volatile bool g_learnNotifyPending = false;
volatile uint8_t g_learnNotifySlot = 0;
volatile uint8_t g_learnNotifySrc = 0;
volatile uint8_t g_learnNotifyChan = 0;
volatile uint8_t g_learnNotifyId = 0;

uint8_t g_midiDevAddr = 0;
volatile bool g_isUsbHost = false;
