/*
Reverb+ for Music Thing Workshop System Computer

Chris Johnson 2024

Interrupt service routine buffer_full() called at 48kHz by ADC DMA (ADC runs at 384kHz)
ADC samples both audio channels, along with knobs and CV in, and sends outputs to DAC (via DMA)
at 48kHz.

Reverb DSP is run within the buffer_full() ISR.  DSP runs in around 12us, out of 20.8us per sample.

The function of knobs, CV and Pulse input/output are controlled by MIDI SysEx commands.
*/

#define ENABLE_MIDI
// #define ENABLE_UART_DEBUGGING
// #define ENABLE_GPIO_DEBUGGING

#ifdef ENABLE_MIDI
#include "bsp/board.h"
#include "tusb.h"
#endif

#ifdef ENABLE_UART_DEBUGGING
// Use debug pins as UART
#include <stdio.h>
#define debug(f_, ...) printf((f_), __VA_ARGS__)
#define debugp(f_) printf(f_)
#define debug_pin(pin_, val_)
#elifdef ENABLE_GPIO_DEBUGGING
// Use UART pins as GPIO outputs
#define debug(f_, ...)
#define debugp(f_)
#define debug_pin(pin_, val_) gpio_put(pin_, val_)
#else
// No debugging
#define debug(f_, ...)
#define debugp(f_)
#define debug_pin(pin_, val_)
#endif


#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "sysex_sentences.h"
#include "reverb_dsp.h"
#include <float.h>
#include <math.h>
#include <string.h>

#include "usb_midi_host.h"

// Variables for passing debug info from audio thread to usb thread
volatile uint32_t pf1 = 0, pf2 = 0, pfflag = 0;

#include "bernoulligate.h"
#include "clock.h"
#include "computer.h"
#include "divider.h"
#include "noise_gate.h"
#include "turingmachine.h"


#define MIDI_NOTE_ON 0x90
#define MIDI_NOTE_OFF 0x80
#define MIDI_CC 0xB0
#define MIDI_PITCHBEND 0xE0

#define OPT_PITCH OPT_PITCH_OF_MIDI_NOTE


uint32_t configFlashAddr = (PICO_FLASH_SIZE_BYTES - 4096) - (PICO_FLASH_SIZE_BYTES - 4096) % 4096;

uint8_t config[CONFIG_LENGTH];
uint8_t configBuffer[CONFIG_LENGTH]; // config that is filled out packet-by-packet when receiving data
uint8_t packet[CONFIG_LENGTH + 10];

// The default configuration, can be reset without USB connection by holding down momentary switch while powering on/resetting.
//                          v     v     v     v        v           v           v           v           v              v              v        v  v      v        v     v
uint8_t defaultConfig[] = { 0, 2, 1, 1, 2, 0, 3, 0, 0, 0, 1, 0, 0, 1, 6, 0, 0, 8, 1, 1, 0, 5, 0, 0, 0, 5, 1, 2, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 8, 5, 10, 1, 0, 0, 1, 0, 5, 10, SYSEX_CONFIGURED_MARKER };

// The various utilities
clock clk[2];
divider pulseout1_divider, pulseout2_divider, cvout1_divider, cvout2_divider, tm_divider, bg_divider;
turing_machine tm;
bernoulli_gate bg;
noise_gate ng;

// lookup for powers of two
uint32_t pow2_128[128];

volatile int32_t knobs[4] = { 0, 0, 0, 0 }; // 0-4095
volatile bool pulse[2] = { 0, 0 };
volatile bool last_pulse[2] = { 0, 0 };
volatile int32_t cv[2] = { 0, 0 }; // -2047 - 2048
volatile int16_t dacOutL = 0, dacOutR = 0;
volatile int16_t adcInL = 0x800, adcInR = 0x800;

volatile uint8_t mxPos = 0; // external multiplexer value

// The ADC (/DMA) run mode, used to stop DMA in a known state before writing to flash
#define RUN_ADC_MODE_RUNNING 0
#define RUN_ADC_MODE_REQUEST_ADC_STOP 1
#define RUN_ADC_MODE_ADC_STOPPED 2
#define RUN_ADC_MODE_REQUEST_ADC_RESTART 3
volatile uint8_t runADCMode = RUN_ADC_MODE_RUNNING;


// Buffers that DMA reads into / out of
uint16_t ADC_Buffer[2][8];
uint16_t SPI_Buffer[2][2];

uint8_t adc_dma, spi_dma; // DMA ids

unsigned allLedFlashTimer = 0;

volatile uint8_t midiDeviceConnected=0;
int32_t midiDeviceOutputCable=0;
uint8_t midiDeviceAddress=0;

int32_t midiPitchbend = 0;
int32_t midiNotenum = 0;

uint8_t dmaPhase = 0;

bool frozenReverb = false;


void process_sample();


// Convert signed int16 value into data string for DAC output
uint16_t __not_in_flash_func(dacval)(int16_t value, uint16_t dacChannel)
{
	return (dacChannel | 0x3000) | (((uint16_t)((value & 0x0FFF) + 0x800)) & 0x0FFF);
}

// Return pseudo-random number
uint32_t __not_in_flash_func(rnd)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed;
}



// Return pseudo-random bit for normalisation probe
uint32_t __not_in_flash_func(next_norm_probe)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 31;
}

