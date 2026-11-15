/*
 * TELEXo Eurorack Module
 * (c) 2016, 2017 Brendon Cassidy
 * MIT License
 */
 
#include "TriggerOutput.h"
#include "TxHelper.h"

/*
 * Initialize a Trigger Output and its LED
 */
TriggerOutput::TriggerOutput(TelexIO& telex, int output, int led) : Output(telex, output, led) {
  /*
  // initialize the pins
  pinMode(_output, OUTPUT);
  pinMode(_led, OUTPUT);
  */
}

/*
 * Set the State of the Trigger Output and Write it To the Output Pins
 *
 *  INLINED
void TriggerOutput::SetState(bool state){
  _state = state;
  //digitalWrite(_output, _state ? HIGH : LOW);
  //digitalWrite(_led, _state ? HIGH : LOW); 
  _telex.SetPulse(_output, _state);
  _telex.SetLed(_led, _state);
}
*/

/*
 * Sets the duration of the trigger pulse in multiple time formats
 */
void TriggerOutput::SetTime(int value, short format){
  _widthMode = false;
  _pulseTime = TxHelper::ConvertMs(value, format);
}

/*
 * Sets the duration of the trigger pulse as a percentage of the metro interval
 */
void TriggerOutput::SetWidth(int value){
  _widthMode = true;
  _width = constrain(value, 0, 100);
  //_pulseTime = _metroInterval * _width / 100.; // FIXED POINT VERSION BELOW:
  _pulseTime = static_cast<int>((static_cast<uint64_t>(_metroInterval) * static_cast<uint64_t>(_width)*82u)>>13);
}

/*
 * Toggle the State of the Trigger Output
 */
void TriggerOutput::ToggleState(){
  _toggle = MAXTIME;
  SetState(!_state);
}

/*
 * Pulse the Trigger for the Time Interval
 */
void TriggerOutput::Pulse() {

  // jump out if pulses are muted
  if (_mutePulse) return;

  // implement the clock divider (if active)
  if (_divide) {
    if (++_counter >= _division)
      _counter = 0;
    else
      return; 
  }
  
  if (_state != _polarity)
    SetState(_polarity);
  _toggle = millis() + _pulseTime;
}

/*
 * Sets the Polarity of the Output (connected to pulse)
 */
void TriggerOutput::SetPolarity(bool polarity){
  _polarity = polarity;
}

/*
 * Sets the division value for the PULSE clock divider
 */
void TriggerOutput::SetDivision(int division){
  _division = division;
  _counter = _division;
  _divide = division > 1 ? true : false;
}

/*
 * Set Clock Multiplier
 */
void TriggerOutput::SetMultiplier(int multiplier){
   _tempMultiplication = max(multiplier, 1);
   _tempMultiplyInterval = _metroInterval / _tempMultiplication;
   _multiply = _tempMultiplication > 1;
   // if turning off multiply during metro - schedule the next beat
   if (!_multiply && _metro) _nextEvent = _nextNormal;
}

/*
 * Activates or Deactivates the Metro for this Trigger at this Moment
 */
void TriggerOutput::SetMetro(int state) {
  SetMetro(state, millis());
}


/**
 * Activates or Deactivates the Metro for this Trigger
 */
void TriggerOutput::SetMetro(int state, unsigned long ms){
  bool m = state != 0;
  if (m){  
    _actualCount = _metroCount;
    if (!_metro) {
      Sync(ms);
      _multiplyInterval = _tempMultiplyInterval;
      _multiplication = _tempMultiplication;
      _multiplyCount = 0;
    }
  }
  _metro = m;
}

/**
 * Sets the time for the metro pulse (in ms, sec, min, and bpm)
 */
void TriggerOutput::SetMetroTime(int value, short format){
  _metroInterval = TxHelper::ConvertMs(value, format);
  _tempMultiplyInterval = _metroInterval / _multiplication;
  if (_widthMode) SetWidth(_width);
}

/**
 * Sets the number of trigger repeats on an M event (0 = inf)
 */
void TriggerOutput::SetMetroCount(int value){
  _metroCount = value;
  _actualCount = value;
}

/*
 * Mute (true) or unmute (false) the PULSE command
 */
void TriggerOutput::SetMute(bool state){
  _mutePulse = state;
}

/**
 * Syncs the metro pulse to now
 */
void TriggerOutput::Sync(){
  Sync(millis());
}

/**
 * Syncs the metro pulse to a specified time
 */
void TriggerOutput::Sync(unsigned long syncTime){
  _nextEvent = syncTime;
  _nextNormal = syncTime;
  _counter = 0;
}


/**
 * Resets the trigger output
 */
void TriggerOutput::Reset(){
  SetPolarity(true);
  SetState(false);
  SetTime(100, 0);
  SetDivision(1);
  SetMultiplier(1);
  SetMetro(0);
  SetMetroTime(1000,0);
  SetMetroCount(0);
  SetMute(false);
}


/*
 * Stop All Pulses
 */
void TriggerOutput::Kill(){
  _toggle = MAXTIME;
  _metro = false;
}

/*
 * Update Function (Call This a Lot)
 *
 * INLINED
void FASTRUN TriggerOutput::Update(unsigned long currentTime){

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
*/





