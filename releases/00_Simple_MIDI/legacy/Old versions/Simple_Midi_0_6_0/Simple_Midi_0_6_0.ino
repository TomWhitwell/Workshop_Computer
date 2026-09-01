/*

  Simple MIDI
  For Music Thing Workshop Computer
  by Tom Whitwell March-November 2024

  Documentation:
  https://www.musicthing.co.uk/workshopsystem/

  Compilation notes:
  USB Stack: Adafruit TinyUSB
  Board: Generic RP2040
  Boot Stage 2: WS25Q16JVxQ QSPI/4
  Flash Size: 2Mb (no FS)

  NB: Edit the file tusb_config_rp2040.h in your system
  MIDI FIFO size of TX and RX
  -#define CFG_TUD_MIDI_RX_BUFSIZE   64
  -#define CFG_TUD_MIDI_TX_BUFSIZE   64
  +#define CFG_TUD_MIDI_RX_BUFSIZE   512
  +#define CFG_TUD_MIDI_TX_BUFSIZE   512


  Released under MIT License
  Copyright (c) 2024 Tom Whitwell.
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/



#include <Adafruit_TinyUSB.h>
#include "tusb.h"
#include <Arduino.h>
#include <MIDI.h>
#include <SPI.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <Wire.h>
#include "pico/unique_id.h"


// Include local headers
#include "ResponsiveAnalogRead.h"
#include "CV.h"
#include "DACChannel.h"
#include "config.h"
#include "calibration.h"
#include "Click.h"



// Config variables
int MAJOR_VERSION = 0x00;
int MINOR_VERSION = 0x06;
int POINT_VERSION = 0x00;

// MIDI
Adafruit_USBD_MIDI usbdMidi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usbdMidi, MIDI);

const int adcResolutionBits = 12;
int maxRange = 1 << adcResolutionBits;
const int channelCount = 8; // three knobs, 1 switch, 2 CV
ResponsiveAnalogRead *analog[channelCount];
// 0 = Audio In 1    
// 1 = Audio In 2   
// 2 = CV In 1
// 3 = CV In 2
// 4 = Main knob
// 5 = Knob X
// 6 = Knob Y
// 7 = Switch

// The value below which we can assume the switch is down
#define SWITCH_DOWN 340

// Lookup to set knob and cv inputs to MIDI outputs 
// Not sure why I set these in the documentation, but let's make that right 
const uint8_t MIDI_LOOKUP[8] = {39,38,40,41,34,35,36,37}; 

// Define the number of precision PWM DAC channels
#define NUM_DACS 2
DACChannel* dacChannels[NUM_DACS];

// Instantiate CV object
CV cv;


// Tap Tempo section
// Attach Switch momentary 'tap' to two functions
extern void buttonTap();
extern void buttonHold();
Click tap(buttonTap, buttonHold);


// Global variables for reading the mux and managing pulses
int muxCount = 0; // Counter for reading through the mux each time
boolean pulse1 = 0;
boolean pulse2 = 0;

// Global variables for Calibration
Calibration calibration;
bool calibrationMode = false;

void setup() {

  // Set up pins etc
  SetupComputerIO();

  // Initialise MIDI

  TinyUSBDevice.setManufacturerDescriptor("Music Thing Modular");
  TinyUSBDevice.setProductDescriptor("Workshop System MIDI");
  TinyUSBDevice.setSerialDescriptor("Workshop System MIDI");

  MIDI.begin(MIDI_CHANNEL_OMNI); // Listen on every channel
  MIDI.turnThruOff();                      
  MIDI.setHandleNoteOn(noteOnHandler);
  MIDI.setHandleNoteOff(noteOffHandler);
  MIDI.setHandleControlChange(CCHandler);

  usbdMidi.setCableName(0, "Music Thing Workshop System MIDI");

  // Begin calibration (send version number )
  // At this point it will create a blank calibration if none present
  calibration.begin(MAJOR_VERSION, MINOR_VERSION, POINT_VERSION);



  // Now connect up calibration.h to DACChannel.h
  int numberOfChannels = calibration.returnNumChannels();
  for (int chan = 0; chan < numberOfChannels; chan++) {
    int numberOfSettings = calibration.returnNumSettings(chan);
    float voltages[numberOfSettings] = {};
    uint32_t dacValues[numberOfSettings] = {};
    for (int setting = 0; setting < numberOfSettings; setting++) {
      voltages[setting] = calibration.returnVoltage(chan, setting) * 0.1;
      dacValues[setting] = calibration.returnSetting(chan, setting);
    }
    dacChannels[chan] = new DACChannel(numberOfSettings, voltages, dacValues, noteOffset);
    dacChannels[chan]->calculateCalibrationConstants();
  }



  // Check if switch is held down, enter calibration mode
  // Switch the Mux so A2 is connected to the Switch
  digitalWrite(MX_A, 1);
  digitalWrite(MX_B, 1);
  if (analogRead(A2) < SWITCH_DOWN) { // Important fix, was 500 which caused errors.
    calibration.setCalibrationMode(true);
    while (analogRead(A2) < SWITCH_DOWN) {
      ledAnimate2();
      // Waiting for you to let go of the toggle get started
    }
    delay(100);
  }


  // Read through the inputs once
  for (int i = 0; i < 4; i++) {
    muxRead(i);
    muxUpdate(i);
  }


  // Wait until the device is mounted
  // Do this after checking for Calibration mode
  // Don't need to try to connect if in calibration mode
  while (!TinyUSBDevice.mounted() && !calibration.isCalibrationMode()) {
    ledAnimate();
  }
  TinyUSBDevice.task();
  // Turn off leds when mounted
  ledOff();

  // Set Pitch outputs to 0v on startup
  if (!calibration.isCalibrationMode()) {
    const uint8_t INIT_NOTE = 60; // C4
    cv.Set(0, dacChannels[0]->midiToDac(INIT_NOTE));
    cv.Set(1, dacChannels[1]->midiToDac(INIT_NOTE));
  }
}

void loop() {


  TinyUSBDevice.task();          // << NEW – poll USB engine

  // Read analog 'audio' inputs
  analog[0]->update(maxRange - analogRead(A0)); // inverted
  analog[1]->update(maxRange - analogRead(A1)); // inverted

  pulse1 = !digitalRead(PULSE_1_INPUT);
  pulse2 = !digitalRead(PULSE_2_INPUT);

  muxRead(muxCount);
  muxCount++;
  if (muxCount >= 4) muxCount = 0;
  muxUpdate(muxCount); // If results are odd, put a delay after mux Update

  // Switch = analog[7] - this checks to see if the switch is pulled down and triggers the tap debouncing
  tap.Update((analog[7]->getValue() < 200));

  static int8_t lastCC[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };


  if (calibration.isCalibrationMode()) {
    calibrationLoop();
  }

  // don't update midi during calibration
  else {

    for (int i = 0; i < channelCount; ++i) {
      if (analog[i]->hasChanged()) {                // optional pre-filter
        uint8_t ccVal = analog[i]->getValue() >> 5; // 12-bit -> 7-bit
        if (ccVal != lastCC[i]) {                   // only send on 7-bit change
          MIDI.sendControlChange(MIDI_LOOKUP[i], ccVal, 1);
          lastCC[i] = ccVal;
          TinyUSBDevice.task();
        }
      }
    }
    MIDI.read(); // run as fast as possible to update midi
  }


}


int calibratingNow = 0;
unsigned long calibLedMillis = 0;
bool calibLedState = 0;
bool editMode = 0;
bool enteringEditMode = 0;
bool exitingEditMode = 0;
int tempChannel;
int tempPoint;
float tempVoltage = 0.0;
uint32_t tempValue = 0;
bool setVoltage = 1; // set DAC output to relevant voltage
const int calibrationOrder[6][2] = {{0, 1}, {0, 2}, {0, 0}, {1, 1}, {1, 2}, {1, 0}};
const int calibLeds[6] = {2, 0, 4, 3, 1, 5};
const int flashSlow = 400;
const int flashFast = 100;
int coarse = 0;
int fine = 0;


void calibrationLoop() {
  TinyUSBDevice.task();

  // This is the order in which items are calibrated = Left Channel, 0v, Left Channel +2v, Left Channel -2v, then same on right
  // Relevant LED for each calibration
  int interval = (editMode) ? flashSlow : flashFast;


  // Blink LEDS to indicate which value we're reading or editing
  if (millis() - calibLedMillis >= interval) {
    calibLedMillis = millis();
    calibLedState = !calibLedState;
    digitalWrite(leds[calibLeds[calibratingNow]], calibLedState);
  }

  // Check if switch is lifted, to enter or exit edit mode
  bool switchIsUp = (analog[7]->getValue() > 3000);
  if (switchIsUp && !editMode) {
    enteringEditMode = 1;
    //    Serial.println("entering edit mode");
  }
  else if (switchIsUp && editMode) {
    // do nothing, remain in edit mode
  } else if (!switchIsUp && editMode) {
    exitingEditMode = 1;
    //    Serial.println("Exiting edit mode");
  }


  if (enteringEditMode) {
    //        Entering edit mode
    //      - read values from calibration
    //      - Map them to pots so both at 12 o'clock = existing values
    //      - OR so they're mapped so current pot position = existing values

    //    Serial.print("now editing ");
    //    Serial.print(tempVoltage);
    //    Serial.print(" ");
    //    Serial.println(tempValue);
    editMode = 1;
    enteringEditMode = 0;

  }


  if (exitingEditMode) {
    //        Exiting edit mode
    //      Write values to calibration
    //      Update Eeprom


    calibration.setSetting(tempChannel, tempPoint, tempValue);
    //    Serial.print("setting ch:");
    //    Serial.print(tempChannel);
    //    Serial.print(" point: ");
    //    Serial.print(tempPoint);
    //    Serial.print(" to: ");
    //    Serial.println(tempValue);

    editMode = 0;
    exitingEditMode = 0;

  }


  if (editMode) {
    /*

      Ongoing loops
      - read pots
      - set values
      - update DAC
      - Set other Dac to -5

      Exiting edit mode
      Write values to calibration
      Update Eeprom


    */
    bool valueChanged = false;

    // Main knob
    if (analog[4]->hasChanged()) {
      fine = analog[4]->getValue();
      //      Serial.print("Fine ");
      //      Serial.println(fine);
      valueChanged = true;
    }

    // X knob
    if (analog[5]->hasChanged()) {
      coarse = analog[5]->getValue();
      //      Serial.print("Coarse ");
      //      Serial.println(coarse);
      valueChanged = true;
    }

    if (valueChanged) {
      tempValue = adjustInPoint(calibration.returnSetting(tempChannel, tempPoint), coarse, fine);
      setVoltage = true;
    }


  }



  muxRead(muxCount);
  muxCount++;
  if (muxCount >= 4) muxCount = 0;
  muxUpdate(muxCount); // If results are odd, put a delay after mux Update

  // Switch = analog[7] - this checks to see if the switch is pulled down and triggers the tap debouncing
  tap.Update((analog[7]->getValue() < 200));




  if (setVoltage) {

    // If not in edit mode, use the current calibration table unchanged
    if (!editMode) {
      tempChannel = calibrationOrder[calibratingNow][0];
      tempPoint = calibrationOrder[calibratingNow][1];
      tempVoltage = calibration.returnVoltage(tempChannel, tempPoint) * 0.1;
      tempValue = calibration.returnSetting(tempChannel, tempPoint);
    }




    // NB this uses midi as a proxy for voltage at the moment, assuming we're working with octave ranges
    // Could later add a voltToDac function to DACChannel but don't want to risk error with that
    // tempVoltage = voltage as a float
    int setChannel = calibrationOrder[calibratingNow][0];
    cv.Set(setChannel, tempValue);



    setVoltage = 0;
  }

}