// Per-audio-sample ISR, called when two sets of ADC samples have been collected from all four inputs
void __not_in_flash_func(buffer_full)()
{
	debug_pin(DEBUG_2, true);
	static int mux_state = 0;
	static int norm_probe_count = 0;

	#ifdef ENABLE_UART_DEBUGGING
	static int np = 0, np1 = 0, np2 = 0;
	#endif
	
	// Internal variables for IIR filters on knobs/cv
	static volatile int32_t knobssm[4] = { 0, 0, 0, 0 };
	static volatile int32_t cvsm[2] = { 0, 0 };

	adc_select_input(0);

	// Advance external mux to next state
	int next_mux_state = (mux_state + 1) & 0x3;
	gpio_put(MX_A, next_mux_state & 1);
	gpio_put(MX_B, next_mux_state & 2);

	// Set up new writes into next buffer
	uint8_t cpuPhase = dmaPhase;
	dmaPhase = 1 - dmaPhase;

	dma_hw->ints0 = 1u << adc_dma; // reset adc interrupt flag
	dma_channel_set_write_addr(adc_dma, ADC_Buffer[dmaPhase], true); // start writing into new buffer
	dma_channel_set_read_addr(spi_dma, SPI_Buffer[dmaPhase], true); // start reading from new buffer

	////////////////////////////////////////
	// Collect various inputs and put them in variables for the DSP

	// Set CV inputs, with ~100Hz LPF on CV input
	int cvi = mux_state % 2;
	cvsm[cvi] = (127 * (cvsm[cvi]) + 16 * ADC_Buffer[cpuPhase][7]) >> 7;
	cv[cvi] = 2048 - (cvsm[cvi] >> 4);


	// Set audio inputs, by averaging the two samples collected
	adcInR = ((ADC_Buffer[cpuPhase][0] + ADC_Buffer[cpuPhase][4]) - 0x1000) >> 1;
	adcInL = ((ADC_Buffer[cpuPhase][1] + ADC_Buffer[cpuPhase][5]) - 0x1000) >> 1;

	// Set pulse inputs
	last_pulse[0] = pulse[0];
	last_pulse[1] = pulse[1];
	pulse[0] = !gpio_get(PULSE_1_INPUT);
	pulse[1] = !gpio_get(PULSE_2_INPUT);

	// Set knobs, with ~100Hz LPF
	int knob = mux_state;
	knobssm[knob] = (127 * (knobssm[knob]) + 16 * ADC_Buffer[cpuPhase][6]) >> 7;
	knobs[knob] = knobssm[knob] >> 4;


	////////////////////////////////////////
	// Run the DSP
	process_sample();

	////////////////////////////////////////
	// Collect DSP outputs and put them in the DAC SPI buffer
	// CV/Pulse outputs are done immediately in ProcessSample

	SPI_Buffer[cpuPhase][0] = dacval(dacOutL, DAC_CHANNEL_A);
	SPI_Buffer[cpuPhase][1] = dacval(dacOutR, DAC_CHANNEL_B);



	mux_state = next_mux_state;

	// Indicate to usb core that we've finished running this sample.
	if (runADCMode == RUN_ADC_MODE_REQUEST_ADC_STOP)
	{
		adc_run(false);
		adc_set_round_robin(0);
		adc_select_input(0);

		runADCMode = RUN_ADC_MODE_ADC_STOPPED;
	}

	norm_probe_count = (norm_probe_count + 1) & 0x1F;
	debug_pin(DEBUG_2, false);
}



////////////////////////////////////////
// Shorthand functions for pulse/CV outputs

volatile bool lastPulseOut1 = false, lastPulseOut2 = false;
volatile int triggerTimer1 = 0, triggerTimer2 = 0;

// Pulse outputs with LEDs
// Pulse output 1 shown on leds[3]. Pulse output 2 shown on leds[5])
void __not_in_flash_func(PulseOut1)(bool val)
{
	if (config[SEN_PULSE_OUT_1] == OPT_GATE)
	{
		gpio_put(leds[3], val);
		gpio_put(PULSE_1_RAW_OUT, !val);
	}
	else
	{
		if (val == true && !lastPulseOut1)
		{
			triggerTimer1 = 100;
			gpio_put(leds[3], true);
			gpio_put(PULSE_1_RAW_OUT, false);
		}
	}
	lastPulseOut1 = val;
}

void __not_in_flash_func(PulseOut2)(bool val)
{
	if (config[SEN_PULSE_OUT_2] == OPT_GATE)
	{
		gpio_put(leds[5], val);
		gpio_put(PULSE_2_RAW_OUT, !val);
	}
	else
	{
		if (val == true && !lastPulseOut2)
		{
			triggerTimer2 = 100;
			gpio_put(leds[5], true);
			gpio_put(PULSE_2_RAW_OUT, false);
		}
	}
	lastPulseOut2 = val;
}

// CV outputs. 0 = max voltage, 1024 = 0V, 2047 = min voltage
void __not_in_flash_func(CVOut1)(uint32_t val)
{
	pwm_set_gpio_level(CV_OUT_1, val);
}

void __not_in_flash_func(CVOut2)(uint32_t val)
{
	pwm_set_gpio_level(CV_OUT_2, val);
}

// CV 'gate' outputs. 0V on 'false', max voltage on 'true'
void __not_in_flash_func(CVOut1Gate)(bool val)
{
	pwm_set_gpio_level(CV_OUT_1, val ? 0 : 1024);
}

void __not_in_flash_func(CVOut2Gate)(bool val)
{
	pwm_set_gpio_level(CV_OUT_2, val ? 0 : 1024);
}



////////////////////////////////////////
// Functions to read continous and tempo type sources from config

int32_t __not_in_flash_func(continuous_source_from_config)(int offset)
{
	int32_t ret = 0;
	switch (config[offset])
	{
	case OPT_MAIN_KNOB:
		ret = knobs[KNOB_MAIN];

		break;
	case OPT_KNOB_X:
		ret = knobs[KNOB_X];
		break;
	case OPT_KNOB_Y:
		ret = knobs[KNOB_Y];
		break;
	case OPT_CV_IN_1:
		ret = cv[0] + 2047;
		break;
	case OPT_CV_IN_2:
		ret = cv[1] + 2047;
		break;
	case OPT_A_CONSTANT:
		ret = ((int32_t)config[offset + 1]) * 41;
		if (ret == 4100) ret = 4095;
		break;
	}

	if (config[offset] <= OPT_KNOB_Y && config[offset + 1] != OPT_ONLY)
	{
		int cvIndex = config[offset + 1] - OPT_PLUS_CV_IN_1;
		if (cvIndex >= 0 && cvIndex <= 1) // should be true always, unless bad data sent from UI
		{
			ret += cv[cvIndex];
		}
	}
	return ret;
}
/*
// Returns tempo in Hz from a "TempoSource" dropdown
float __not_in_flash_func(tempo_source_from_config)(int offset)
{
	if (config[offset] == OPT_A_CONSTANT)
	{
		int val = 100 * config[offset + 1]
		          + 10 * config[offset + 2]
		          + config[offset + 3];
		float fval = val;
		return (config[offset + 4] == OPT_HZ) ? fval : fval * 0.0166666666f;
	}
	else
	{
		// tempo from knob or CV: use exponential mapping
		int32_t int_clock_tempo = continuous_source_from_config(offset);
		return expf(1.9531e-3f * int_clock_tempo - 3.0f);
	}
}
*/


// Return integer part of 2^(in/4096) (approximate)
// 7-bit lookup table (128 entries), 5-bit (32-step) linear interpolation between steps
uint32_t __not_in_flash_func(Pow2)(uint32_t in)
{
	uint32_t oct = in >> 12; // floor(in/4096)

	// Table index and value of lookup
	uint32_t ind1 = (in & 0xFE0) >> 5; // 0xFE0 = 111111100000
	uint32_t val1 = pow2_128[ind1];

	// Next lookup
	uint32_t ind2 = (ind1 + 1) & 0x7F; //
	uint32_t val2 = pow2_128[ind2];
	if (ind2 == 0) val2 <<= 1; // interpolating between end of one octave and the start of another

	// Linearly interpolate with 32 steps
	uint32_t subsample = in & 0x1F;
	uint32_t val = val1 * (32 - subsample) + val2 * subsample;

	val >>= 31 - oct;

	return val;
}

