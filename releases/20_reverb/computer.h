#ifndef COMPUTER_H
#define COMPUTER_H

// Input normalisation probe
#define NORMALISATION_PROBE 4

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
uint leds[NUM_LEDS] = { LED1, LED2, LED3, LED4, LED5, LED6 };

#define DAC_CHANNEL_A 0x0000
#define DAC_CHANNEL_B 0x8000

#define DAC_CS 21
#define DAC_SCK 18
#define DAC_TX 19

#define EEPROM_SDA 16
#define EEPROM_SCL 17

#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9

#define KNOB_MAIN 0
#define KNOB_X 1
#define KNOB_Y 2
#define KNOB_SWITCH 3

#define USB_HOST_STATUS 20

#define CV_OUT_1 23
#define CV_OUT_2 22


#define DEBUG_1 0
#define DEBUG_2 1

#define SPI_PORT spi0
#define SPI_DREQ DREQ_SPI0_TX


#define BOARD_ID_0 7
#define BOARD_ID_1 6
#define BOARD_ID_2 5
#define BOARD_PROTO_1 0x2a // 2024 Dyski
#define BOARD_PROTO_2_0 0x30
#define BOARD_REV1_0 0x30 // Q4 2024
#define BOARD_REV1_1 0x0C // Q2 2025

uint8_t GetBoardID()
{
	gpio_set_pulls(BOARD_ID_0, false, true);
	gpio_set_pulls(BOARD_ID_1, false, true);
	gpio_set_pulls(BOARD_ID_2, false, true);
	sleep_us(1);
	uint8_t pd = gpio_get(BOARD_ID_0) | (gpio_get(BOARD_ID_1) << 2) | (gpio_get(BOARD_ID_2) << 4);
	gpio_set_pulls(BOARD_ID_0, true, false);
	gpio_set_pulls(BOARD_ID_1, true, false);
	gpio_set_pulls(BOARD_ID_2, true, false);
	sleep_us(1);
	uint8_t pu = (gpio_get(BOARD_ID_0) << 1) | (gpio_get(BOARD_ID_1) << 3) | (gpio_get(BOARD_ID_2) << 5);
	gpio_set_pulls(BOARD_ID_0, false, true);
	gpio_set_pulls(BOARD_ID_1, false, true);
	gpio_set_pulls(BOARD_ID_2, false, true);
	return pd | pu;
}


#define EEPROM_ADDR_ID 0
#define EEPROM_ADDR_VERSION 2
#define EEPROM_ADDR_CRC_L 87
#define EEPROM_ADDR_CRC_H 86
#define EEPROM_VAL_ID 2001
#define EEPROM_NUM_BYTES 88

#define CAL_MAX_CHANNELS 2
#define CAL_MAX_POINTS 10
uint8_t eepromPageAddress = 0x50;


typedef struct
{
	float m, b;
	int32_t mi, bi;
} CalCoeffs;

typedef struct
{
	int32_t dacSetting;
	int8_t voltage;
} CalPoint;

uint8_t numCalibrationPoints[CAL_MAX_CHANNELS];
CalPoint calibrationTable[CAL_MAX_CHANNELS][CAL_MAX_POINTS];
CalCoeffs calCoeffs[CAL_MAX_CHANNELS];

// Read a byte from EEPROM
uint8_t ReadByteFromEEPROM(unsigned int eeAddress)
{
	uint8_t deviceAddress = eepromPageAddress | ((eeAddress >> 8) & 0x0F);
	uint8_t data = 0xFF;

	uint8_t addr_low_byte = eeAddress & 0xFF;
	i2c_write_blocking(i2c0, deviceAddress, &addr_low_byte, 1, false);

	i2c_read_blocking(i2c0, deviceAddress, &data, 1, false);
	return data;
}

// Read a 16-bit integer from EEPROM
int ReadIntFromEEPROM(unsigned int eeAddress)
{
	uint8_t highByte = ReadByteFromEEPROM(eeAddress);
	uint8_t lowByte = ReadByteFromEEPROM(eeAddress + 1);
	return (highByte << 8) | lowByte;
}

uint16_t CRCencode(const uint8_t *data, int length)
{
	uint16_t crc = 0xFFFF; // Initial CRC value
	for (int i = 0; i < length; i++)
	{
		crc ^= ((uint16_t)data[i]) << 8; // Bring in the next byte
		for (uint8_t bit = 0; bit < 8; bit++)
		{
			if (crc & 0x8000)
			{
				crc = (crc << 1) ^ 0x1021; // CRC-CCITT polynomial
			}
			else
			{
				crc = crc << 1;
			}
		}
	}
	return crc;
}