void noteOnHandler(byte channel, byte note, byte velocity) {
  // Ignore MIDI in calibration mode
  if (calibration.isCalibrationMode()) return;

  if (channel == 1) {
    digitalWrite(PULSE_1_RAW_OUT, 0); // inverted
    digitalWrite(leds[4], 1); // Pulse 1
    cv.Set(0, dacChannels[0]->midiToDac(note));
    analogWrite(leds[2], (note - 40));

  }
  if (channel == 2) {
    digitalWrite(PULSE_2_RAW_OUT, 0); // inverted
    digitalWrite(leds[5], 1); // Pulse 2
    cv.Set(1, dacChannels[1]->midiToDac(note));
    analogWrite(leds[3], (note - 40));
  }
}

void noteOffHandler(byte channel, byte note, byte velocity) {
  // Ignore MIDI in calibration mode
  if (calibration.isCalibrationMode()) return;

  if (channel == 1) {
    digitalWrite(PULSE_1_RAW_OUT, 1); // inverted
    digitalWrite(leds[4], 0); // Pulse 1

  }
  if (channel == 2) {
    digitalWrite(PULSE_2_RAW_OUT, 1); // inverted
    digitalWrite(leds[5], 0); // Pulse 2
  }

}

void CCHandler(byte channel, byte number, byte value) {
  // Ignore MIDI in calibration mode
  if (calibration.isCalibrationMode()) return;

  int  valueMap = map(value, 0, 127, DAClowPoint, DAChighPoint);

  if (channel == 1 && number == 42) {
    dacWrite(0, valueMap  ); // inverted
    analogWrite(leds[0], value << 1);

  }
  if (channel == 2 && number == 42) {
    dacWrite(1, valueMap); // inverted
    analogWrite(leds[1], value << 1);
  }


}

void buttonTap() {
  calibratingNow++;
  if (calibratingNow >= 6) calibratingNow = 0;
  ledOff();
  setVoltage = true;
}

void buttonHold() {
}


uint32_t adjustInPoint(uint32_t inPoint, uint16_t potCoarse, uint16_t potFine) {
  // Combine potCoarse and potFine into a single value
  uint32_t total_pot_value = ((uint32_t)potCoarse * 4096UL) + ((uint32_t)potFine * 1024UL);

  // Calculate the scaling factor ranging from 95000 to 105000 (representing 95% to 105%)
  uint32_t factor = 105000UL - (uint32_t)(((uint64_t)total_pot_value * 10000UL) / 16777215UL);

  // Adjust inPoint by the scaling factor
  uint32_t outPoint = (uint32_t)(((uint64_t)inPoint * factor) / 100000UL);

  return outPoint;
}