// Returns tempo as increment of a 32-bit wrapping counter, from a "TempoSource" dropdown
uint32_t __not_in_flash_func(tempo_source_from_config_incr)(int offset)
{
	if (config[offset] == OPT_A_CONSTANT)
	{
		// Use this slightly odd way of summing digits for maximum precision
		if (config[offset + 4] == OPT_HZ) // Hz
		{
			return config[offset + 1]*8947849 + config[offset + 2]*894785 + config[offset + 3]*89479;
		}
		else // BPM
		{
			return config[offset + 1]*149131 + config[offset + 2]*14913 + config[offset + 3]*1491;
		}
	}
	else
	{
		// tempo from knob or CV: use exponential mapping
		// Range is from ~0.05 Hz = 1/20Hz to 150Hz
		// That corresponds to an increment of 4454.87 to 13252516.1
		// That's a range of about 2^11.5
		int32_t int_clock_tempo = continuous_source_from_config(offset);
		return Pow2(48621 + 12*int_clock_tempo);
	}
}


////////////////////////////////////////
// Turing machine

void __not_in_flash_func(tm_step)(bool useClockDivider, bool risingEdge)
{
	// If we're using the clock divider, set it up and calculate whether to trigger
	bool step = false;
	if (useClockDivider)
	{
		divider_set(&tm_divider, config[SEN_TM_CLOCK + 1] + 1);
		step = divider_step(&tm_divider, risingEdge);
	}

	// Now do the actual Turing Machine, either if clock divider disabled, or if enabled and on nth rising edge
	if (!useClockDivider || (step && risingEdge))
	{
		uint32_t chosenBit = turing_machine_step(&tm, continuous_source_from_config(SEN_TM_MAIN_KNOB));
		uint32_t volt = turing_machine_volt(&tm);
		
		// TM pulse -> Pulse out
		if (config[SEN_PULSE_OUT_1 + 1] == OPT_TURING_MACHINE_PULSE_OUT) PulseOut1(chosenBit);
		if (config[SEN_PULSE_OUT_2 + 1] == OPT_TURING_MACHINE_PULSE_OUT) PulseOut2(chosenBit);

		// TM analogue -> CV out
		if (config[SEN_CV_OUT_1] == OPT_TURING_MACHINE_ANALOGUE_OUT) CVOut1(1024 - (volt << 2));
		if (config[SEN_CV_OUT_2] == OPT_TURING_MACHINE_ANALOGUE_OUT) CVOut2(1024 - (volt << 2));
	}
}


////////////////////////////////////////
// Bernoulli gate

void __not_in_flash_func(bg_div_step)(bool risingEdge)
{
	static bool lastDiv = false;

	divider_set(&bg_divider, config[SEN_BG_CLOCK + 1] + 1);

	bool newDiv = divider_step(&bg_divider, risingEdge);

	if (newDiv != lastDiv)
	{
		bool bg_value = bernoulli_gate_step(&bg,
		                                    continuous_source_from_config(SEN_BG_RAND),
		                                    newDiv);


		bool chanA = bg_value, chanB = !bg_value;
		if (bg.awi)
		{
			chanA = chanA && newDiv;
			chanB = chanB && newDiv;
		}

		if (config[SEN_PULSE_OUT_1 + 1] == OPT_BERNOULLI_GATE_OUT_A) PulseOut1(chanA);
		if (config[SEN_PULSE_OUT_1 + 1] == OPT_BERNOULLI_GATE_OUT_B) PulseOut1(chanB);
		if (config[SEN_PULSE_OUT_2 + 1] == OPT_BERNOULLI_GATE_OUT_A) PulseOut2(chanA);
		if (config[SEN_PULSE_OUT_2 + 1] == OPT_BERNOULLI_GATE_OUT_B) PulseOut2(chanB);
	}

	lastDiv = newDiv;
}



////////////////////////////////////////
// Non-application-specific functions for dealing with configuration over SysEx


// declarations of application-level handling functions
void handle_midi_message(uint8_t *packet);
void post_config_processing();
void post_flash_processing();


// Set RAM configuration to be the default defined in this .c file
void set_default_config()
{
	for (int i = 0; i < CONFIG_LENGTH; i++)
	{
		config[i] = defaultConfig[i];
	}
}

// Set RAM configuration from that stored in flash (if valid), or
// from default configuration if no valid config stored in flash
void set_config_from_flash()
{
	for (int i = 0; i < CONFIG_LENGTH; i++)
	{
		config[i] = *((uint8_t *)(XIP_BASE + configFlashAddr + i));
	}
	if (config[SYSEX_CONFIGURED_MARKER_INDEX] != SYSEX_CONFIGURED_MARKER)
	{
		set_default_config();
	}

	post_config_processing();
}

int set_config_from_sysex(uint8_t *packet)
{
	//	debug("set_config_from_sysex %d %d\n", packet[SYSEX_INDEX_LENGTH_ALL_DATA], CONFIG_LENGTH);
	if (packet[SYSEX_INDEX_LENGTH_ALL_DATA] != CONFIG_LENGTH)
		return -1; // error

	uint32_t payloadLength = packet[SYSEX_INDEX_LENGTH];
	uint32_t offset = packet[SYSEX_INDEX_PACKET_START_BYTE];
	bool lastPacket = packet[SYSEX_INDEX_PACKET_INDEX] == packet[SYSEX_INDEX_NUM_PACKETS] - 1;

	if (offset + payloadLength > CONFIG_LENGTH)
	{
		return -1; // error, woudn't fit into config
	}

	// copy sysex payload to appropriate place in RAM config buffer
	memcpy(&(configBuffer[offset]), &(packet[SYSEX_READ_CONFIG_HEADER_LEN]), payloadLength);

	if (lastPacket)
	{
		// Copy data from temporary buffer into real config
		memcpy(config, configBuffer, CONFIG_LENGTH);
		
		// run any application-specific processing
		post_config_processing();
	}
	return 0; // success
}

void save_config_to_flash()
{
	runADCMode = RUN_ADC_MODE_REQUEST_ADC_STOP;

	// wait for audio core to detect runADCMode flag and stop
	while (runADCMode != RUN_ADC_MODE_ADC_STOPPED) {}

	// shut down the other core
	multicore_lockout_start_blocking();

	// erase page of flash
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(configFlashAddr, 4096);
	restore_interrupts(ints);

	// write RAM config to flash
	ints = save_and_disable_interrupts();
	flash_range_program(configFlashAddr, config, CONFIG_LENGTH);
	restore_interrupts(ints);

	post_flash_processing();

	// Request ADC restart once other core starts up again
	runADCMode = RUN_ADC_MODE_REQUEST_ADC_RESTART;

	// restore other core
	multicore_lockout_end_blocking();
}


