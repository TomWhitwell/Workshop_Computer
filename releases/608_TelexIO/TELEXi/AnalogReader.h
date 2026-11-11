/*
 * TELEXi Eurorack Module
 * (c) 2016 Brendon Cassidy
 * MIT License
 */
 
#pragma once

#ifndef AnalogReader_h
#define AnalogReader_h

#define TOP 16383
#define BOTTOM -16384


// helper functions replaced from the arduino library
static inline int constrainInt(int value, int minValue, int maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static inline int mapInt(int value, int inMin, int inMax, int outMin, int outMax)
{
    return (value - inMin) * (outMax - outMin)
         / (inMax - inMin) + outMin;
}

class TelexIO;

/*
 * helper class created for the TELEXi to read and scale inputs
 */
class AnalogReader
{
  public:

    AnalogReader(TelexIO& telex, int address);
    AnalogReader(TelexIO& telex, int address, bool reverse);
    
    int Read();
    int GetLatest();

    void SetTop(int top);
    void SetBottom(int bottom);
    void SetMap (int top, int bottom);
    
    void Calibrate(int measure);
    bool GetCalibrated();
    void SetCalibrated(bool calibrated);
    void GetCalibrationData(int data[3]);
    void SetCalibrationData(int measure, int value);

  private:
  
    TelexIO& _telex;
    int _address;
    const bool _reverse;
    
    //ResponsiveAnalogRead *_analog; // NOT NEEDED ON COMPUTER
    
    int volatile _readValue;
    int volatile _latestValue;
    
    int Scale(int value);
    int _calibrationData[3];
    bool _calibrated = false;

    bool _map = false;
    int _top = TOP;
    int _bottom = BOTTOM;
    
    int _i;
    
};


#endif

