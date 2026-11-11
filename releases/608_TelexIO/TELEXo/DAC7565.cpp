// **********************************************************************************
// Driver definition for TI DAC7565, DAC7564, DAC8164 and DAC8564 Library
// **********************************************************************************
// Creative Commons Attrib Share-Alike License
// You are free to use/extend this library but please abide with the CC-BY-SA license:
// http://creativecommons.org/licenses/by-sa/4.0/
//
// For any explanation see DAC7565 information at
// http://www.ti.com/product/dac7565
//
// Code based on following datasheet
// http://www.ti.com/lit/gpn/dac7565 
//
// Written by Charles-Henri Hallard (http://hallard.me)
//
// History : V1.00 2015-04-14 - First release
//
// All text above must be included in any redistribution.
//
// **********************************************************************************
#include "DAC7565.h"
#include <SPI.h>

// Class Constructor
DAC::DAC(uint8_t enable_pin, uint8_t sync_pin, uint8_t ldac_pin, uint8_t data_pin, uint8_t clock_pin)
{       
  _enable_pin = enable_pin;
  _sync_pin   = sync_pin;
  _ldac_pin   = ldac_pin;
  _data_pin   = data_pin;
  _clock_pin  = clock_pin;

  // Hardware SPI
  // if (clock_pin==-1 && data_pin==-1)
  _hw_spi = true;
}

/* ======================================================================
Function: init
Purpose : Initialize DAC 
Input   : -
Output  : -
Comments: 
====================================================================== */
void DAC::init(void)
{ 
/*  
  // Default SPI SS pin if not specified
  if (_enable_pin == -1)
    _enable_pin = SS;

  pinMode(_enable_pin, OUTPUT);
  digitalWrite(_enable_pin, HIGH);
*/

  Serial.print("in init for dac\n");

  // Sync HIGH
  if (_sync_pin != -1)
  {
    pinMode(_sync_pin, OUTPUT);
    digitalWrite(_sync_pin, HIGH);
  }

  // LDAC to low
  if (_ldac_pin != -1)
  {
    pinMode(_ldac_pin, OUTPUT);
    digitalWrite(_ldac_pin, LOW);
  }

  // Hardware SPI init
  if (_hw_spi)
  {
    Serial.print("Starting SPI\n");
    SPI.begin();
    SPI.setBitOrder(MSBFIRST);

    // Max speed, we can go up to 50MHz with DAC
    SPI.setClockDivider(SPI_CLOCK_DIV2);
    Serial.printf("Bitorder: %d; Clock Divider: %d\n", MSBFIRST, SPI_CLOCK_DIV2);
  }
  else
  {
    pinMode(_clock_pin, OUTPUT);
    //digitalWrite(_clock_pin, LOW);
    pinMode(_data_pin, OUTPUT);
  }

}

/* ======================================================================
Function: write
Purpose : write the 24 bits full data to the DAC
Input   : 24 bits data value
Output  : 
Comments: 
====================================================================== */
void DAC::write(uint32_t data)
{ 
  uint8_t datahigh, datamid, datalow;

  if (_enable_pin != -1)
    digitalWrite(_enable_pin, LOW);

  datahigh = (uint8_t) ((data >> 16) & 0xFF); 
  datamid  = (uint8_t) ((data >>  8) & 0xFF);
  datalow  = (uint8_t) ((data >>  0) & 0xFF);
/*
  Serial.print(F("SPI Data1=")); Serial.print(datahigh,BIN);
  Serial.print(F(" Data2="));    Serial.print(datamid,BIN);
  Serial.print(F(" Data3="));    Serial.println(datalow,BIN);

  Serial.print(F("SPI Data1=")); Serial.print(datahigh,HEX);
  Serial.print(F(" Data2="));    Serial.print(datamid,HEX);
  Serial.print(F(" Data3="));    Serial.println(datalow,HEX);
*/

  if (_sync_pin != -1)
    digitalWrite(_sync_pin, LOW);

  //SPI.transfer((uint8_t) ((data >> 16) & 0xFF) );
  //SPI.transfer((uint8_t) ((data >>  8) & 0xFF) );
  //SPI.transfer((uint8_t) ((data >>  0) & 0xFF) );

  // Hardware SPI ?
  if (_hw_spi)
  {
    SPI.transfer(datahigh);
    SPI.transfer(datamid);
    SPI.transfer(datalow);
  }
  else
  {
    shiftOut(_data_pin, _clock_pin, MSBFIRST, datahigh);  
    shiftOut(_data_pin, _clock_pin, MSBFIRST, datamid);  
    shiftOut(_data_pin, _clock_pin, MSBFIRST, datalow);  
  }

  if (_sync_pin != -1) 
    digitalWrite(_sync_pin, HIGH);

  if (_enable_pin != -1)
    digitalWrite(_enable_pin, HIGH);
}

