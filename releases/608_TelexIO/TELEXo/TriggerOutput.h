/*
 * TELEXo Eurorack Module
 * (c) 2016 Brendon Cassidy
 * MIT License
 */
#pragma once
 
#ifndef TriggerOutput_h
#define TriggerOutput_h

#include "Output.h"
#include "telexio.h"

#include "pico/time.h"

#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt))) // replace constrain
#define max(a,b) ((a) > (b) ? (a) : (b)) // include the max keyword

#define millis() to_ms_since_boot(get_absolute_time())

class TriggerOutput : public Output
{
  public:
  
    // TriggerOutput(int output);
    TriggerOutput(TelexIO& telex, int output, int led);
    
    void Update(unsigned long currentTime);
    
    void SetState(bool state);
    void ToggleState();
    void Pulse();
    void SetPolarity(bool polarity);
    void SetTime(int value, short format);
    void SetWidth(int value);
    
    void SetDivision(int division);
    void SetMultiplier(int multiplier);

    void SetMetro(int state);
    void SetMetro(int state, unsigned long syncTime);
    void SetMetroTime(int value, short format);
    void SetMetroCount(int value);

    void SetMute(bool state);
    
    void Sync(unsigned long syncTime);
    void Sync();
    void Reset();

    // virtual implementations
    void Kill();
    
  protected:
    
  private:
    
    bool _state = false;
    bool _polarity = true;
    unsigned long _toggle = MAXTIME;

    bool _divide = false;
    unsigned short _division = 0;
    unsigned short _counter = 0;

    bool _multiply = false;
    unsigned short _multiplication = 1;
    unsigned short _tempMultiplication = 1;
    unsigned long _multiplyInterval = 1000;
    unsigned long _tempMultiplyInterval = 1000;
    unsigned long _nextNormal = 0;
    int _multiplyCount = 0;

    bool _metro = false;
    unsigned long _metroInterval = 1000;
    unsigned long _nextEvent = 0;

    int _metroCount = 0;
    int _actualCount = -1;

    bool _widthMode = false;
    int _width = 0;
    
    // 100ms is the teletype's default value for the pulse time
    unsigned long _pulseTime = 100;    

    bool _mutePulse = false;
};

__attribute__((always_inline))
inline void __not_in_flash_func(TriggerOutput::SetState)(bool state){
  _state = state;
  //digitalWrite(_output, _state ? HIGH : LOW);
  //digitalWrite(_led, _state ? HIGH : LOW); 
  _telex.SetPulse(_output, _state);
  _telex.SetLed(_led, _state);
}

__attribute__((always_inline))
inline void __not_in_flash_func(TriggerOutput::Update)(unsigned long currentTime){

   //
   // _multiplication = number of dongises
   // _multiply = bool ON or OFF
   // _multiplyInterval
   // unsigned long _nextNormal = 0;
   // int _multiplyCount = 0;
   //

  // turn off the pulse
  if (currentTime >= _toggle) {
    if (_state == _polarity)
      SetState(!_polarity);
    _toggle = MAXTIME;
  }

  // evaluate pinging the metro event
  if (_metro && currentTime >= _nextEvent){

    if (_multiply){

      if (_multiplyCount == 0){
        if (_metroCount == 0 || (_metroCount > 0 && --_actualCount > 0)){
          // set the next reference beat (avoids divisionn drift)
          _nextNormal = _nextNormal + _metroInterval;
          // copy over any new values
          _multiplyInterval = _tempMultiplyInterval;
          _multiplication = _tempMultiplication;
          // reset multiplication counter
          _multiplyCount = 0;
        } else {
          // we have beat for the expected count - disable the metro
          _metro = false;
        }
      }
      
      if (++_multiplyCount < _multiplication) {
        // set the next event to the multiply interval
        _nextEvent = _nextEvent + _multiplyInterval;
      } else {
        // set the next event to the metro interval (normal) and reset count
        _nextEvent = _nextNormal;
        _multiplyCount = 0;
      }
      
    } else {

      // we are just doing basic metronomes
      if (_metroCount == 0 || (_metroCount > 0 && --_actualCount > 0)){
        _nextEvent = _nextNormal + _metroInterval;
        _nextNormal = _nextEvent;
      } else
        _metro = false;
      
    }
      
    Pulse();
  }
  
}

#endif

