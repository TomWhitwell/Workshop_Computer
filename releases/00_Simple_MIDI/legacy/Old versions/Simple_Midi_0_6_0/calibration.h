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

#include <Arduino.h>
#include "CV.h"
#include "DACChannel.h"
#include "ResponsiveAnalogRead.h"


class Calibration {
  public:
    Calibration() {
      // Initialize calibrationSteps array with PWM channels and target voltages
    }

    void begin(byte MA, byte MI, byte PO) {

      MAJOR_VERSION = MA;
      MINOR_VERSION = MI;
      POINT_VERSION = PO;
      // Initialization code, e.g., load calibration data

      // If Eeprom is blank or corrupted initialise Eeprom
      if (!checkEeprom()) {
//        Serial.println("eeprom not initialised, initialising");
        initialiseEeprom();
      }

      // Attempt to read eeprom, if reading fails - CRC check - then initialise
      if (!readEeprom()) {
//        Serial.println("eeprom corrupt, initialising");
        initialiseEeprom();
        readEeprom();
      }
    }

    void handleCalibration() {






      /*

        // Main calibration loop
        if (currentCalibrationIndex >= totalCalibrationSteps) {
        // Calibration complete
        saveCalibrationData();
        calibrationMode = false;
        return;
        }

        CalibrationEntry* entry = &calibrationSteps[currentCalibrationIndex];

        // Extract PWM channel and target voltage
        char pwmChannel = entry->pwmChannel;
        float targetVoltage = entry->targetVoltage;

        // Deactivate the other PWM output
        deactivateOtherPWM(pwmChannel);

        // Retrieve the current DAC setting (from EEPROM or default)
        uint32_t dacValue = entry->dacSetting;

        // Adjust DAC value based on knob inputs
        adjustCalibrationValue(dacValue);

        // Send DAC value to the active PWM output
        setPWMOutput(pwmChannel, dacValue);

        // Provide feedback to the user
        indicateCalibrationStep(pwmChannel, targetVoltage);

        // Check if the user wants to proceed to the next step
        if (nextStepButtonPressed()) {
        // Save the adjusted DAC value
        entry->dacSetting = dacValue;

        // Move to the next calibration step
        currentCalibrationIndex++;
        }

      */

    }




    // Functions to return calibration variables to main loop
    int8_t returnVoltage(byte channel, byte point) {
      return calibrationTable[channel][point].targetVoltage;
    }

    uint32_t returnSetting(byte channel, byte point) {
      return calibrationTable[channel][point].dacSetting;
    }

    uint8_t returnNumSettings(byte channel) {
      return numCalibrationPoints[channel];
    }


    void setVoltage(byte channel, byte point, int8_t voltage) {
      if (channel >= maxChannels || point >= maxCalibrationPoints) {
        // Invalid channel or point
//        Serial.println("Invalid channel or point in setVoltage");
        return;
      }

      calibrationTable[channel][point].targetVoltage = voltage;

      // Update numCalibrationPoints if necessary
      if (point >= numCalibrationPoints[channel]) {
        numCalibrationPoints[channel] = point + 1;
      }

      writeEeprom();
    }


    void setSetting(byte channel, byte point, uint32_t setting) {
      if (channel >= maxChannels || point >= maxCalibrationPoints) {
        // Invalid channel or point
//        Serial.println("Invalid channel or point in setSetting");
        return;
      }

      calibrationTable[channel][point].dacSetting = setting;

      // Update numCalibrationPoints if necessary
      if (point >= numCalibrationPoints[channel]) {
        numCalibrationPoints[channel] = point + 1;
      }

      writeEeprom();
    }



    uint8_t returnNumChannels() {
      return maxChannels;
    }


    // Check if calibration mode is active
    bool isCalibrationMode() {
      return calibrationMode;
    }

    void setCalibrationMode(bool calibMode){
      calibrationMode = calibMode;
    }

  private:
    struct CalibrationEntry {
      int8_t targetVoltage; // Voltage x 10 - so -50 = -5V, +25 = +2.5V
      uint32_t dacSetting; // DAC setting value - maximum 20 bits
    };

    // Constants

    static const int maxChannels = 2; // Maximum number of calibration channels
    static const int maxCalibrationPoints = 10; // Maximum number of calibration points per channel