#ifdef ENABLE_MIDI
void process_sys_ex_command(uint8_t *packet)
{
	if (packet[SYSEX_INDEX_MANUFACTURER] != SYSEX_MANUFACTURER_DEV)
		return;

	switch (packet[SYSEX_INDEX_COMMAND])
	{
	case SYSEX_COMMAND_PREVIEW: // preview: save config from sysex to ram, but not to flash
		set_config_from_sysex(packet);
		break;

	case SYSEX_COMMAND_WRITE_FLASH:

		// If successful in setting RAM config from sysex...
		if (!set_config_from_sysex(packet))
		{
			// save RAM config to flash
			save_config_to_flash();
		}
		break;

	case SYSEX_COMMAND_READ:
		debugp("Received card config request\n");
		packet[0] = 0xF0; // start sysex
		packet[SYSEX_INDEX_MANUFACTURER] = SYSEX_MANUFACTURER_DEV;
		packet[SYSEX_INDEX_COMMAND] = SYSEX_COMMAND_READ; // indicate what command this is in response to

		{
			int max_payload_size = 32;
			int num_packets = (CONFIG_LENGTH + max_payload_size - 1) / max_payload_size;
			int bytes_to_send = CONFIG_LENGTH;
			int bytes_this_packet, start_byte;
			for (int p = 0; p < num_packets; p++)
			{
				if (bytes_to_send > max_payload_size)
				{
					bytes_this_packet = max_payload_size;
				}
				else
				{
					bytes_this_packet = bytes_to_send;
				}

				start_byte = p * max_payload_size;

				packet[SYSEX_INDEX_LENGTH] = bytes_this_packet;
				packet[SYSEX_INDEX_NUM_PACKETS] = num_packets;
				packet[SYSEX_INDEX_PACKET_INDEX] = p;
				packet[SYSEX_INDEX_PACKET_START_BYTE] = start_byte;
				packet[SYSEX_INDEX_LENGTH_ALL_DATA] = CONFIG_LENGTH;

				memcpy(&(packet[SYSEX_READ_CONFIG_HEADER_LEN]), &(config[start_byte]), bytes_this_packet); // copy config into sysex packet
				packet[bytes_this_packet + SYSEX_READ_CONFIG_HEADER_LEN] = 0xF7; // end sysex
			
				int rv2 = tud_midi_stream_write(0, packet, bytes_this_packet + SYSEX_READ_CONFIG_HEADER_LEN + 1);
			
				bytes_to_send -= bytes_this_packet;
			}
		}
		break;

	case SYSEX_COMMAND_READ_CARD_RELEASE:
		packet[0] = 0xF0; // start sysex
		packet[SYSEX_INDEX_MANUFACTURER] = SYSEX_MANUFACTURER_DEV;
		packet[SYSEX_INDEX_COMMAND] = SYSEX_COMMAND_READ_CARD_RELEASE; // indicate what command this is in response to
		packet[SYSEX_INDEX_LENGTH] = 4; // 4-byte response of card ID number, and major and minor versions
		packet[4] = CARD_ID_LOW;
		packet[5] = CARD_ID_HIGH;
		packet[6] = CARD_VER_MAJOR;
		packet[7] = CARD_VER_MINOR;
		packet[8] = 0xF7; // end sysex
		int rv3 = tud_midi_stream_write(0, packet, 9);
		break;

	default:
		break;
	}
}

void midi_device_task()
{
	// Read incoming packet if availabie
	while (tud_midi_available())
	{
		uint32_t bytesToProcess = tud_midi_stream_read(packet, sizeof(packet));

		uint8_t *bufferPtr = packet;
		
		while (bytesToProcess > 0)
		{
			if (bufferPtr[0] == 0xF0) // If SysEx, handle with standard routine
			{
				process_sys_ex_command(bufferPtr);
			}
			else // else pass on to application midi message handler
			{
				handle_midi_message(bufferPtr);
			}

			// Move pointer to next midi message in buffer, if it exists
			// by scanning until we see a MIDI command byte (most significant bit set)
			do 
			{
				bufferPtr++;
				bytesToProcess--;
			} while (bytesToProcess > 0 && (!(*bufferPtr & 0x80)));			
		}
	}
}


////////////////////////////////////////
// MIDI host callbacks
void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
{
	(void)in_ep; (void)out_ep; (void)num_cables_rx; // avoid unused variable warnings

	
	if (midiDeviceAddress == 0)
	{
		midiDeviceAddress = dev_addr;
		midiDeviceConnected = 1;
		midiDeviceOutputCable = num_cables_tx - 1; // sets to -1 if device has no midi
	}
}


void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)instance;
	
	if (dev_addr == midiDeviceAddress)
	{
		midiDeviceAddress = 0;
		midiDeviceConnected = 0;
		midiDeviceOutputCable = -1;
	}
}

// Handle host-mode incoming midi message
void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
	if (midiDeviceAddress != dev_addr)
		return;

	if (num_packets == 0)
		return;
	
	uint8_t cable_num;
	static uint8_t buffer[512];
	while (1)
	{
		int32_t bytesToProcess = tuh_midi_stream_read(dev_addr, &cable_num, buffer, sizeof(buffer));
		uint8_t *bptr = buffer;

		if (bytesToProcess == 0)
		{
			break;
		}
		
		while (bytesToProcess > 0)
		{
			if (bptr[0] == 0xF0)
			{
				process_sys_ex_command(bptr);
			}
			else
			{
				handle_midi_message(bptr);
			}

			// move pointer along until we reach an other MIDI command byte
			do 
			{
				bptr++;
				bytesToProcess--;
			} while (bytesToProcess > 0 && (!(*bptr & 0x80)));			
		}

	}
}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}



#endif


////////////////////////////////////////////////////////////////////////////////
// Application-specific functions for dealing with config and midi messages

int n_notes_on_pulse1 = 0;
int n_notes_on_pulse2 = 0;
int n_notes_on_cv1 = 0;
int n_notes_on_cv2 = 0;

// Return true if packet channel MIDI channel config at offset 'sen'
bool midi_channel(uint8_t *packet, int sen)
{
	uint8_t channel = packet[0] & 0x0F;

	return (config[sen] == OPT_ANY_MIDI_CHANNEL || config[sen] == channel + 1);
}

// Return true if packet, assumed to be a MIDI note on/off, has key and channel
// corresponding to the config at offset 'sen'
bool midi_gate(uint8_t *packet, int sen)
{
	uint8_t noteNum = packet[1];
	return (config[sen] == OPT_ANY || config[sen] == noteNum + 1) && midi_channel(packet, sen + 1);
}


bool midi_cc(uint8_t *packet, int sen)
{
	uint8_t ccNum = packet[1];
	return (config[sen] == ccNum) && midi_channel(packet, sen + 1);
}

