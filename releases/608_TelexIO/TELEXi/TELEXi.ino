/*
 * TELEXi Eurorack Module
 * (c) 2016, 2017 Brendon Cassidy
 * MIT License
 */
 
// debug flag turns on serial debugging over USB
// #define DEBUG 1

// i2c Wire library (for Teensy)
#include <i2c_t3.h>
#include <EEPROM.h>

// support libraries
#include "telex.h"
#include "Quantizer.h"
#include "AnalogReader.h"
#include "TxHelper.h"

/*
 * Ugly Globals
 */

// i2c pullup setting
bool enablePullups = false;

// config inputs
int configPins[] = { 2, 1, 0 };
int configID = TI;

// logging in the loop
#define LOGINTERVAL 250 // 1000
#define LEDINTERVAL 1000

// inputs, readers and storage
int inputs[] = { A6, A7, A8, A9, A1, A3, A0, A2 };
AnalogReader *analogReaders[8];
QuantizeResponse qresponse;
int volatile inputValue[8];
int volatile quantizedValue[8];
int volatile quantizedNote[8];

// read timer and its local variables
IntervalTimer readTimer;
int p = 0;

// quantizers and quantizer state
Quantizer *quant[8];

// i2c transmission stuff
byte buffer[4];
int targetOutput = 0;

// i2c slave transmit
byte activeInput = 0;
byte activeMode = 0;

#ifdef DEBUG
unsigned long logInterval = 0;
// led status for tx/rx
int LED = 13;
unsigned long ledInterval;
bool ledOn = false;
#endif

/*
 * Setup Function
 */
void setup() {

  int i;
  
  // config
  int cfg = 0;
  for (i=0; i < 3; i++){
    pinMode(configPins[i], INPUT);
    cfg += digitalRead(configPins[i]) << i;
  }
  configID += cfg;  

  // TELEXi uses the standard Teensy analog inputs which have 13 bit usable resolution
  analogReadResolution(13);

  // debugging nonsense
#ifdef DEBUG
  // set the behind-the-scenes LED output pin
  pinMode(LED, OUTPUT);
  logInterval = millis() + LOGINTERVAL;
  Serial.begin(9600);
  // wait for debugging connection   
  while (!Serial);
  Serial.printf("ConfigID: %d\n", configID);
#endif

  // initialize the readers, input values and quantizers
  for (i=0; i <8; i++) {
    analogReaders[i] = new AnalogReader(inputs[i], i >= 4);
    inputValue[i] = 0;
    quant[i] = new Quantizer(0);
  }

  // read the calibration data from EEPROM
  readCalibrationData();

#ifdef DEBUG
  // take a quick pause (for the calibration data to print for debugging)
  delay(1000);
#endif

  // start the read timer
  readTimer.begin(readInputs, 1000); // 500

  // enable i2c and connect the event callbacks
  Wire.begin(I2C_SLAVE, configID, I2C_PINS_18_19, enablePullups ? I2C_PULLUP_INT : I2C_PULLUP_EXT, I2C_RATE_400); // I2C_RATE_2400 // I2C_PULLUP_EXT
  Wire.onReceive(receiveEvent);  
  Wire.onRequest(requestEvent);

}

/*
 * the read input timer interrupt
 * need to be careful with what we access and do here
 * this function is pushing it with the quantization and stuff
 */
void readInputs(){
  // loop through the 8 inputs and store the latest value 
  for (p=0; p < 8; p++){
    inputValue[p] = analogReaders[p]->Read();
    // handle the quantized response
    qresponse = quant[p]->Quantize(inputValue[p]);
    quantizedValue[p] = qresponse.Value;
    quantizedNote[p] = qresponse.Note;
  }
}

/*
 * a simple debugging print loop - all other actions happen in the callbacks and timers
 */
void loop() {

#ifdef DEBUG
    // print stuff
    if (millis() >= logInterval) {
        for (int l=0; l < 8; l++)
          Serial.printf("%d=%d; ", l, inputValue[l]);
        Serial.printf("\n");
        logInterval = millis() + LOGINTERVAL;
    }

    // turn off LED
    if (ledOn && millis() > ledInterval) {
      ledOn = false;
      ledInterval = 0;
    }
#endif
  
}


/*
 * receive the event from the i2c wire library
 */
void receiveEvent(size_t len) {

#ifdef DEBUG
  // set LED active to indicate data transfer
  digitalWrite(LED, HIGH);
  ledOn = true;
  ledInterval = millis() + LEDINTERVAL;
#endif

  // parse the response
  TxResponse response = TxHelper::Parse(len);

  // true command our setting of the input for a read?
  if (len == 1) {
    
    TxIO io = TxHelper::DecodeIO(response.Command);
    
#ifdef DEBUG
    Serial.printf("Port: %d; Mode: %d [%d]\n", io.Port, io.Mode, response.Command);
#endif
    
    // this is the single byte that sets the active input
    activeInput = io.Port;
    activeMode = io.Mode;
    
  } else {
    // act on the command
    actOnCommand(response.Command, response.Output, response.Value);
  }
  
}

