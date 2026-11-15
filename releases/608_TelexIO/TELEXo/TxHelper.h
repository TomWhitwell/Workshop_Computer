/*
 * TELEXo Eurorack Module
 * (c) 2016 Brendon Cassidy
 * MIT License
 */
 
#ifndef TxHelper_h
#define TxHelper_h

#include <cstdint>
#include "pico/stdlib.h"

// i2c
//#include <i2c_t3.h>
//#include "Arduino.h"

struct TxResponse {
  uint8_t Command;
  uint8_t Output;
  int16_t Value;
};

struct TxIO {
  short Port;
  short Mode;
};

class TxHelper
{
  public:

    //static TxResponse Parse(size_t len);
    static TxResponse Parse(const uint8_t* buffer, const uint8_t len);
    static TxIO DecodeIO(int io);
    static float VOct2Frequency(int value);
    static unsigned long ConvertMs(unsigned long ms, short format);

  protected:
    
  private:
 

};

#endif