// Calls on/off function only when number of notes down moves towards/away from zero
void gate_counter(bool noteOn, int *count, void (*fn)(bool))
{
	if (noteOn)
	{
		if (*count == 0)
		{
			fn(true);
		}
		(*count)++;
	}
	else if (*count > 0)
	{
		(*count)--;
		if ((*count) == 0)
		{
			fn(false);
		}
	}
}
void handle_midi_message(uint8_t *packet)
{
	uint8_t messageType = packet[0] & 0xF0;

	// If a note on or note off
	if (messageType == MIDI_NOTE_ON || messageType == MIDI_NOTE_OFF)
	{
		// Gates
		
		// Count note on with velocity zero as note off
		bool noteOn = (messageType == MIDI_NOTE_ON) && (packet[2] != 0);

		if (config[SEN_REV_FREEZE] == OPT_GATE_OF_MIDI_NOTE
			&& midi_gate(packet, SEN_REV_FREEZE + 1))
		{
			frozenReverb = noteOn;
		}
			
		// MIDI gate/trigger --> pulse out 1/2
		if (config[SEN_PULSE_OUT_1 + 1] == OPT_MIDI_NOTE && midi_gate(packet, SEN_PULSE_OUT_1 + 2))
		{
			if (config[SEN_PULSE_OUT_1] == OPT_TRIGGER && noteOn)
			{
				lastPulseOut1 = false;
				PulseOut1(true);
			}
			else
			{
				gate_counter(noteOn, &n_notes_on_pulse1, PulseOut1);
			}
		}
		
		if (config[SEN_PULSE_OUT_2 + 1] == OPT_MIDI_NOTE && midi_gate(packet, SEN_PULSE_OUT_2 + 2))
		{
			if (config[SEN_PULSE_OUT_2] == OPT_TRIGGER && noteOn)
			{
				lastPulseOut2 = false;
				PulseOut2(true);
			}
			else
			{
				gate_counter(noteOn, &n_notes_on_pulse2, PulseOut2);
			}
		}

		// MIDI gate/trigger --> CV out 1/2
		if (config[SEN_CV_OUT_1] == OPT_MIDI_NOTE && midi_gate(packet, SEN_CV_OUT_1 + 1))
		{
			gate_counter(noteOn, &n_notes_on_cv1, CVOut1Gate);
		}
		if (config[SEN_CV_OUT_2] == OPT_MIDI_NOTE && midi_gate(packet, SEN_CV_OUT_2 + 1))
		{
			gate_counter(noteOn, &n_notes_on_cv2, CVOut2Gate);
		}

		// MIDI gate --> TM
		if (config[SEN_TM_CLOCK] == OPT_MIDI_NOTE && midi_gate(packet, SEN_TM_CLOCK + 1)
		    && noteOn)
		{
			tm_step(false, false);
		}

		// MIDI gate --> Bernoulli gate
		if (config[SEN_BG_CLOCK] == OPT_MIDI_NOTE && midi_gate(packet, SEN_BG_CLOCK + 1))
		{        
			bg_div_step(noteOn);
		}
		 
		// MIDI note pitch --> CV out
		// (note pitch + pitchbend)
		midiNotenum = packet[1];
		if (noteOn)
		{
			if (config[SEN_CV_OUT_1] == OPT_PITCH && midi_channel(packet, SEN_CV_OUT_1 + 1))
			{
				CVOut1(midiToDac8((midiNotenum<<8)+(midiPitchbend>>4), 0) >> 8);
			}
			if (config[SEN_CV_OUT_2] == OPT_PITCH && midi_channel(packet, SEN_CV_OUT_2 + 1))
			{
				CVOut2(midiToDac8((midiNotenum<<8)+(midiPitchbend>>4), 1) >> 8);
			}
		}
	}
	else if (messageType == MIDI_CC) // If MIDI CC
	{
		// MIDI CC --> CV out 1/2
		uint8_t ccVal = packet[2];
		if (config[SEN_CV_OUT_1] == OPT_MIDI_CC && midi_cc(packet, SEN_CV_OUT_1 + 1))
		{
			CVOut1(1024 - ccVal * 8);
		}
		if (config[SEN_CV_OUT_2] == OPT_MIDI_CC && midi_cc(packet, SEN_CV_OUT_2 + 1))
		{
			CVOut2(1024 - ccVal * 8);
		}
	}
	else if (messageType == MIDI_PITCHBEND)
	{
		// MIDI pitchbend --> CV out
		// (note pitch + pitchbend)
		midiPitchbend =  packet[1] + (packet[2]<<7) - 8192;
		if (config[SEN_CV_OUT_1] == OPT_PITCH && midi_channel(packet, SEN_CV_OUT_1 + 1))
		{		
			CVOut1(midiToDac8((midiNotenum<<8)+(midiPitchbend>>4), 0) >> 8);
		}
		
		if (config[SEN_CV_OUT_2] == OPT_PITCH && midi_channel(packet, SEN_CV_OUT_2 + 1))
		{
			CVOut2(midiToDac8((midiNotenum<<8)+(midiPitchbend>>4), 1) >> 8);
		}
	}
}


reverb *dv = 0;


// After saving to flash, reset reverb buffers (occasionally had reverb overflow)
void post_flash_processing()
{
	reverb_reset(dv);

	n_notes_on_pulse1 = 0;
	n_notes_on_pulse2 = 0;
	n_notes_on_cv1 = 0;
	n_notes_on_cv2 = 0;
}

// Processing after config changed
void post_config_processing()
{
	clock_set_freq_incr(&clk[0], tempo_source_from_config_incr(SEN_CLKA_TEMPO));
	clock_set_freq_incr(&clk[1], tempo_source_from_config_incr(SEN_CLKB_TEMPO));

	turing_machine_set_length(&tm, config[SEN_TM_LENGTH]);

	bernoulli_gate_set_toggle(&bg, config[SEN_BG_OPTS]);
	bernoulli_gate_set_and_with_input(&bg, config[SEN_BG_OPTS + 1]);

	allLedFlashTimer = 4800;
}


volatile int inDefaultConfigLoad;

