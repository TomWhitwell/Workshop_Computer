/*
   Class to attach functions to short and long pushes on a button
   Tom Whitwell, Music Thing Modular, November 2018, November 2024  
   Herne Hill, London

*/



#ifndef Click_h
#define Click_h


class Click

{
  public:
    Click(void (*functionShort)(), void (*functionLong)() );
    void Update(boolean reading);

  private:
    int _pin;
    boolean _reading; 
    unsigned long _downTime = 0;
    boolean _buttonDown = false;
    boolean _longPushed = false;
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

void Click::Update(boolean reading) {
  // first time we notice button is pushed
  _reading = reading;
  if (_reading == HIGH &&
      _buttonDown == false) {
    _downTime = millis();
    _buttonDown = true;
    _longPushed = false;
  }

  // while holding, do nothing unless enough time has passed to register a long push
  if (_reading == HIGH &&
      _buttonDown == true &&
      _longPushed == false &&
      (millis() - _downTime) > _tooLong) {
    _longPushed = true;
    _functionLong();
  }

  // button is released checks if bounce, or short push
  if (_reading == LOW &&
      _buttonDown == true) {
    _buttonDown = false;
    _pushLength = millis() - _downTime;
    if (_pushLength > _tooShort &&
        _pushLength < _tooLong &&
        _longPushed == false) {
      _functionShort();
    }
  }

}


#endif
