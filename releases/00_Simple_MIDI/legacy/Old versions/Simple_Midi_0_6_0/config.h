// Global configuation


int noteOffset = 60; // MIDI note corresponding to 0V for this DAC


// Mux pins
#define MX_A 24
#define MX_B 25

// ADC input pins
#define AUDIO_L_IN_1 26
#define AUDIO_R_IN_1 27
#define MUX_IO_1 28
#define MUX_IO_2 29

#define NUM_LEDS 6
#define LED1 10
#define LED2 11
#define LED3 12
#define LED4 13
#define LED5 14
#define LED6 15
uint8_t leds[NUM_LEDS] = {LED1, LED2, LED3, LED4, LED5, LED6};

#define DAC_config_chan_A_gain 0b0001000000000000
#define DAC_config_chan_B_gain 0b1001000000000000
#define DAC_config_chan_A_nogain 0b0011000000000000
#define DAC_config_chan_B_nogain 0b1011000000000000

#define DAC_CS 21
#define DAC_SCK 18
#define DAC_TX 19

// Basic MCP4822 calibration values
const int DACzeroPoint = 2037; // 0v
const int DAClowPoint = 3740; // -5v ;
const int DAChighPoint = 338; // +5v

#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9

#define KNOB_MAIN 0
#define KNOB_X 1
#define KNOB_Y 2
#define KNOB_SWITCH 3

#define CV_OUT_1 23
#define CV_OUT_2 22

#define DEBUG_1 0
#define DEBUG_2 1

SPISettings spiSettings(15625000, MSBFIRST, SPI_MODE0);