/* ======================================================================
Function: setReference
Purpose : write the power up reference config of the DAC
Input   : DAC_REFERENCE_ALWAYS_POWERED_DOWN or
          DAC_REFERENCE_POWERED_TO_DEFAULT or
          DAC_REFERENCE_ALWAYS_POWERED_UP
Output  : 
Comments: 
====================================================================== */
void DAC::setReference(uint16_t reference)
{ 
  uint32_t data = DAC_MASK_PD0;

  // set reference mde
  data |= reference;

  write(data);

}

/* ======================================================================
Function: writeChannel
Purpose : write value to a channel
Input   : DAC_CHANNEL_A to DAC_CHANNEL_D or DAC_CHANNEL_ALL
          data value (in 12 LSB bits)
Output  : 
Comments: 
====================================================================== */
void DAC::writeChannel(uint8_t channel, uint16_t value)
{ 
  uint32_t data ;

  if (channel == DAC_CHANNEL_A)
    data = DAC_SINGLE_CHANNEL_UPDATE;

  else if (channel == DAC_CHANNEL_B)
    data = DAC_SINGLE_CHANNEL_UPDATE | DAC_MASK_DACSEL0 ;

  else if (channel == DAC_CHANNEL_C)
    data = DAC_SINGLE_CHANNEL_UPDATE| DAC_MASK_DACSEL1 ;

  else if (channel == DAC_CHANNEL_D)
    data = DAC_SINGLE_CHANNEL_UPDATE | DAC_MASK_DACSEL1 | DAC_MASK_DACSEL0 ;

  else if (channel == DAC_CHANNEL_ALL)
    data = DAC_BROADCAST_UPDATE | DAC_MASK_DACSEL1 ;

  else
    // avoid writing bad data
    return;

  // value is 12 MSB bits (last LSB nibble to 0)
  // data |= value << 4;
  data |= value;

  // Send to chip
  write (data);
}

/* ======================================================================
Function: setChannelPower
Purpose : set power value of a channel 
Input   : DAC_CHANNEL_A to DAC_CHANNEL_D or DAC_CHANNEL_ALL
          DAC_POWER_DOWN_1K, DAC_POWER_DOWN_HIZ or DAC_POWER_DOWN_100K
Output  : 
Comments: 
====================================================================== */
void DAC::setChannelPower(uint8_t channel, uint16_t power)
{ 
  // Default we'll set power
  uint32_t data = power | DAC_MASK_PD0 ;

  if (channel == DAC_CHANNEL_A)
    data |= DAC_SINGLE_CHANNEL_UPDATE;

  else if (channel == DAC_CHANNEL_B)
    data |= DAC_SINGLE_CHANNEL_UPDATE | DAC_MASK_DACSEL0 ;

  else if (channel == DAC_CHANNEL_C)
    data |= DAC_SINGLE_CHANNEL_UPDATE| DAC_MASK_DACSEL1 ;

  else if (channel == DAC_CHANNEL_D)
    data |= DAC_SINGLE_CHANNEL_UPDATE | DAC_MASK_DACSEL1 | DAC_MASK_DACSEL0 ;

  else if (channel == DAC_CHANNEL_ALL)
    data |= DAC_BROADCAST_UPDATE | DAC_MASK_DACSEL1 ;

  // Send to chip
  write (data);
}

