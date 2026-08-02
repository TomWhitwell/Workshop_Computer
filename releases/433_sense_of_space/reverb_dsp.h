#ifndef REVERB_DSP
#define REVERB_DSP

#include "pico/stdlib.h"

#include <stdint.h>
struct sreverb;

int32_t __not_in_flash_func(clamp)(int32_t x, int32_t min, int32_t max);

// Get pointer to initialized reverb struct 
struct sreverb *reverb_create(void);

// Silence reverb by zeroing state 
void reverb_reset(struct sreverb *v);

// Free resources and delete reverb instance 
void reverb_delete(struct sreverb *v);

// Set reverb parameters 
void __not_in_flash_func(reverb_setPreDelay)(struct sreverb *v, int16_t value);
void __not_in_flash_func(reverb_setDecayDiffusion)(struct sreverb *v, int32_t value);
void __not_in_flash_func(reverb_set_size)(struct sreverb *v, int32_t size);
void __not_in_flash_func(reverb_set_freeze_size)(struct sreverb *v, int32_t size, int32_t mult);
void __not_in_flash_func(reverb_set_tilt)(struct sreverb *v, int32_t value);

// Send mono input into reverbation tank 
void __not_in_flash_func(reverb_process)(struct sreverb *v, int32_t in);

// Get reverbated signal for left channel 
int32_t __not_in_flash_func(reverb_get_left)(struct sreverb *v);

// Get reverbated signal for right channel 
int32_t __not_in_flash_func(reverb_get_right)(struct sreverb *v);

enum
{
	TAP_MAIN = 0,
	TAP_OUT1,
	TAP_OUT2,
	TAP_OUT3,
	MAX_TAPS
};

// buffer, for delays and allpass filters
typedef struct sbuffer
{
	int32_t *buffer;
	uint16_t mask; // Mask for fast array index wrapping in read / write
	uint16_t readOffset[MAX_TAPS]; // read offsets
} buffer;

// reverb context 
typedef struct sreverb
{
	buffer preDelay;
	int32_t preFilterH[2];
	int32_t preFilterL[2];
	int32_t acCouplingHPF;

	// input diffusors
	buffer inDiffusion[4]; // APF

	// Reverbation tank left / right halves
	buffer decayDiffusion1[2]; // APF
	buffer preDampingDelay[2]; // Delay
	int32_t dampingH[2]; // HPF
	int32_t dampingL[2]; // LPF
	buffer decayDiffusion2[2]; // APF
	buffer postDampingDelay[2]; // Delay

	// -- Reverb settings --
	int32_t preFilterLPF;
	int32_t preFilterHPF;

	int32_t inputDiffusion1Amount;
	int32_t inputDiffusion2Amount;

	int32_t decayDiffusion1Amount;
	int32_t dampingLPF;
	int32_t dampingHPF;
	int32_t decayAmount;
	int32_t decayDiffusion2Amount; // Automatically set in reverb_setDecay

	uint8_t lpf; // boolean
	uint16_t modulate; // mask
	int modulatedist;

	int32_t tapModVal, tapDir;
	// Cycle count
	uint16_t t;
} reverb;

#endif
