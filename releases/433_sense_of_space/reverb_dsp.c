/*
  Integer-arithmetic reverb for RP2040

  Algorithm based on Dattorro paper:
     https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf
     https://github.com/el-visio/dattorro-verb

  Filters inside and prior to reverb 'tank' are added from the above, to give
    high or low pass filtering

  Implemented usign integer arithmetic, for fast execution on RP2040. Products implemented 
  as (a*b)>>16 (for int32_t a,b), which benefits from single-cycle multiply on RP2040.
  Shift operations on negative are undefined behaviour in C, but gcc compiles
  to ASR instruction, which divides negative numbers by two, rounding towards
  negative infinity.
  In the allpass filter, (a*(b>>4))>>12 is used, to give more audio headroom without wrapping
*/

#include "reverb_dsp.h"

#include <stdlib.h>
#include <string.h>


// Clamp value between min and max 
int32_t __not_in_flash_func(clamp)(int32_t x, int32_t min, int32_t max)
{
	if (x < min)
		return min;

	if (x > max)
		return max;

	return x;
}

// Set delay amount 
void __not_in_flash_func(buffer_setDelay)(buffer *db, uint16_t tap, uint16_t delay)
{
	db->readOffset[tap] = db->mask + 1 - delay;
}

////////////////////////////////////////
// Delay functions
void buffer_init(buffer *db, uint16_t delay)
{
	memset(db, 0, sizeof(delay));

	// Calculate number of bits
	int16_t x = delay;
	uint16_t numBits = 0;
	while (x)
	{
		numBits++;
		x >>= 1;
	}

	// Buffer size is always 2^n, to avoid
	uint16_t bufferSize = 1 << numBits;

	// Allocate buffer
	db->buffer = malloc(bufferSize * sizeof(int32_t));
	if (!db->buffer)
		return;

	// Clear buffer
	memset(db->buffer, 0, bufferSize * sizeof(int32_t));

	// Create bitmask for fast wrapping of the circular buffer
	db->mask = bufferSize - 1;
	buffer_setDelay(db, TAP_MAIN, delay);
}

void buffer_clear(buffer *db)
{
	uint16_t size = db->mask + 1;
	memset(db->buffer, 0, size * sizeof(int32_t));
}

void buffer_delete(buffer *db)
{
	free(db->buffer);
	db->buffer = 0;
}

// Write input value into buffer, read delayed output 
int32_t __not_in_flash_func(delay_process)(buffer *db, uint16_t t, int32_t in)
{
	db->buffer[t & db->mask] = in;
	return db->buffer[(t + db->readOffset[TAP_MAIN]) & db->mask];
}

// Write value into delay buffer 
void __not_in_flash_func(buffer_write)(buffer *db, uint16_t t, int32_t in)
{
	db->buffer[t & db->mask] = in;
}

// Read delayed output value 
int32_t __not_in_flash_func(buffer_read)(buffer *db, uint16_t tapId, uint16_t t)
{
	return db->buffer[(t + db->readOffset[tapId]) & db->mask];
}

typedef int32_t multiplier_type;

// Allpass filter 
int32_t __not_in_flash_func(allpass_process)(buffer *db, uint16_t t, int32_t gain, int32_t in)
{
	// give 4 more bits of headroom in multiply than elsewhere
	// since gain is not a critical parameter (we can cope with 12-bit accuracy)
	// and overflow was fairly frequent without this
	gain >>= 4;

	int32_t delayed = buffer_read(db, TAP_MAIN, t);
	in += ((delayed * -gain + 2048) >> 12);
	// in = clamp(in, -16383, 16383);

	buffer_write(db, t, in);
	return delayed + ((in * gain + 2048) >> 12);
}


/*
Single-pole IIR lowpass
Cutoff frequency as function of 'b' parameter is

f_c = -(f_s/(2 pi)) * ln(1-b/65536)

where f_s is the sample rate
*/
int32_t __not_in_flash_func(lowpass_process)(int32_t *out, int32_t b, int32_t in)
{
	*out += (((in - *out) * b + 32768) >> 16);
	return *out;
}