// Handles USB. Not synchronised to ADC/audio
void usb_worker()
{
	static uint32_t delayVal = 1;
	inDefaultConfigLoad = 0;
	set_config_from_flash();


	// Precalculate exponential lookup table, used only by this core.
	float f = 67108864; // 2^26
	for (int i = 0; i < 128; i++)
	{
		pow2_128[i] = (uint32_t)f;
		f *= 1.005429901112803f; // 2^(1/128)
	}

		
	sleep_us(100000); // wait 0.1s
#ifdef ENABLE_MIDI

	// work out MIDI host status
	uint8_t boardid = GetBoardID();
	bool isHost;
	if (boardid == BOARD_PROTO_1 || boardid == BOARD_PROTO_2_0)
	{
		// USB host mode not supported on 2024 boards
		isHost = false;
	}
	else if (gpio_get(USB_HOST_STATUS))
	{
		isHost = false; // 2025 (or later) board, upstream facing port
	}
	else
	{
		isHost = true; // 2025 (or later) board, downstream facing port
	}

	if (isHost)
	{
		tuh_init(TUH_OPT_RHPORT);
	}
	else
	{
		tud_init(TUD_OPT_RHPORT);
	}

	board_init();
#endif

	// If switch held down (momentary) for 0.1s at startup,
	// load default configuration and write it into flash
	{
		int loadDefaultConfig = 1;
		for (int i = 0; i < 10; i++)
		{
			sleep_us(10000); // wait 0.01s

			// If switch is not in momentary position, abort
			if (knobs[KNOB_SWITCH] >= 1000)
			{
				loadDefaultConfig = 0;
				break;
			}
		}
		if (loadDefaultConfig)
		{
			inDefaultConfigLoad = 1;
			sleep_us(50000); // wait 0.05s
			set_default_config();
			save_config_to_flash();
		}
	}

	while (1)
	{
		delayVal = 1664525 * delayVal + 1013904223;
#ifdef ENABLE_MIDI
		if (isHost)
		{
			tuh_task();
		}
		else
		{
			tud_task();
			midi_device_task();
		}
#endif

		// Do this processing in the second core, as it's a relatively expensive calculation
		// though should be quicker now it is not floating point
//		clock_set_freq_hz(&clk[0], tempo_source_from_config(SEN_CLKA_TEMPO));
//		clock_set_freq_hz(&clk[1], tempo_source_from_config(SEN_CLKB_TEMPO));

		clock_set_freq_incr(&clk[0], tempo_source_from_config_incr(SEN_CLKA_TEMPO));
		clock_set_freq_incr(&clk[1], tempo_source_from_config_incr(SEN_CLKB_TEMPO));
		
		// Stall for a random amount of time, up to 255us, to minimise tones in audio interference
		// Don't do this for MIDI host, as I think this is slightly more timing-sensitive
		if (!isHost) sleep_us(delayVal & 0xFF);

		if (pfflag == 1)
		{
			debug("%08x %08x\n", pf1, pf2);
			pfflag = 0;
		}
	}
}



////////////////////////////////////////
// Audio core functions

// Main audio core function
void __not_in_flash_func(audio_worker)()
{
	// Audio worker must be able to stop when USB worker wants to write to flash
	multicore_lockout_victim_init();


	adc_select_input(0);
	adc_set_round_robin(0b0001111U);

	// enabled, with DMA request when FIFO contains data, no erro flag, no byte shift
	adc_fifo_setup(true, true, 1, false, false);


	// ADC clock runs at 48MHz
	// 48MHz ÷ (124+1) = 384kHz ADC sample rate
	//                 = 8×48kHz audio sample rate
	adc_set_clkdiv(124);

	// claim and setup DMAs for reading to ADC, and writing to SPI DAC
	adc_dma = dma_claim_unused_channel(true);
	spi_dma = dma_claim_unused_channel(true);

	dma_channel_config adc_dmacfg, spi_dmacfg;
	adc_dmacfg = dma_channel_get_default_config(adc_dma);
	spi_dmacfg = dma_channel_get_default_config(spi_dma);

	// Reading from ADC into memory buffer, so increment on write, but no increment on read
	channel_config_set_transfer_data_size(&adc_dmacfg, DMA_SIZE_16);
	channel_config_set_read_increment(&adc_dmacfg, false);
	channel_config_set_write_increment(&adc_dmacfg, true);

	// Synchronise ADC DMA the ADC samples
	channel_config_set_dreq(&adc_dmacfg, DREQ_ADC);

	// Setup DMA for 8 ADC samples
	dma_channel_configure(adc_dma, &adc_dmacfg, ADC_Buffer[dmaPhase], &adc_hw->fifo, 8, true);

	// Turn on IRQ for ADC DMA
	dma_channel_set_irq0_enabled(adc_dma, true);

	// Call buffer_full ISR when ADC DMA finished
	irq_set_enabled(DMA_IRQ_0, true);
	irq_set_exclusive_handler(DMA_IRQ_0, buffer_full);



	// Set up DMA for SPI
	spi_dmacfg = dma_channel_get_default_config(spi_dma);
	channel_config_set_transfer_data_size(&spi_dmacfg, DMA_SIZE_16);

	// SPI DMA timed to SPI TX
	channel_config_set_dreq(&spi_dmacfg, SPI_DREQ);

	// Set up DMA to transmit 2 samples to SPI
	dma_channel_configure(spi_dma, &spi_dmacfg, &spi_get_hw(SPI_PORT)->dr, NULL, 2, false);



	// Sample processing loop
	noise_gate_init(&ng);


	adc_run(true);

	while (1)
	{
		// If ready to restart
		if (runADCMode == RUN_ADC_MODE_REQUEST_ADC_RESTART)
		{
			runADCMode = RUN_ADC_MODE_RUNNING;

			dma_hw->ints0 = 1u << adc_dma; // reset adc interrupt flag
			dma_channel_set_write_addr(adc_dma, ADC_Buffer[dmaPhase], true); // start writing into new buffer
			dma_channel_set_read_addr(spi_dma, SPI_Buffer[dmaPhase], true); // start reading from new buffer

			adc_set_round_robin(0);
			adc_select_input(0);
			adc_set_round_robin(0b0001111U);
			adc_run(true);
		}
	}
}

int32_t __not_in_flash_func(freeze_mute)(bool mute)
{
	static int32_t mul = 256;

	if (mute && mul > 0) mul--;
	if (!mute && mul < 256) mul++;
	return mul;
}

// process_sample is called once-per-audio-sample (48kHz) by the buffer_full ISR
const int startupSampleDelay = 20000;

