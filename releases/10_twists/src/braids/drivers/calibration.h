// Music Thing Workshop System Calibration
// Tom Whitwell, Herne Hill, London, October 2024


/*
 * PAGE 0 0x50 memory map for 2 x precision PWM voltage outputs = Channels 0 and 1
 * -----------------------------------------------------------------------------
 * Offset  | Bytes | Contents
 * -----------------------------------------------------------------------------
 * 0       | 2     | Magic number = 2001 - if number is present, EEPROM has been initialized
 * 2       | 1     | Version number 0-255
 * 3       | 1     | Padding
 * 4       | 1     | Channel 0 - Number of entries 0-9
 * 5       | 40    | 10 x 4 byte blocks: 1 x 4 bit voltage + 4 bits space | 1 x 24 bit setting = 32 bits = 4 bytes
 * 45      | 1     | Channel 1 - Number of entries 0-9
 * 46      | 40    | 10 x 4 byte blocks: 1 x 4 bit voltage + 4 bits space | 1 x 24 bit setting = 32 bits = 4 bytes
 * 86      | 2     | CRC Check over previous data
 * 88      |       | END
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/i2c.h"
#include "hardware/flash.h"

#define PIN_EEPROM_SDA  16
#define PIN_EEPROM_SCL  17
#define EEPROM_SPEED    400000
#define CALIBRATION_CONSTANT_M  0
#define CALIBRATION_CONSTANT_B  1
#define CALIBRATION_CVIN_MAGIC1 0x72
#define CALIBRATION_CVIN_MAGIC2 0x77
#define CALIBRATION_CVIN_SLOPE_DEFAULT      -0.002948224598
#define CALIBRATION_CVIN_INTERCEPT_DEFAULT  6.144412677

class Calibration {
  public:
    Calibration() {
      // Initialize calibrationSteps array with PWM channels and target voltages
    }

    void init() {

      initialiseHardware();

      // Initialization code, e.g., load calibration data

      // If Eeprom is blank or corrupted initialise Eeprom
      if (!checkEeprom()) {
        initialiseEeprom();
      }

      // Attempt to read eeprom, if reading fails - CRC check - then initialise
      if (!readEeprom()) {
        initialiseEeprom();
      }

      calculateCalibrationConstants();
    }

    // Functions to return calibration variables to main loop
    int8_t returnVoltage(uint8_t channel, uint8_t point) {
      return calibrationTable[channel][point].targetVoltage;
    }

    uint32_t returnSetting(uint8_t channel, uint8_t point) {
      return calibrationTable[channel][point].dacSetting;
    }

    uint8_t returnNumSettings(uint8_t channel) {
      return numCalibrationPoints[channel];
    }

    uint8_t returnNumChannels() {
      return maxChannels;
    }

    uint32_t voltageToCalibratedOut(int channel, float voltage) {
        if(channel >= maxChannels)
          return 0;
        
        if(voltage < -6.0) voltage = -6.0;
        if(voltage > 6.0) voltage = 6.0;

        float dacValueFloat = calibrationConstants[channel][CALIBRATION_CONSTANT_M] * voltage + calibrationConstants[channel][CALIBRATION_CONSTANT_B];
        uint32_t dacValue = (uint32_t)(dacValueFloat + 0.5);  // Round to nearest integer

        // Ensure DAC value is within 19-bit range
        if (dacValue > 524287) dacValue = 524287;
        if (dacValue < 0)      dacValue = 0;

        return dacValue;
    }

    void readCVInCalibration(float *slope, float *intercept) {
      uint8_t *flash_data = (uint8_t *) (XIP_BASE + calibrationFlashAddr_);
      if(flash_data[0] == CALIBRATION_CVIN_MAGIC1 && flash_data[1] == CALIBRATION_CVIN_MAGIC2) {
        ((uint8_t*)slope)[0] = flash_data[2];
        ((uint8_t*)slope)[1] = flash_data[3];
        ((uint8_t*)slope)[2] = flash_data[4];
        ((uint8_t*)slope)[3] = flash_data[5];

        ((uint8_t*)intercept)[0] = flash_data[6];
        ((uint8_t*)intercept)[1] = flash_data[7];
        ((uint8_t*)intercept)[2] = flash_data[8];
        ((uint8_t*)intercept)[3] = flash_data[9];
      }
      else
      {
        *slope = CALIBRATION_CVIN_SLOPE_DEFAULT;
        *intercept = CALIBRATION_CVIN_INTERCEPT_DEFAULT;
      }
    }

    void writeCVInCalibration(float slope, float intercept) {
      uint8_t data[10] = {
        CALIBRATION_CVIN_MAGIC1,
        CALIBRATION_CVIN_MAGIC2,
        ((uint8_t*)&slope)[0],
        ((uint8_t*)&slope)[1],
        ((uint8_t*)&slope)[2],
        ((uint8_t*)&slope)[3],
        ((uint8_t*)&intercept)[0],
        ((uint8_t*)&intercept)[1],
        ((uint8_t*)&intercept)[2],
        ((uint8_t*)&intercept)[3]
      };

      // shut down the other core
			multicore_lockout_start_blocking();
			// erase page of flash
			uint32_t ints = save_and_disable_interrupts();
			flash_range_erase(calibrationFlashAddr_, 4096);
			restore_interrupts(ints);

			// write config to flash
			ints = save_and_disable_interrupts();
			flash_range_program(calibrationFlashAddr_, data, 10);
			restore_interrupts(ints);

			// restore other core
			multicore_lockout_end_blocking();
    }

  private:
    uint32_t calibrationFlashAddr_ = (PICO_FLASH_SIZE_BYTES - 8192) - (PICO_FLASH_SIZE_BYTES - 8192)%4096;

    struct CalibrationEntry {
      int8_t targetVoltage; // Voltage x 10 - so -50 = -5V, +25 = +2.5V
      uint32_t dacSetting; // DAC setting value - maximum 20 bits
    };

    // Constants
    static const int maxChannels = 2; // Maximum number of calibration channels
    static const int maxCalibrationPoints = 10; // Maximum number of calibration points per channel

    // Member variables
    CalibrationEntry calibrationTable[maxChannels][maxCalibrationPoints];
    float calibrationConstants[maxChannels][2];

    int magicNumber = 2001;
    uint8_t eepromPageAddress = 0x50;
    int numCalibrationPoints[maxChannels];

    void initialiseHardware() {
      i2c_init(i2c_default, EEPROM_SPEED);
      gpio_set_function(PIN_EEPROM_SDA, GPIO_FUNC_I2C);
      gpio_set_function(PIN_EEPROM_SCL, GPIO_FUNC_I2C);
      gpio_pull_up(PIN_EEPROM_SDA);
      gpio_pull_up(PIN_EEPROM_SCL);
    }

    bool checkEeprom() {
      bool result = false;
      // check eeprom magic number is present
      int magicCandidate = readIntFromEEPROM(0);
      result = (magicCandidate == magicNumber);
      return result;
    }

    bool readEeprom() {
      bool result = false;
      uint8_t eepBuffer[88] = {0};

      for (int i = 0; i < 88; i++) {
        eepBuffer[i] = readByteFromEEPROM(i);
      }
      // Ignore Magic Number, already read

      // Decode Version number
      uint8_t eepVersion = eepBuffer[2];

      // Reading back the version numbers from the packed byte
      int eepMajor = (eepVersion >> 4) & 0x0F;  // Extract bits 7-4
      int eepMinor = (eepVersion >> 2) & 0x03;  // Extract bits 3-2
      int eepPoint = eepVersion & 0x03;         // Extract bits 1-0

      // Add some code here to handle version control

      uint16_t calculatedCRC = CRCencode(eepBuffer, 86); // Compute CRC over mapBuffer[0] to mapBuffer[85]
      uint16_t foundCRC = ((uint16_t)eepBuffer[86] << 8) | eepBuffer[87];

      if (calculatedCRC == foundCRC) {
        result = true;
      }
      else {
        return result;
      }

      // Now unpack the data from eepBuffer into the calibration table
      int bufferIndex = 4;

      for (uint8_t channel = 0; channel < maxChannels; channel++) {
        int channelOffset = bufferIndex + (41 * channel); // channel 0 = 4, channel 1 = 45
        numCalibrationPoints[channel] = eepBuffer[channelOffset++];
        for (uint8_t point = 0; point < numCalibrationPoints[channel]; point++) {

          // Unpack Pack targetVoltage (int8_t) from eepBuffer
          int8_t targetVoltage = (int8_t)eepBuffer[channelOffset++];

          // Unack dacSetting (uint32_t) from eepBuffer (4 bytes)
          uint32_t dacSetting = 0;
          dacSetting |= ((uint32_t)eepBuffer[channelOffset++]) << 24; // MSB
          dacSetting |= ((uint32_t)eepBuffer[channelOffset++]) << 16;
          dacSetting |= ((uint32_t)eepBuffer[channelOffset++]) << 8;
          dacSetting |= ((uint32_t)eepBuffer[channelOffset++]);       // LSB

          // Write settings into calibration table
          calibrationTable[channel][point].targetVoltage = targetVoltage;
          calibrationTable[channel][point].dacSetting = dacSetting;

        }
      }

      return result;
    }

    void initialiseEeprom() {
      calibrationTable[0][0].targetVoltage = -20; // -2V
      calibrationTable[0][0].dacSetting = 347700;
      calibrationTable[0][1].targetVoltage = 0; // 0V
      calibrationTable[0][1].dacSetting = 261200;
      calibrationTable[0][2].targetVoltage = 20; // +2V
      calibrationTable[0][2].dacSetting = 174400;

      calibrationTable[1][0].targetVoltage = -20; // -2V
      calibrationTable[1][0].dacSetting = 347700;
      calibrationTable[1][1].targetVoltage = 0; // 0V
      calibrationTable[1][1].dacSetting = 261200;
      calibrationTable[1][2].targetVoltage = 20; // +2V
      calibrationTable[1][2].dacSetting = 174400;
    }

    // Function to read an integer from EEPROM
    int readIntFromEEPROM(unsigned int eeAddress) {
      uint8_t highByte = readByteFromEEPROM(eeAddress);
      uint8_t lowByte = readByteFromEEPROM(eeAddress + 1);
      return (highByte << 8) | lowByte;
    }

    // Function to read a byte from EEPROM
    uint8_t readByteFromEEPROM(unsigned int eeAddress) {
      uint8_t deviceAddress = eepromPageAddress | ((eeAddress >> 8) & 0x0F);
      uint8_t data = 0xFF;

      uint8_t write_value = (uint8_t)(eeAddress & 0xFF);
      i2c_write_blocking(i2c_default, deviceAddress, &write_value, 1, true);

      uint8_t read_value = 0;
      // if we get our 1 byte back, use the read value
      if(i2c_read_blocking(i2c_default, deviceAddress, &read_value, 1, true) == 1) {
        data = read_value;
      }

      return data;
    }

    // CRCencode function to compute CRC-16 (CRC-CCITT)
    uint16_t CRCencode(const uint8_t* data, int length) {
      uint16_t crc = 0xFFFF; // Initial CRC value
      for (int i = 0; i < length; i++) {
        crc ^= ((uint16_t)data[i]) << 8; // Bring in the next byte
        for (uint8_t bit = 0; bit < 8; bit++) {
          if (crc & 0x8000) {
            crc = (crc << 1) ^ 0x1021; // CRC-CCITT polynomial
          } else {
            crc = crc << 1;
          }
        }
      }
      return crc;
    }

    void calculateCalibrationConstants() {     
      for(int channel=0; channel < maxChannels; channel++) {
        float sumV = 0.0;
        float sumDAC = 0.0;
        float sumV2 = 0.0;
        float sumVDAC = 0.0;
        
        int N = numCalibrationPoints[channel];
        for (int i = 0; i < N; i++) {
          float v = calibrationTable[channel][i].targetVoltage * 0.1;
          sumV += v;
          sumDAC += calibrationTable[channel][i].dacSetting;
          sumV2 += v * v;
          sumVDAC += v * calibrationTable[channel][i].dacSetting;
        }

        float denominator = N * sumV2 - sumV * sumV;
        if (denominator != 0) {
          calibrationConstants[channel][CALIBRATION_CONSTANT_M] = (N * sumVDAC - sumV * sumDAC) / denominator;
          calibrationConstants[channel][CALIBRATION_CONSTANT_B] = (sumDAC - calibrationConstants[channel][CALIBRATION_CONSTANT_M] * sumV) / N;
        } else {
          // Handle division by zero
          calibrationConstants[channel][CALIBRATION_CONSTANT_M] = 0.0;
          calibrationConstants[channel][CALIBRATION_CONSTANT_B] = sumDAC / N;
        }
      }
    }
};

#endif