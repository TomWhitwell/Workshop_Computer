/*
 * TELEXo Eurorack Module
 * (c) 2016 Brendon Cassidy
 * MIT License
 */
 
#include "TxHelper.h"
//#include "fastexp.h"
//#include "Arduino.h"
#include <cmath>

// i2c
//#include <i2c_t3.h>

TxResponse __not_in_flash_func(TxHelper::Parse)(const uint8_t* buffer, const uint8_t len){

  TxResponse response;

  response.Command = buffer[0];
  
  response.Output  = len > 1 ? buffer[1] : 0;
  
  uint16_t temp = 0;
  if (len > 2) temp |= static_cast<uint16_t>(buffer[2]) << 8;
  if (len > 3) temp |= buffer[3];
  response.Value = static_cast<int16_t>(temp);
  
  return response;
}

TxIO __not_in_flash_func(TxHelper::DecodeIO)(int io) {
  
  TxIO decoded;
  
  // turn it into 0-7 for the individual device's port
  decoded.Port = io % 8;
  
  // output mode (0-7 = normal; 8-15 = Quantized; 16-23 = Note Number)
  decoded.Mode = io >> 3;

  return decoded;
}

/*
 * Takes vOct between 0 and 16383 and convert them to frequencies
 */
/*
float __not_in_flash_func(TxHelper::VOct2Frequency)(int value){
   //return 16.351597831287414 * fastpow2((value / 1638.3) - 1.);
}
*/

constexpr float INV_1638_3 = 1.0f / 1638.3f;
float __not_in_flash_func(TxHelper::VOct2Frequency)(int value){
   return 8.1757989f * exp2f(static_cast<float>(value) * INV_1638_3);
}

unsigned long __not_in_flash_func(TxHelper::ConvertMs)(unsigned long ms, short format){
  
  switch(format){
    
    // seconds
    case 1:
      ms *= 1000;
      break;

    // minutes
    case 2:
      ms *= 60000;
      break;

    // bpm
    case 3:
      ms = 60000 / ms;
      break;
      
  }

  return ms;
  
}