// Highpass, as above 
int32_t __not_in_flash_func(highpass_process)(int32_t *out, int32_t b, int32_t in)
{
	*out += (((in - *out) * b + 32768) >> 16);
	return in - *out;
}


// Set decay amount and calculate related decay diffusion 2 amount. Value is  65536 * float value 
void __not_in_flash_func(reverb_set_size)(reverb *v, int32_t size)
{
	int32_t s = size >> 1;
	int32_t value = 65500 - (((32750 - s) * (32750 - s)) >> 14);
	v->decayAmount = value;
	v->decayDiffusion2Amount = clamp(value + 9830, 16384, 32768);

	// set pre-delay amount. This is the line that causes pitch shift when knob x is altered
	buffer_setDelay(&v->preDelay, TAP_MAIN, size >> 4);
}

// Set decay amount and calculate related decay diffusion 2 amount. Value is  65536 * float value 
void __not_in_flash_func(reverb_set_freeze_size)(reverb *v, int32_t size, int32_t mult)
{
	// Fixed decay amount and diffusion
	int32_t s = size >> 1;
	int32_t value = 65500 - (((32750 - s) * (32750 - s)) >> 14);
	v->decayAmount = (65536 * (256 - mult) + value * mult) >> 8;
	v->decayDiffusion2Amount = clamp(value + 9830, 16384, 32768);

	// set pre-delay amount. This is the line that causes pitch shift when knob x is altered
	buffer_setDelay(&v->preDelay, TAP_MAIN, size >> 4);
}


// Set frequency response inside reverberator 
void __not_in_flash_func(reverb_set_tilt)(struct sreverb *v, int32_t value)
{
	value -= 32768;
	v->lpf = (value < 0);

	value = (value * value) >> 14;

	v->dampingLPF = 65535 - value;
	v->preFilterLPF = 65535 - value;
	v->dampingHPF = value >> 1;
	v->preFilterHPF = value >> 1;
}

// Initialise reverb instance
void initialise(reverb *v)
{
	memset(v, 0, sizeof(reverb));

	buffer_init(&v->preDelay, 4100);

	buffer_init(&v->inDiffusion[0], 142);
	buffer_init(&v->inDiffusion[1], 107);
	buffer_init(&v->inDiffusion[2], 379);
	buffer_init(&v->inDiffusion[3], 277);

	buffer_init(&v->decayDiffusion1[0], 672); // + EXCURSION
	v->decayDiffusion1[0].readOffset[TAP_OUT1] = v->decayDiffusion1[0].readOffset[TAP_MAIN];

	buffer_init(&v->preDampingDelay[0], 4453);
	buffer_setDelay(&v->preDampingDelay[0], TAP_OUT1, 353);
	buffer_setDelay(&v->preDampingDelay[0], TAP_OUT2, 3627);
	buffer_setDelay(&v->preDampingDelay[0], TAP_OUT3, 1990);

	buffer_init(&v->decayDiffusion2[0], 1800);
	buffer_setDelay(&v->decayDiffusion2[0], TAP_OUT1, 187);
	buffer_setDelay(&v->decayDiffusion2[0], TAP_OUT2, 1228);

	buffer_init(&v->postDampingDelay[0], 3720);
	buffer_setDelay(&v->postDampingDelay[0], TAP_OUT1, 1066);
	buffer_setDelay(&v->postDampingDelay[0], TAP_OUT2, 2673);

	buffer_init(&v->decayDiffusion1[1], 908); // + EXCURSION
	v->decayDiffusion1[1].readOffset[TAP_OUT1] = v->decayDiffusion1[1].readOffset[TAP_MAIN];

	buffer_init(&v->preDampingDelay[1], 4217);
	buffer_setDelay(&v->preDampingDelay[1], TAP_OUT1, 266);
	buffer_setDelay(&v->preDampingDelay[1], TAP_OUT2, 2974);
	buffer_setDelay(&v->preDampingDelay[1], TAP_OUT3, 2111);

	buffer_init(&v->decayDiffusion2[1], 2656);
	buffer_setDelay(&v->decayDiffusion2[1], TAP_OUT1, 335);
	buffer_setDelay(&v->decayDiffusion2[1], TAP_OUT2, 1913);

	buffer_init(&v->postDampingDelay[1], 3163);
	buffer_setDelay(&v->postDampingDelay[1], TAP_OUT1, 121);
	buffer_setDelay(&v->postDampingDelay[1], TAP_OUT2, 1996);

	// Default settings
	v->modulate = 0x7ff;
	v->modulatedist = 16;

	v->lpf = 1;
	v->tapModVal = 0;
	v->tapDir = -1;
	buffer_setDelay(&v->preDelay, TAP_MAIN, 400);
	v->preFilterHPF = 49152;
	v->preFilterLPF = 49152;
	v->inputDiffusion1Amount = 49152;
	v->inputDiffusion2Amount = 40960;
	v->decayDiffusion1Amount = 45875;
	reverb_set_size(v, 49152);
	reverb_set_tilt(v, 62259);
}

