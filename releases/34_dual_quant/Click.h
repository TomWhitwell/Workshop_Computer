//
//  Click.swift
//  
//
//  Created by Adrian Vos on 16/05/2026.
//


/*
   Class to attach functions to short and long pushes on a button
   Tom Whitwell, Music Thing Modular, November 2018 
   Herne Hill, London

   Modified by Dune Desormeaux for Pico SDK compatibility, 2025
*/

#ifndef Click_h
#define Click_h

#include "pico/stdlib.h"

#define HIGH 1
#define LOW 0

int32_t pico_millis() {
  return (int32_t)to_ms_since_boot(get_absolute_time());
}

class Click

{
  public:
    Click(void (*functionShort)(), void (*functionLong)() );
    void Update(bool reading);

  private:
    int _pin;
    bool _reading; 
    unsigned long _downTime = 0;
    bool _buttonDown = false;
    bool _longPushed = false;
    unsigned long _tooShort = 15; // debounce time; clicks shorter than this are ignored
    unsigned long _tooLong = 500; // minimum time to hold the button and register a hold 
    unsigned long _pushLength; 
    void (*_functionShort)();
    void (*_functionLong)();  
};



Click::Click(void (*functionShort)(), void (*functionLong)() ) {
  _functionShort = functionShort;
  _functionLong = functionLong;
}

void Click::Update(bool reading) {
  // first time we notice button is pushed
  _reading = reading;
  if (_reading == HIGH &&
      _buttonDown == false) {
    _downTime = pico_millis();
    _buttonDown = true;
    _longPushed = false;
  }

  // while holding, do nothing unless enough time has passed to register a long push
  if (_reading == HIGH &&
      _buttonDown == true &&
      _longPushed == false &&
      (pico_millis() - _downTime) > _tooLong) {
    _longPushed = true;
    _functionLong();
  }

  // button is released checks if bounce, or short push
  if (_reading == LOW &&
      _buttonDown == true) {
    _buttonDown = false;
    _pushLength = pico_millis() - _downTime;
    if (_pushLength > _tooShort &&
        _pushLength < _tooLong &&
        _longPushed == false) {
      _functionShort();
    }
  }

}


#endif