/*
 * this is when the master is requesting data from an input
 * we return the int (which is cast to unsigned so the sign can survive the transit)
 */
void requestEvent() {

  // disable interrupts. get and cast the value
  uint16_t shiftReady = 0;
  switch(activeMode){
    case 1:
      noInterrupts();
      shiftReady = (uint16_t)quantizedValue[activeInput];
      interrupts();
      break;
    case 2:
      noInterrupts();
      shiftReady = (uint16_t)quantizedNote[activeInput];
      interrupts();
      break;
    default:
      noInterrupts();
      shiftReady = (uint16_t)inputValue[activeInput];
      interrupts();
      break;
  }
  
#ifdef DEBUG
  Serial.printf("delivering: %d; value: %d [%d]\n", activeInput, inputValue[activeInput], shiftReady);
#endif

  // send the puppy as a pair of bytes
  Wire.write(shiftReady >> 8);
  Wire.write(shiftReady & 255);
}


/*
 * act on commands delivered over i2c
 * command list is in the shared telex.h file
 */
void actOnCommand(byte cmd, byte out, int value){

  byte outHelper = out;

  // act on your commands
  switch (cmd) {

    case TI_IN_SCALE:
      outHelper += 4;
    case TI_PARAM_SCALE:
      quant[outHelper]->SetScale(value);
      break;

    case TI_IN_TOP:
      outHelper += 4;
    case TI_PARAM_TOP:
      analogReaders[outHelper]->SetTop(value);
      break;

    case TI_IN_BOT:
      outHelper += 4;
    case TI_PARAM_BOT:
      analogReaders[outHelper]->SetBottom(value);
      break;

    case TI_IN_CALIB:  
      outHelper += 4;  
    case TI_PARAM_CALIB:
      analogReaders[outHelper]->Calibrate(value);
      break;    

    case TI_STORE:
      saveCalibrationData();
      break;

    case TI_RESET:
      resetCalibrationData();
      break;
  }

#ifdef DEBUG
  Serial.printf("Action: %d, Output: %d\n", cmd, outHelper);
#endif
  
}



/*
 * saves the calibration data to the Teensy's EEPROM
 * and is careful to write only what has changed
 */
void saveCalibrationData() {

  int bitPosition = 0;
  
  uint16_t uInt16t = 0;

  // Look for the TXi Tag
  // "TXi "
  if (EEPROM.read(bitPosition) != 84) EEPROM.write(bitPosition, 84);
  if (EEPROM.read(++bitPosition) != 88) EEPROM.write(bitPosition, 88);
  if (EEPROM.read(++bitPosition) != 105) EEPROM.write(bitPosition, 105);
  if (EEPROM.read(++bitPosition) != 32) EEPROM.write(bitPosition, 32);
  ++bitPosition;
  
  for (int i=0; i < 8; i++) {
    
      int cdata[3];
      analogReaders[i]->GetCalibrationData(cdata);
      bool calibrated = analogReaders[i]->GetCalibrated();
      
      if (EEPROM.read(bitPosition) != calibrated ? 1 : 0) EEPROM.write(bitPosition, calibrated ? 1 : 0);
      ++bitPosition;
    
    for (int q=0; q< 3; q++) {
      uInt16t = (uint16_t)cdata[q];
      byte one = uInt16t & 255;
      byte two = uInt16t >> 8;
      if (EEPROM.read(bitPosition) != one) EEPROM.write(bitPosition, one);
      if (EEPROM.read(++bitPosition) != two) EEPROM.write(bitPosition, two);
      ++bitPosition;
    }
  }
    
}

/*
 * resets the calibration data to defaults
 */
void resetCalibrationData() {
  for(int i=0;i<8;i++){
    analogReaders[i]->SetCalibrationData(0, i < 4 ? 0 : -16384);
    analogReaders[i]->SetCalibrationData(1, 0);
    analogReaders[i]->SetCalibrationData(2, 16384);
  } 
}

/*
 * reads the calibration data from the Teensy's EEPROM
 */
void readCalibrationData(){
  
  int bitPosition = 0;

  uint16_t uInt16t = 0;
  
  // Look for the TXi Tag
  // "TXi "
  if (EEPROM.read(bitPosition++) == 84 && EEPROM.read(bitPosition++) == 88 && EEPROM.read(bitPosition++) == 105 && EEPROM.read(bitPosition++) == 32) {
   
    for (int i=0; i < 8; i++) {
        analogReaders[i]->SetCalibrated(EEPROM.read(bitPosition++) >= 1);
      for (int q=0; q< 3; q++) {
        uInt16t = EEPROM.read(bitPosition) + (EEPROM.read(bitPosition + 1) << 8);
        bitPosition += 2;
        analogReaders[i]->SetCalibrationData(q, (int16_t)uInt16t);
      }
    }
  
#ifdef DEBUG
  } else {
    Serial.print("skipping - eprom not initialized\n");
#endif
  }
  
}