// Get pointer to initialised reverb instance
reverb *reverb_create(void)
{
	reverb *v = malloc(sizeof(reverb));
	if (!v)
		return NULL;

	initialise(v);
	return v;
}

// Free resources and delete reverb instance
void reverb_delete(reverb *v)
{
	// Free delay buffers
	buffer_delete(&v->preDelay);

	for (int i = 0; i < 4; i++)
	{
		buffer_delete(&v->inDiffusion[i]);
	}

	for (int i = 0; i < 2; i++)
	{
		buffer_delete(&v->decayDiffusion1[i]);
		buffer_delete(&v->preDampingDelay[i]);
		buffer_delete(&v->decayDiffusion2[i]);
		buffer_delete(&v->postDampingDelay[i]);
	}

	free(v);
}

// Resets buffers to zero 
void __not_in_flash_func(reverb_reset)(reverb *v)
{
	if (!v) return;

	// Free delay buffers
	buffer_clear(&v->preDelay);

	for (int i = 0; i < 4; i++)
	{
		buffer_clear(&v->inDiffusion[i]);
	}

	for (int i = 0; i < 2; i++)
	{
		buffer_clear(&v->decayDiffusion1[i]);
		buffer_clear(&v->preDampingDelay[i]);
		buffer_clear(&v->decayDiffusion2[i]);
		buffer_clear(&v->postDampingDelay[i]);
	}

	v->preFilterH[0] = 0;
	v->preFilterL[0] = 0;
	v->preFilterH[1] = 0;
	v->preFilterL[1] = 0;
	v->acCouplingHPF = 0;
	v->dampingH[0] = 0;
	v->dampingH[1] = 0;
	v->dampingL[0] = 0;
	v->dampingL[1] = 0;
}


