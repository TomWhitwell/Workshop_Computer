/*
 * TELEXi Eurorack Module
 * (c) 2016,2017 Brendon Cassidy
 * MIT License
 */
 
#include "TxHelper.h"
#include "Arduino.h"

// i2c
#include <i2c_t3.h>

TxResponse TxHelper::Parse(size_t len){

  TxResponse response;

  int buffer[4] = { 0, 0, 0, 0 };

  // zero out the read buffer
  int counterPal = 0;
  memset(buffer, 0, sizeof(buffer));

  // read the data
  while (1 < Wire.available()) {
    if (counterPal < 4) {
      buffer[counterPal++] = Wire.read();
    }
  }
  // get the last byte
  buffer[counterPal] = Wire.read();

  uint16_t temp = (uint16_t)((buffer[2] << 8) + (buffer[3]));
  int16_t temp2 = (int16_t)temp;

  response.Command = buffer[0];
  response.Output = buffer[1];
  response.Value = (int)temp2;
  
  // Serial.printf("temp: %d; temp2: %d; helper: %d\n", temp, temp2, response.Value);

  return response;
  
}

TxIO TxHelper::DecodeIO(int io) {
  
  TxIO decoded;
  
  // turn it into 0-7 for the individual device's port
  decoded.Port = io % 8;
  
  // output mode (0-7 = normal; 8-15 = Quantized; 16-23 = Note Number)
  decoded.Mode = io >> 3;

  return decoded;
}

