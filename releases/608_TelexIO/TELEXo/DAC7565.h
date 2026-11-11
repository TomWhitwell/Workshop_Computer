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
#ifndef DAC_H
#define DAC_H

#include <arduino.h>

// 24 bits code definition
#define DAC_REFERENCE_ALWAYS_POWERED_DOWN  0x2000
#define DAC_REFERENCE_POWERED_TO_DEFAULT   0x0000
#define DAC_REFERENCE_ALWAYS_POWERED_UP    0x1000

#define DAC_DATA_INPUT_REGISTER    0x011000

#define DAC_MASK_LD1        0x200000
#define DAC_MASK_LD0        0x100000
#define DAC_MASK_DACSEL1    0x040000
#define DAC_MASK_DACSEL0    0x020000
#define DAC_MASK_PD0        0x010000
#define DAC_MASK_PD1        0x008000
#define DAC_MASK_PD2        0x004000
#define DAC_MASK_DATA       0x00FFF0

#define DAC_SINGLE_CHANNEL_STORE    0 /* LD1=0,LD0=0 */
#define DAC_SINGLE_CHANNEL_UPDATE   DAC_MASK_LD0 /* LD1=0,LD0=1 */
#define DAC_SIMULTANEOUS_UPDATE     DAC_MASK_LD1 /* LD1=1,LD0=0 */
#define DAC_BROADCAST_UPDATE        DAC_MASK_LD1 | DAC_MASK_LD0 /* LD1=1,LD0=1 */

#define DAC_POWER_DOWN_1K   DAC_MASK_PD2
#define DAC_POWER_DOWN_100K DAC_MASK_PD1
#define DAC_POWER_DOWN_HIZ  DAC_MASK_PD2 | DAC_MASK_PD1

// 8 bit constant to pass only 8 bits paramaeters
#define DAC_CHANNEL_A   1
#define DAC_CHANNEL_B   2
#define DAC_CHANNEL_C   3
#define DAC_CHANNEL_D   4
#define DAC_CHANNEL_ALL 5

// #define DAC_MAX_SCALE 4096 // Max Scale points (DAC 14 bits)
#define DAC_MAX_SCALE 65536 // Max Scale points (DAC 16 bits)


class DAC { 
  public:
         DAC(uint8_t enable_pin=-1, uint8_t sync_pin=-1, uint8_t ldac_pin=-1, uint8_t data_pin=-1, uint8_t clock_pin=-1);
    void init(void);
    void setReference(uint16_t reference);
    void writeChannel(uint8_t channel, uint16_t value);
    void setChannelPower(uint8_t channel, uint16_t power);

  private:
    void    write(uint32_t data);
    uint8_t _enable_pin;
    uint8_t _sync_pin ;
    uint8_t _ldac_pin ;
    uint8_t _data_pin ;
    uint8_t _clock_pin ;
    boolean _hw_spi;

};

#endif