// Process mono audio
void __not_in_flash_func(reverb_process)(reverb *v, int32_t in)
{
	int32_t x, x1, x2, x3;

	// Every 2048 samples (23.4Hz at 48kHz sample rate), alter allpass length by 1 sample
	// Triangle wave modulation, reducing and increasing length by 16 samples

	in = clamp(in, -16384, 16383);

	if (v->modulate && (v->t & v->modulate) == 0)
	{
		v->decayDiffusion1[0].readOffset[TAP_MAIN] = v->decayDiffusion1[0].readOffset[TAP_OUT1] + v->tapModVal;
		v->decayDiffusion1[1].readOffset[TAP_MAIN] = v->decayDiffusion1[1].readOffset[TAP_OUT1] + v->tapModVal;

		v->tapModVal += v->tapDir;
		if (v->tapModVal <= -v->modulatedist)
		{
			v->tapDir = 1;
		}
		if (v->tapModVal >= 0)
		{
			v->tapDir = -1;
		}
	}

	x = delay_process(&v->preDelay, v->t, in); // pre-delay

	x2 = lowpass_process(&v->preFilterL[0], v->preFilterLPF, x); // pre-filter
	x3 = highpass_process(&v->preFilterH[0], v->preFilterHPF, x);
	x2 = clamp(x2, -16383, 16383);
	x3 = clamp(x3, -16383, 16383);

	x = (v->lpf) ? x2 : x3;

	// Input diffusion
	x = allpass_process(&v->inDiffusion[0], v->t, v->inputDiffusion1Amount, x);
	x = allpass_process(&v->inDiffusion[1], v->t, v->inputDiffusion1Amount, x);
	x = allpass_process(&v->inDiffusion[2], v->t, v->inputDiffusion2Amount, x);
	x = allpass_process(&v->inDiffusion[3], v->t, v->inputDiffusion2Amount, x);
	//	x = clamp(x, -16383, 16383);

	// Throughout this repeated section, occasionally clamp inputs to be within intended range,
	// to try to avoid integer wrapping.
	// This almost certainly doesn't eliminate wrapping entirely, but helps avoid it
	for (int i = 0; i < 2; i++)
	{

		// Add cross feedback
		x1 = x + ((buffer_read(&v->postDampingDelay[1 - i], TAP_MAIN, v->t) * v->decayAmount) >> 16);
		x1 = clamp(x1, -16383, 16383);

		// 11 Hz DC-blocking HPF once within figure-of-eight
		if (i == 0)
		{
			x1 = highpass_process(&v->acCouplingHPF, 100, x1);
			x1 = clamp(x1, -16383, 16383);
		}
		
		// Process single half of the tank
		x1 = allpass_process(&v->decayDiffusion1[i], v->t, -v->decayDiffusion1Amount, x1);
		//		x1 = clamp(x1, -16383, 16383);

		x1 = delay_process(&v->preDampingDelay[i], v->t, x1);

		// Primative shelf filter for damping high or low frequencies
		// Proper shelf topology would almost certainly be preferable
		x2 = lowpass_process(&v->dampingL[i], v->dampingLPF, x1);
		x3 = highpass_process(&v->dampingH[i], v->dampingHPF, x1);
		x2 = clamp(x2, -16383, 16383);
		x3 = clamp(x3, -16383, 16383);
		x1 = (7 * x1 + ((v->lpf) ? x2 : x3)) >> 3; // turn filter into shelf
		x1 = clamp(x1, -16383, 16383);

		x1 = ((x1 * v->decayAmount) >> 16); // attenuate

		x1 = allpass_process(&v->decayDiffusion2[i], v->t, v->decayDiffusion2Amount, x1);
		//		x1 = clamp(x1, -16383, 16383);

		buffer_write(&v->postDampingDelay[i], v->t, x1);
	}

	// Increment delay position
	v->t++;
}

// Get left channel reverb
int32_t __not_in_flash_func(reverb_get_left)(reverb *v)
{
	int32_t a;
	a = buffer_read(&v->preDampingDelay[1], TAP_OUT1, v->t);
	a += buffer_read(&v->preDampingDelay[1], TAP_OUT2, v->t);
	a -= buffer_read(&v->decayDiffusion2[1], TAP_OUT2, v->t);
	a += buffer_read(&v->postDampingDelay[1], TAP_OUT2, v->t);
	a -= buffer_read(&v->preDampingDelay[0], TAP_OUT3, v->t);
	a -= buffer_read(&v->decayDiffusion2[0], TAP_OUT1, v->t);
	a += buffer_read(&v->postDampingDelay[0], TAP_OUT1, v->t);
	return a;
}

// Get right channel reverb
int32_t __not_in_flash_func(reverb_get_right)(reverb *v)
{
	int32_t a;
	a = buffer_read(&v->preDampingDelay[0], TAP_OUT1, v->t);
	a += buffer_read(&v->preDampingDelay[0], TAP_OUT2, v->t);
	a -= buffer_read(&v->decayDiffusion2[0], TAP_OUT2, v->t);
	a += buffer_read(&v->postDampingDelay[0], TAP_OUT2, v->t);
	a -= buffer_read(&v->preDampingDelay[1], TAP_OUT3, v->t);
	a -= buffer_read(&v->decayDiffusion2[1], TAP_OUT1, v->t);
	a += buffer_read(&v->postDampingDelay[1], TAP_OUT1, v->t);
	return a;
}