uint32_t midiToDac(int midiNote, int channel)
{
	int32_t dacValue = ((calCoeffs[channel].mi * (midiNote - 60)) >> 4) + calCoeffs[channel].bi;
	if (dacValue > 524287) dacValue = 524287;
	if (dacValue < 0) dacValue = 0;
	return dacValue;
}

uint32_t midiToDac8(int midiNote8, int channel)
{
	int32_t dacValue = ((calCoeffs[channel].mi * (midiNote8 - (60*256))) >> 12) + calCoeffs[channel].bi;
	if (dacValue > 524287) dacValue = 524287;
	if (dacValue < 0) dacValue = 0;
	return dacValue;
}

void CalcCalCoeffs(int channel)
{
	float sumV = 0.0;
	float sumDAC = 0.0;
	float sumV2 = 0.0;
	float sumVDAC = 0.0;
	int N = numCalibrationPoints[channel];

	for (int i = 0; i < N; i++)
	{
		float v = calibrationTable[channel][i].voltage * 0.1f;
		float dac = calibrationTable[channel][i].dacSetting;
		sumV += v;
		sumDAC += dac;
		sumV2 += v * v;
		sumVDAC += v * dac;
	}

	float denominator = N * sumV2 - sumV * sumV;
	if (denominator != 0)
	{
		calCoeffs[channel].m = (N * sumVDAC - sumV * sumDAC) / denominator;
	}
	else
	{
		calCoeffs[channel].m = 0.0;
	}
	calCoeffs[channel].b = (sumDAC - calCoeffs[channel].m * sumV) / N;

	calCoeffs[channel].mi = (int32_t)(calCoeffs[channel].m * 1.333333333333333f + 0.5f);
	calCoeffs[channel].bi = (int32_t)(calCoeffs[channel].b + 0.5f);
	debug("%d %f %f\n", channel, calCoeffs[channel].m, calCoeffs[channel].b);
}

int ReadEEPROM()
{
	// Set up default values in the calibration table,
	// to be used if EEPROM read fails
	for (int channel = 0; channel < CAL_MAX_CHANNELS; channel++)
	{
		numCalibrationPoints[channel] = 3;
		calibrationTable[channel][0].voltage = -20; // -2V
		calibrationTable[channel][0].dacSetting = 347700;
		calibrationTable[channel][1].voltage = 0; // 0V
		calibrationTable[channel][1].dacSetting = 261200;
		calibrationTable[channel][2].voltage = 20; // +2V
		calibrationTable[channel][2].dacSetting = 174400;
		CalcCalCoeffs(channel);
	}
	if (ReadIntFromEEPROM(EEPROM_ADDR_ID) != EEPROM_VAL_ID)
	{
		debugp("Failed to read EEPROM ID\n");
		return 1;
	}
	uint8_t buf[EEPROM_NUM_BYTES];
	for (int i = 0; i < EEPROM_NUM_BYTES; i++)
	{
		buf[i] = ReadByteFromEEPROM(i);
	}

	#ifdef ENABLE_UART_DEBUGGING
	{
		int eepMajor = (buf[EEPROM_ADDR_VERSION] >> 4) & 0x0F; // Extract bits 7-4
		int eepMinor = (buf[EEPROM_ADDR_VERSION] >> 2) & 0x03; // Extract bits 3-2
		int eepPoint = buf[EEPROM_ADDR_VERSION] & 0x03; // Extract bits 1-0
		debug("EEPROM version %d.%d.%d\n", eepMajor, eepMinor, eepPoint);
	}
	#endif

	uint16_t calculatedCRC = CRCencode(buf, 86);
	uint16_t foundCRC = ((uint16_t)buf[EEPROM_ADDR_CRC_H] << 8) | buf[EEPROM_ADDR_CRC_L];

	if (calculatedCRC != foundCRC)
	{
		debugp("EEPROM CRC check failed\n");
		return 1;
	}

	int bufferIndex = 4;

	for (uint8_t channel = 0; channel < CAL_MAX_CHANNELS; channel++)
	{
		int channelOffset = bufferIndex + (41 * channel); // channel 0 = 4, channel 1 = 45
		numCalibrationPoints[channel] = buf[channelOffset++];
		for (uint8_t point = 0; point < numCalibrationPoints[channel]; point++)
		{
			// Unpack Pack targetVoltage (int8_t) from buf
			int8_t targetVoltage = (int8_t)buf[channelOffset++];

			// Unack dacSetting (uint32_t) from buf (4 bytes)
			uint32_t dacSetting = 0;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 24; // MSB
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 16;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 8;
			dacSetting |= ((uint32_t)buf[channelOffset++]); // LSB

			// Write settings into calibration table
			calibrationTable[channel][point].voltage = targetVoltage;
			calibrationTable[channel][point].dacSetting = dacSetting;
		}
		CalcCalCoeffs(channel);
	}
	for (uint8_t channel = 0; channel < CAL_MAX_CHANNELS; channel++)
	{
		for (uint8_t point = 0; point < numCalibrationPoints[channel]; point++)
		{
			debug("%d %d %d %d\n",
			      channel,
			      point,
			      calibrationTable[channel][point].voltage,
			      calibrationTable[channel][point].dacSetting);
		}
	}
	return 0;
}


