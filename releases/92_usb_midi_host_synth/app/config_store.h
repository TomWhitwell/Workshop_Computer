// Persistent CardConfig + ExtConfig and flash save coalescing.
#pragma once

#include "protocol.h"

#include <cstdint>

extern CardConfig g_config;
extern ExtConfig g_ext;
extern volatile bool g_flashSaveAckPending;
extern volatile uint8_t g_flashSaveAckCmd;

void applyMapDefaults();
void applyDefaults();
void sanitizeExtConfig(ExtConfig &ext);
void loadConfigFromFlash();
void requestSaveToFlash(uint8_t ackCmd);
void serviceFlashSaveRequest();
void applyConfigBytes(const uint8_t *data, uint32_t size);
