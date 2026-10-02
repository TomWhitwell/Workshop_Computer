// DACCHannel.h 

// Turns Midi notes into 19 bit values for the DAC based on calibration points  

#ifndef DACCHANNEL_H
#define DACCHANNEL_H

#include <Arduino.h>

class DACChannel {
public:
  // Calibration data points
  int numPoints;        // Number of calibration points
  float* voltages;      // Array of voltages
  uint32_t* dacValues;  // Array of DAC values

  // Calibration constants
  float m; // Slope
  float b; // Intercept

  // MIDI to Voltage Conversion Constants
  int noteOffset;      // MIDI note number corresponding to 0V
  float vPerOctave;    // Voltage per octave

  // Constructor
  DACChannel(int numPoints, float* voltages, uint32_t* dacValues, int noteOffset = 60, float vPerOctave = 1.0);

  // Destructor
  ~DACChannel();

  // Methods
  void calculateCalibrationConstants();
  uint32_t midiToDac(int midiNote);
};


DACChannel::DACChannel(int numPoints, float* voltages, uint32_t* dacValues, int noteOffset, float vPerOctave)
  : numPoints(numPoints), noteOffset(noteOffset), vPerOctave(vPerOctave) {
  // Allocate memory for arrays
  this->voltages = new float[numPoints];
  this->dacValues = new uint32_t[numPoints];

  // Copy calibration data
  for (int i = 0; i < numPoints; i++) {
    this->voltages[i] = voltages[i];
    this->dacValues[i] = dacValues[i];
  }

  // Initialize calibration constants
  m = 0.0;
  b = 0.0;
}

DACChannel::~DACChannel() {
  // Free allocated memory
  delete[] voltages;
  delete[] dacValues;
}

void DACChannel::calculateCalibrationConstants() {
  // Perform linear regression using the calibration data
  float sumV = 0.0;
  float sumDAC = 0.0;
  float sumV2 = 0.0;
  float sumVDAC = 0.0;
  int N = numPoints;

  for (int i = 0; i < N; i++) {
    sumV += voltages[i];
    sumDAC += dacValues[i];
    sumV2 += voltages[i] * voltages[i];
    sumVDAC += voltages[i] * dacValues[i];

//Serial.print(i); 
//Serial.print(" Voltages ");
//Serial.print(voltages[i]); 
//Serial.print(" Values ");
//Serial.println(dacValues[i]); 
    
  }

  float denominator = N * sumV2 - sumV * sumV;
  if (denominator != 0) {
    m = (N * sumVDAC - sumV * sumDAC) / denominator;
    b = (sumDAC - m * sumV) / N;
  } else {
    // Handle division by zero
    m = 0.0;
    b = sumDAC / N;
  }

  // Debug output
//  Serial.print("Calibration slope (m): ");
//  Serial.println(m);
//  Serial.print("Calibration intercept (b): ");
//  Serial.println(b);
}

uint32_t DACChannel::midiToDac(int midiNote) {
  float voltage = (midiNote - noteOffset) / 12.0 * vPerOctave;

//  // Limit voltage to calibration range
//  float minVoltage = voltages[0];
//  float maxVoltage = voltages[0];
//  for (int i = 1; i < numPoints; i++) {
//    if (voltages[i] < minVoltage) minVoltage = voltages[i];
//    if (voltages[i] > maxVoltage) maxVoltage = voltages[i];
//  }
//
//  if (voltage < minVoltage) voltage = minVoltage;
//  if (voltage > maxVoltage) voltage = maxVoltage;

  float dacValueFloat = m * voltage + b;
  uint32_t dacValue = (uint32_t)(dacValueFloat + 0.5);  // Round to nearest integer

  // Ensure DAC value is within 19-bit range
  if (dacValue > 524287) dacValue = 524287;
  if (dacValue < 0)      dacValue = 0;

  return dacValue;
}







#endif