void SetupComputerIO()
{

	// Initialize all LEDs
	for (int i = 0; i < NUM_LEDS; i++)
	{
		gpio_init(leds[i]);
		gpio_set_dir(leds[i], GPIO_OUT);
	}

	// Board version ID pins
	gpio_set_dir(BOARD_ID_0, GPIO_IN);
	gpio_set_dir(BOARD_ID_1, GPIO_IN);
	gpio_set_dir(BOARD_ID_2, GPIO_IN);

	// Normalisation probe pin
	gpio_init(NORMALISATION_PROBE);
	gpio_set_dir(NORMALISATION_PROBE, GPIO_OUT);
	gpio_put(NORMALISATION_PROBE, false);

	adc_init(); // Initialize the ADC

	// Set ADC pins
	adc_gpio_init(AUDIO_L_IN_1);
	adc_gpio_init(AUDIO_R_IN_1);
	adc_gpio_init(MUX_IO_1);
	adc_gpio_init(MUX_IO_2);

	// Initialize Mux Control pins
	gpio_init(MX_A);
	gpio_init(MX_B);
	gpio_set_dir(MX_A, GPIO_OUT);
	gpio_set_dir(MX_B, GPIO_OUT);

	// Initialize pulse out
	gpio_init(PULSE_1_RAW_OUT);
	gpio_set_dir(PULSE_1_RAW_OUT, GPIO_OUT);
	gpio_put(PULSE_1_RAW_OUT, true); // set raw value high (output low)

	gpio_init(PULSE_2_RAW_OUT);
	gpio_set_dir(PULSE_2_RAW_OUT, GPIO_OUT);
	gpio_put(PULSE_2_RAW_OUT, true); // set raw value high (output low)

	// Initialize pulse in
	gpio_init(PULSE_1_INPUT);
	gpio_set_dir(PULSE_1_INPUT, GPIO_IN);
	gpio_pull_up(PULSE_1_INPUT); // NB Needs pullup to activate transistor on inputs

	gpio_init(PULSE_2_INPUT);
	gpio_set_dir(PULSE_2_INPUT, GPIO_IN);
	gpio_pull_up(PULSE_2_INPUT); // NB: Needs pullup to activate transistor on inputs


	// Setup SPI for DAC output
	spi_init(SPI_PORT, 15625000);
	spi_set_format(SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
	gpio_set_function(DAC_TX, GPIO_FUNC_SPI);
	gpio_set_function(DAC_CS, GPIO_FUNC_SPI);

	// Setup I2C for EEPROM
	i2c_init(i2c0, 100 * 1000);
	gpio_set_function(EEPROM_SDA, GPIO_FUNC_I2C);
	gpio_set_function(EEPROM_SCL, GPIO_FUNC_I2C);

	// Initialise USB host status pin
	gpio_init(USB_HOST_STATUS);
	gpio_set_dir(USB_HOST_STATUS, GPIO_IN);
	gpio_disable_pulls(USB_HOST_STATUS);



	// Setup CV PWM
	// First, tell the CV pins that the PWM is in charge of the value.
	gpio_set_function(CV_OUT_1, GPIO_FUNC_PWM);
	gpio_set_function(CV_OUT_2, GPIO_FUNC_PWM);

	// now create PWM config struct
	pwm_config config = pwm_get_default_config();
	pwm_config_set_wrap(&config, 2047); // 11-bit PWM


	// now set this PWM config to apply to the two outputs
	// NB: CV_A and CV_B share the same PWM slice, which means that they share a PWM config
	// They have separate 'gpio_level's (output compare unit) though, so they can have different PWM on-times
	pwm_init(pwm_gpio_to_slice_num(CV_OUT_1), &config, true); // Slice 1, channel A
	pwm_init(pwm_gpio_to_slice_num(CV_OUT_2), &config, true); // slice 1 channel B (redundant to set up again)

	// set initial level to half way (0V)
	pwm_set_gpio_level(CV_OUT_1, 1024);
	pwm_set_gpio_level(CV_OUT_2, 1024);

// If not using UART pins for UART, instead use as debug lines
#ifndef ENABLE_UART_DEBUGGING
	// Debug pins
	gpio_init(DEBUG_1);
	gpio_set_dir(DEBUG_1, GPIO_OUT);

	gpio_init(DEBUG_2);
	gpio_set_dir(DEBUG_2, GPIO_OUT);
#endif
}
#endif