    // Member variables
    CalibrationEntry calibrationTable[maxChannels][maxCalibrationPoints];
    int currentCalibrationIndex;
    bool calibrationMode = false;
    int magicNumber = 2001;
    byte eepromPageAddress = 0x50;
    byte MAJOR_VERSION = 0;
    byte MINOR_VERSION = 0;
    byte POINT_VERSION = 0;
    int numCalibrationPoints[maxChannels];



    bool checkEeprom() {
      bool result = false;
      // check eeprom magic number is present
      int magicCandidate = readIntFromEEPROM(0);
      result = (magicCandidate == magicNumber);
      return result;
    }

    bool readEeprom() {
      bool result = false;

//      Serial.println("Reading eeprom");

      byte eepBuffer[88] = {0};

      for (int i = 0; i < 88; i++) {
        eepBuffer[i] = readByteFromEEPROM(i);
      }
      // Ignore Magic Number, already read

      // Decode Version number
      byte eepVersion = eepBuffer[2];

      // Reading back the version numbers from the packed byte
      int eepMajor = (eepVersion >> 4) & 0x0F;  // Extract bits 7-4
      int eepMinor = (eepVersion >> 2) & 0x03;  // Extract bits 3-2
      int eepPoint = eepVersion & 0x03;         // Extract bits 1-0

      // Add some code here to handle version control

      /*
            Serial.print("Version number: ");
            Serial.print(eepMajor);
            Serial.print(" | ");

            Serial.print(eepMinor);
            Serial.print(" | ");

            Serial.print(eepPoint);
            Serial.println(" | ");
      */


      uint16_t calculatedCRC = CRCencode(eepBuffer, 86); // Compute CRC over mapBuffer[0] to mapBuffer[85]
      uint16_t foundCRC = ((uint16_t)eepBuffer[86] << 8) | eepBuffer[87];

      if (calculatedCRC == foundCRC) {
        result = true;
//        Serial.println("Eeprom found and CRC passed");
      }
      else {
//        Serial.println("Eeprom found but CRC failed");
        return result;
      }

      // Now unpack the data from eepBuffer into the calibration table

      int bufferIndex = 4;

      for (byte channel = 0; channel < maxChannels; channel++) {
        int channelOffset = bufferIndex + (41 * channel); // channel 0 = 4, channel 1 = 45
        numCalibrationPoints[channel] = eepBuffer[channelOffset++];
        for (byte point = 0; point < numCalibrationPoints[channel]; point++) {

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

        /*  Serial.print("Channel ");
          Serial.print(channel);
          Serial.println(" Calibration Data:");
          Serial.print(numCalibrationPoints[channel]);
          Serial.println(" calibration points");
          for (byte point = 0; point < numCalibrationPoints[channel]; point++) {
            Serial.print("  Point ");
            Serial.print(point);
            Serial.print(": Target Voltage = ");
            Serial.print(calibrationTable[channel][point].targetVoltage * 0.1, 1);
            Serial.print(" V, DAC Setting = ");
            Serial.println(calibrationTable[channel][point].dacSetting);
          }
        */

      }




      return result;

    }

    void initialiseEeprom() {
      bool result = false;
      // write eeprom with default calibration data
      // create temporary buffer to hold entire memory map
      byte mapBuffer[88] = {0};

      // fill the buffer

      // Magic Number at 0
      addIntToBuffer(mapBuffer, magicNumber, 0);

      // Version at 2
      byte versionByte = (MAJOR_VERSION << 4) | (MINOR_VERSION << 2) | POINT_VERSION;
      mapBuffer[2] = versionByte;

      // Default calibration settings

      const byte numChannels = 2;
      const byte numCalibrationPoints = 3;

      mapBuffer[4] = numCalibrationPoints;

      CalibrationEntry defaults[numChannels][numCalibrationPoints];
      defaults[0][0].targetVoltage = -20; // -2V
      defaults[0][0].dacSetting = 347700;
      defaults[0][1].targetVoltage = 0; // 0V
      defaults[0][1].dacSetting = 261200;
      defaults[0][2].targetVoltage = 20; // +2V
      defaults[0][2].dacSetting = 174400;

      defaults[1][0].targetVoltage = -20; // -2V
      defaults[1][0].dacSetting = 347700;
      defaults[1][1].targetVoltage = 0; // 0V
      defaults[1][1].dacSetting = 261200;
      defaults[1][2].targetVoltage = 20; // +2V
      defaults[1][2].dacSetting = 174400;


      // Pack defaults into buffer

      int bufferIndex = 4;  // Start index after the last used position in mapBuffer

      for (byte channel = 0; channel < numChannels; channel++) {
        int channelOffset = bufferIndex + (41 * channel); // channel 0 = 4, channel 1 = 45
        mapBuffer[channelOffset++] = numCalibrationPoints;
        for (byte point = 0; point < numCalibrationPoints; point++) {
          // Pack targetVoltage (int8_t) into mapBuffer
          mapBuffer[channelOffset++] = (byte)defaults[channel][point].targetVoltage;

          // Pack dacSetting (uint32_t) into mapBuffer (4 bytes)
          mapBuffer[channelOffset++] = (byte)(defaults[channel][point].dacSetting >> 24); // MSB
          mapBuffer[channelOffset++] = (byte)(defaults[channel][point].dacSetting >> 16);
          mapBuffer[channelOffset++] = (byte)(defaults[channel][point].dacSetting >> 8);
          mapBuffer[channelOffset++] = (byte)(defaults[channel][point].dacSetting);       // LSB
        }
      }

      // Compute CRC over mapBuffer[0..85]
      uint16_t crc = CRCencode(mapBuffer, 86); // Compute CRC over mapBuffer[0] to mapBuffer[85]
      addIntToBuffer(mapBuffer, crc, 86);

      uint16_t storedCrc = ((uint16_t)mapBuffer[86] << 8) | mapBuffer[87];
      bool isValid = CRCdecode(mapBuffer, 86, storedCrc);
      Serial.print(" CRC check ");
      Serial.println(isValid ? "pass" : "fail");


      // Write the buffer to EEPROM in 16-byte pages
      unsigned int eeAddress = 0;  // Starting EEPROM address
      int bytesToWrite = 88;       // Total bytes to write
      int bufferOffset = 0;        // Offset in mapBuffer

      while (bytesToWrite > 0) {
        byte pageSize = 16 - (eeAddress % 16); // Remaining bytes in the current EEPROM page
        if (pageSize > bytesToWrite) {
          pageSize = bytesToWrite;
        }

        writePageToEEPROM(eeAddress, &mapBuffer[bufferOffset], pageSize);

        eeAddress += pageSize;
        bufferOffset += pageSize;
        bytesToWrite -= pageSize;
      }



      /*

            Serial.println("Memory Buffer");
            for (int i = 0; i < 88; i++) {
              Serial.print(i);
              Serial.print("=");
              Serial.print( mapBuffer[i]);
              Serial.print("\t");
              if ((i + 1) % 8 == 0)Serial.println("");
            }


            Serial.println("Eeprom storage");
            for (int i = 0; i < 88; i++) {
              Serial.print(i);
              Serial.print("=");
              Serial.print( readByteFromEEPROM(i));
              Serial.print("\t");
              if ((i + 1) % 8 == 0)Serial.println("");
            }


      */


    }


    // New method to write calibration data to EEPROM
    void writeEeprom() {
      // Create a mapBuffer[88]
      byte mapBuffer[88] = {0};

      // Fill the buffer with the data from calibrationTable
      // First, add the magic number and version

      addIntToBuffer(mapBuffer, magicNumber, 0);

      // Version at 2
      byte versionByte = (MAJOR_VERSION << 4) | (MINOR_VERSION << 2) | POINT_VERSION;
      mapBuffer[2] = versionByte;

      // Now, pack the data from calibrationTable into mapBuffer

      int bufferIndex = 4; // Start index after the last used position in mapBuffer

      for (byte channel = 0; channel < maxChannels; channel++) {
        int channelOffset = bufferIndex + (41 * channel); // channel 0 = 4, channel 1 = 45
        mapBuffer[channelOffset++] = numCalibrationPoints[channel];
        for (byte point = 0; point < numCalibrationPoints[channel]; point++) {
          // Pack targetVoltage (int8_t) into mapBuffer
          mapBuffer[channelOffset++] = (byte)calibrationTable[channel][point].targetVoltage;

          // Pack dacSetting (uint32_t) into mapBuffer (4 bytes)
          mapBuffer[channelOffset++] = (byte)(calibrationTable[channel][point].dacSetting >> 24); // MSB
          mapBuffer[channelOffset++] = (byte)(calibrationTable[channel][point].dacSetting >> 16);
          mapBuffer[channelOffset++] = (byte)(calibrationTable[channel][point].dacSetting >> 8);
          mapBuffer[channelOffset++] = (byte)(calibrationTable[channel][point].dacSetting);       // LSB
        }
      }

      // Compute CRC over mapBuffer[0..85]
      uint16_t crc = CRCencode(mapBuffer, 86); // Compute CRC over mapBuffer[0] to mapBuffer[85]
      addIntToBuffer(mapBuffer, crc, 86);

      // Write the buffer to EEPROM in 16-byte pages
      unsigned int eeAddress = 0;  // Starting EEPROM address
      int bytesToWrite = 88;       // Total bytes to write
      int bufferOffset = 0;        // Offset in mapBuffer

      while (bytesToWrite > 0) {
        byte pageSize = 16 - (eeAddress % 16); // Remaining bytes in the current EEPROM page
        if (pageSize > bytesToWrite) {
          pageSize = bytesToWrite;
        }

        writePageToEEPROM(eeAddress, &mapBuffer[bufferOffset], pageSize);

        eeAddress += pageSize;
        bufferOffset += pageSize;
        bytesToWrite -= pageSize;
      }
    }



    // Function to write an integer to EEPROM
    void writeIntToEEPROM(unsigned int eeAddress, int value) {
      byte highByte = (value >> 8) & 0xFF;
      byte lowByte = value & 0xFF;

      writeByteToEEPROM(eeAddress, highByte);
      writeByteToEEPROM(eeAddress + 1, lowByte);
    }

    // Function to read an integer from EEPROM
    int readIntFromEEPROM(unsigned int eeAddress) {
      byte highByte = readByteFromEEPROM(eeAddress);
      byte lowByte = readByteFromEEPROM(eeAddress + 1);
      return (highByte << 8) | lowByte;
    }

    // Function to write a byte to EEPROM
    void writeByteToEEPROM(unsigned int eeAddress, byte data) {
      byte deviceAddress = eepromPageAddress | ((eeAddress >> 8) & 0x0F);

      Wire.beginTransmission(deviceAddress);
      Wire.write((byte)(eeAddress & 0xFF)); // Send lower 8 bits of address
      Wire.write(data);
      Wire.endTransmission();

      // EEPROM write delay (typical write time is 3 ms)
      delay(5);
    }

    // Function to read a byte from EEPROM
    byte readByteFromEEPROM(unsigned int eeAddress) {
      byte deviceAddress = eepromPageAddress | ((eeAddress >> 8) & 0x0F);
      byte data = 0xFF;

      Wire.beginTransmission(deviceAddress);
      Wire.write((byte)(eeAddress & 0xFF)); // Send lower 8 bits of address
      Wire.endTransmission();

      Wire.requestFrom(deviceAddress, (byte)1);
      if (Wire.available()) {
        data = Wire.read();
      }
      return data;
    }

    // Function to write a 16-byte page to EEPROM
    // Function to write a page to EEPROM with acknowledge polling
    void writePageToEEPROM(unsigned int eeAddress, byte* data, byte length) {
      if (length > 16) {
        length = 16;  // Limit the data to 16 bytes for a page write
      }

      // Calculate the device address by combining the base address and the upper bits of the EEPROM address
      byte deviceAddress = eepromPageAddress | ((eeAddress >> 8) & 0x0F);

      // Begin transmission to the EEPROM
      Wire.beginTransmission(deviceAddress);
      Wire.write((byte)(eeAddress & 0xFF)); // Send lower 8 bits of EEPROM address

      // Write up to 16 bytes of data to the EEPROM
      for (byte i = 0; i < length; i++) {
        Wire.write(data[i]);
      }

      // End transmission and initiate the write cycle
      Wire.endTransmission();
      // Acknowledge polling: wait until the EEPROM is ready
      while (true) {
        Wire.beginTransmission(deviceAddress);
        byte error = Wire.endTransmission();
        if (error == 0) {
          break; // EEPROM has acknowledged, write cycle is complete
        }
      }

    }

    void addIntToBuffer(byte* buffer, int value, int position) {
      // Split the int into two bytes (high byte and low byte)
      buffer[position] = (value >> 8) & 0xFF;  // High byte
      buffer[position + 1] = value & 0xFF;     // Low byte
    }

    // CRCencode function to compute CRC-16 (CRC-CCITT)
    uint16_t CRCencode(const byte* data, int length) {
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

    bool CRCdecode(const byte* data, int length, uint16_t storedCrc) {
      uint16_t computedCrc = CRCencode(data, length);
      return (computedCrc == storedCrc);
    }




};

#endif
