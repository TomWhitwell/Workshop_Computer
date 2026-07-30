//
// Copyright(C) 2026 ws-doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//   Music Thing Modular Workshop System Computer hardware backend:
//   MCP4822 SPI DAC audio out, mux'd ADC panel controls, pulse I/O,
//   CV outs and LEDs. Pin map follows the ComputerCard reference
//   library by Chris Johnson.
//

#ifndef WS_HW_H
#define WS_HW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// ---- pins (Workshop System Computer, Rev 1) ----
#define WS_PULSE1_IN_PIN     2   // inverted by input transistor
#define WS_PULSE2_IN_PIN     3
#define WS_NORM_PROBE_PIN    4
#define WS_PULSE1_OUT_PIN    8   // inverted driver
#define WS_PULSE2_OUT_PIN    9
#define WS_LED_FIRST_PIN    10   // 6 LEDs on 10..15
#define WS_EEPROM_SDA_PIN   16
#define WS_EEPROM_SCL_PIN   17
#define WS_DAC_SCK_PIN      18
#define WS_DAC_TX_PIN       19
#define WS_DAC_CS_PIN       21
#define WS_CV2_OUT_PIN      22   // PWM slice 3 chan B
#define WS_CV1_OUT_PIN      23   // PWM slice 3 chan A... (23=B,22=A: 22 even=A)
#define WS_MUX_A_PIN        24
#define WS_MUX_B_PIN        25
#define WS_ADC_AUDIO_R_PIN  26   // ADC0 (unused here)
#define WS_ADC_AUDIO_L_PIN  27   // ADC1 (unused here)
#define WS_ADC_MUX_KNOBS    28   // ADC2: Main/X/Y/Switch via mux
#define WS_ADC_MUX_CV       29   // ADC3: CV1/CV2 via mux

// PWM slice used purely as the 48kHz audio sample timer (no pin output)
#define WS_AUDIO_PWM_SLICE   0

struct audio_buffer_pool;

// GPIO/SPI/PWM/ADC bring-up; call once after the system clock is set.
void ws_hw_early_init(void);

// Attach the doom mixer's producer pool and start the 48kHz DAC interrupt.
void ws_audio_init(struct audio_buffer_pool *pool);

// Poll panel controls, post doom key events, refresh CV/pulse/LED outputs.
// Call from the game loop (core 0) once per tic.
void ws_input_poll(void);

// Tap from S_StartSound: fires pulse out 1 (and the audio-in-2 gate) on
// player weapon sounds; returns nonzero when the shot sound should be
// suppressed because audio in 2 replaces it.
int ws_on_player_sound(void *origin, int sfx_id);

// user mix settings, persisted in a spare flash sector
extern volatile uint8_t ws_music_vol; // base music level, 128 = nominal
extern volatile uint8_t ws_sfx_vol;   // effects level, 255 = nominal
extern volatile uint8_t ws_turn_sens; // turn response, 128 = nominal
extern volatile uint8_t ws_move_sens; // move/strafe response, 128 = nominal
void ws_settings_load(void);
void ws_settings_set(int which, uint8_t value); // 0 = music, 1 = sfx (RAM only)
void ws_settings_request_save(void);            // commit to flash next tic
void ws_settings_task(void);                    // performs the requested save

// true when a jack is patched into audio in 1 (external music source)
bool ws_ext_music(void);

#ifdef __cplusplus
}
#endif

#endif