void __not_in_flash_func(process_sample)()
{
	//	gpio_put(DEBUG_2, true);
	static int32_t mix1, mix2, mixf1, mixf2;

	static int32_t frame = 0;
	static uint32_t cv1Noise = 0, cv2Noise = 0;
	static int clippingCounter = 0; // clipping indicator counter

	int inStartupState = (frame < startupSampleDelay);


	static bool lastMomentarySwitch = false;
	bool momentarySwitch = knobs[KNOB_SWITCH] < 1000;

	if (!inStartupState)
	{
		if (momentarySwitch && !lastMomentarySwitch)
			frozenReverb = true;

		if (!momentarySwitch && lastMomentarySwitch)
			frozenReverb = false;
	}

	////////////////////////////////////////
	// Set reverb parameters from configuration

	// Get dry/wet knob position
	// add some dead-zones to get 100% wet/dry at end of travel
	// 0-4096 inclusive
	int32_t knob;
	knob = continuous_source_from_config(SEN_REV_WETDRY);
	int32_t drywet = clamp((((knob)*71936) >> 16) - 200, 0, 4096);

	// Shape dry/wet control to make it easier to select very small
	// amounts of wet signal
	drywet = (drywet >> 2) + ((3 * drywet * drywet) >> 14);

	// Decay time
	knob = continuous_source_from_config(SEN_REV_DECAY);
	int32_t knobx = clamp((((knob)*71936) >> 12) - 3200, 50, 65500);

	// Tone
	knob = continuous_source_from_config(SEN_REV_TONE);
	reverb_set_tilt(dv, clamp(knob, 0, 4095) * 16);

	int32_t fm_mult = freeze_mute(frozenReverb);

	reverb_set_freeze_size(dv, knobx, fm_mult);


	////////////////////////////////////////
	// The actual audio DSP:

	// Mute ADC or fade it in, if in startup state
	if (inStartupState)
	{
		if (frame < startupSampleDelay - 16384)
		{
			adcInL = 0;
			adcInR = 0; // mute ADC
		}
		else
		{
			int32_t adcL = adcInL * (frame + 16384 - startupSampleDelay);
			int32_t adcR = adcInR * (frame + 16384 - startupSampleDelay);
			adcInL = adcL >> 14;
			adcInR = adcR >> 14;
		}
	}

	int32_t mix = (adcInL - adcInR) << 2; // mix is limited by ±16384

	// 12kHz notch filter, to remove interference from mux lines
	int32_t ooa0 = 16302, a2oa0 = 16221; // Q = 100, very narrow notch
	int32_t mixf = (ooa0 * (mix + mix2) - a2oa0 * mixf2) >> 14;
	mix2 = mix1;
	mix1 = mix;
	mixf2 = mixf1;
	mixf1 = mixf;

	// If switch is in middle position, apply noise gate to mixed ADC input
	int32_t gated_mono_in = (knobs[KNOB_SWITCH] < 3000) ? noise_gate_tick(&ng, mixf) : mixf;

	// If reverb frozen, mute input signal, with fade in/out
	gated_mono_in = (fm_mult * gated_mono_in) >> 8;

	// Run the reverb algorithm
	reverb_process(dv, gated_mono_in);
	int32_t left = reverb_get_left(dv);
	int32_t right = reverb_get_right(dv);

	int32_t dry = gated_mono_in << 1; // +- 32768

	// Get absolute values of wet output, for VU meter
	int32_t aleft = left;
	if (aleft < 0) aleft = -aleft;

	// Crossfade wet and dry signals
	// ( (0 to 4096)  * (-32768 to 32768) )/2^16 = -2048 to 2048
	left = (drywet * left + (4096 - drywet) * dry) >> 16;
	right = (drywet * right + (4096 - drywet) * dry) >> 16;

	// generate values for DAC, with final stage of clipping
	dacOutL = clamp(left, -2047, 2047);
	dacOutR = clamp(right, -2047, 2047);

	// Mute DAC or fade it in, if in startup state
	if (inStartupState)
	{
		if (frame < startupSampleDelay - 16384)
		{
			dacOutL = 0;
			dacOutR = 0; // mute DAC for first ~1.5 seconds
		}
		else
		{
			// Fade DAC in
			int32_t dacL = dacOutL * (frame + 16384 - startupSampleDelay);
			int32_t dacR = dacOutR * (frame + 16384 - startupSampleDelay);
			dacOutL = dacL >> 14;
			dacOutR = dacR >> 14;
		}
	}


	debug_pin(DEBUG_1, true);


	////////////////////////////////////////
	// Display clipping indicator

	// If input is clipping, activate 0.1s clipping indicator
	if (adcInL >= 2000 || adcInL <= -2000 || adcInR >= 2000 || adcInR <= -2000)
	{
		clippingCounter = 4800;
	}

	// If output is clipping, activate 0.1s clipping indicator
	if (dacOutL == 2047 || dacOutL == -2047 || dacOutR == 2047 || dacOutR == -2047)
	{
		clippingCounter = 4800;
	}

	// Count down clipping counter
	if (clippingCounter) clippingCounter--;

	// Display reverb input or output clipping
	gpio_put(leds[1], clippingCounter > 0);


	////////////////////////////////////////
	// Misc UI

	// Don't count forever, otherwise after ~12.5 hours, the 32-bit int will wrap
	// and we'll go back into the startup phase
	if (frame < 1000000) frame++;

	// Show LED pattern during startup state
	if (inStartupState)
	{
		int t = frame >> 10;
		gpio_put(leds[0], ((t + 0) % 6) == 0 || inDefaultConfigLoad);
		gpio_put(leds[2], ((t + 1) % 6) == 0 || inDefaultConfigLoad);
		gpio_put(leds[4], ((t + 2) % 6) == 0 || inDefaultConfigLoad);
		gpio_put(leds[5], ((t + 3) % 6) == 0 || inDefaultConfigLoad);
		gpio_put(leds[3], ((t + 4) % 6) == 0 || inDefaultConfigLoad);
		gpio_put(leds[1], ((t + 5) % 6) == 0 || inDefaultConfigLoad);

		return;
	}

	// Display VU meter
	gpio_put(leds[4], aleft > 4095);
	gpio_put(leds[2], aleft > 8191);
	gpio_put(leds[0], aleft > 16383);


	////////////////////////////////////////
	// Actions driven by pulse inputs
	for (int pulse_index = 0; pulse_index < 2; pulse_index++)
	{
		bool pulseInRisingEdge = pulse[pulse_index] && !last_pulse[pulse_index];
		bool pulseInFallingEdge = !pulse[pulse_index] && last_pulse[pulse_index];

		if (pulseInRisingEdge || pulseInFallingEdge)
		{
			/// pulse_opt is the value corresponding to the menu option 'clocked by Pulse In X'
			uint8_t pulse_opt = OPT_PULSE_IN_1 + pulse_index;

			// Pulse in --> out 1/2 with clock division
			if (config[SEN_PULSE_OUT_1 + 1] == pulse_opt)
			{
				divider_set(&pulseout1_divider, config[SEN_PULSE_OUT_1 + 2] + 1);
				PulseOut1(divider_step(&pulseout1_divider, pulseInRisingEdge));
			}
			if (config[SEN_PULSE_OUT_2 + 1] == pulse_opt)
			{
				divider_set(&pulseout2_divider, config[SEN_PULSE_OUT_2 + 2] + 1);
				PulseOut2(divider_step(&pulseout2_divider, pulseInRisingEdge));
			}

			
			// Pulse in --> Bernoulli gate
			if (config[SEN_BG_CLOCK] == pulse_opt)
			{
				bg_div_step(pulseInRisingEdge);
			}
			
			// Pulse in --> TM
			if (config[SEN_TM_CLOCK] == pulse_opt)
			{
				tm_step(true, pulseInRisingEdge);
			}
			
			// Pulse in --> CV out 1/2 noise S&H
			if (config[SEN_CV_OUT_1] == OPT_NOISE && config[SEN_CV_OUT_1 + 1] == pulse_opt)
			{
				divider_set(&cvout1_divider, config[SEN_CV_OUT_1 + 2] + 1);
				if (pulseInRisingEdge && divider_step(&cvout1_divider, pulseInRisingEdge)) cv1Noise = rnd() >> 21;
			}

			if (config[SEN_CV_OUT_2] == OPT_NOISE && config[SEN_CV_OUT_2 + 1] == pulse_opt)
			{
				divider_set(&cvout2_divider, config[SEN_CV_OUT_2 + 2] + 1);
				if (pulseInRisingEdge && divider_step(&cvout2_divider, pulseInRisingEdge)) cv2Noise = rnd() >> 21;
			}

			// Pulse in --> Reverb freeze
			if (config[SEN_REV_FREEZE] == pulse_opt)
			{
				frozenReverb = pulseInRisingEdge;
			}

			// Pulse in --> CV out 1/2 gate
			if (config[SEN_CV_OUT_1] == pulse_opt)
			{
				divider_set(&cvout1_divider, config[SEN_CV_OUT_1 + 1] + 1);
				CVOut1Gate(divider_step(&cvout1_divider, pulseInRisingEdge));
			}
			if (config[SEN_CV_OUT_2] == pulse_opt)
			{
				divider_set(&cvout2_divider, config[SEN_CV_OUT_2 + 1] + 1);
				CVOut2Gate(divider_step(&cvout2_divider, pulseInRisingEdge));
			}
		}
	}


	////////////////////////////////////////
	// Actions driven by the two internal clocks
	for (int clk_index = 0; clk_index < 2; clk_index++)
	{
		if (clock_tick(&clk[clk_index])) // true for both rising and falling edges
		{
			bool risingEdge = clock_state(&clk[clk_index]);
			int clockOpt = OPT_INTERNAL_CLOCK_A + clk_index;

			// Internal clocks --> pulse out
			if (config[SEN_PULSE_OUT_1 + 1] == clockOpt)
			{
				divider_set(&pulseout1_divider, config[SEN_PULSE_OUT_1 + 2] + 1);
				PulseOut1(divider_step(&pulseout1_divider, risingEdge));
			}
			if (config[SEN_PULSE_OUT_2 + 1] == clockOpt)
			{
				divider_set(&pulseout2_divider, config[SEN_PULSE_OUT_2 + 2] + 1);
				PulseOut2(divider_step(&pulseout2_divider, risingEdge));
			}

			// Internal clocks --> CV out
			if (config[SEN_CV_OUT_1] == clockOpt)
			{
				divider_set(&cvout1_divider, config[SEN_CV_OUT_1 + 1] + 1);
				CVOut1Gate(divider_step(&cvout1_divider, risingEdge));
			}
			if (config[SEN_CV_OUT_2] == clockOpt)
			{
				divider_set(&cvout2_divider, config[SEN_CV_OUT_2 + 1] + 1);
				CVOut2Gate(divider_step(&cvout2_divider, risingEdge));
			}

			// Internal clocks -> noise S&H trigger
			if (config[SEN_CV_OUT_1] == OPT_NOISE && config[SEN_CV_OUT_1 + 1] == clockOpt)
			{
				divider_set(&cvout1_divider, config[SEN_CV_OUT_1 + 2] + 1);
				if (risingEdge && divider_step(&cvout1_divider, risingEdge))
				{
					cv1Noise = rnd() >> 21;
				}
			}

			if (config[SEN_CV_OUT_2] == OPT_NOISE && config[SEN_CV_OUT_2 + 1] == clockOpt)
			{
				divider_set(&cvout2_divider, config[SEN_CV_OUT_2 + 2] + 1);
				if (risingEdge && divider_step(&cvout2_divider, risingEdge))
				{
					cv2Noise = rnd() >> 21;
				}
			}

			// Internal clocks --> Bernoulli gate
			if (config[SEN_BG_CLOCK] == clockOpt)
			{
				bg_div_step(risingEdge);
			}

			// Internal clocks --> Reverb freeze
			if (config[SEN_REV_FREEZE] == clockOpt)
			{
				frozenReverb = risingEdge;
			}

			// Internal clocks --> TM
			if (config[SEN_TM_CLOCK] == clockOpt && risingEdge)
			{
				tm_step(true, risingEdge);
			}
		}
	}

	if (config[SEN_CV_OUT_1] == OPT_NOISE)
	{
		if (config[SEN_CV_OUT_1 + 1] == OPT_CONTINUOUS) cv1Noise = rnd() >> 21;
		CVOut1(cv1Noise);
	}
	if (config[SEN_CV_OUT_2] == OPT_NOISE)
	{
		if (config[SEN_CV_OUT_2 + 1] == OPT_CONTINUOUS) cv2Noise = rnd() >> 21;
		CVOut2(cv2Noise);
	}

	lastMomentarySwitch = momentarySwitch;

	// Process trigger timers, and turn off pulse outputs if trigger is over
	if (triggerTimer1 > 0)
	{
		triggerTimer1--;
		if (triggerTimer1 == 0 && config[SEN_PULSE_OUT_1] == OPT_TRIGGER)
		{
			gpio_put(leds[3], false);
			gpio_put(PULSE_1_RAW_OUT, true);
		}
	}
	if (triggerTimer2 > 0)
	{
		triggerTimer2--;
		if (triggerTimer2 == 0 && config[SEN_PULSE_OUT_2] == OPT_TRIGGER)
		{
			gpio_put(leds[5], false);
			gpio_put(PULSE_2_RAW_OUT, true);
		}
	}

	if (allLedFlashTimer)
	{
		for (int i=0; i<6; i++) gpio_put(leds[i],true);
		allLedFlashTimer--;
	}
	
	debug_pin(DEBUG_1, false);
}



////////////////////////////////////////
// Entry point

int main()
{
	// Run at 192MHz, near max clock as of pico-sdk 2.1.1
	// Divides sample rate of 48kHz exactly, which helps remove tones in ADC input.
	// https://github.com/raspberrypi/pico-sdk/releases/tag/2.1.1
	set_sys_clock_khz(192000, true);

	adc_run(false);
	adc_select_input(0);

	sleep_ms(50);

	SetupComputerIO();

#ifdef ENABLE_UART_DEBUGGING
	stdio_init_all();
#endif

	ReadEEPROM();

	dv = reverb_create();

	turing_machine_init(&tm);
	bernoulli_gate_init(&bg);
	clock_init(&clk[0]);
	clock_init(&clk[1]);
	divider_init(&pulseout1_divider);
	divider_init(&pulseout2_divider);
	divider_init(&cvout1_divider);
	divider_init(&cvout2_divider);
	divider_init(&tm_divider);
	divider_init(&bg_divider);

	multicore_launch_core1(audio_worker);

	usb_worker();
}
