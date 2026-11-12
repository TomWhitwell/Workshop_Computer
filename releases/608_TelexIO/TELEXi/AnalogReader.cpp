/*
 * TELEXi Eurorack Module
 * (c) 2016 Brendon Cassidy
 * MIT License
 */
 
//#include "Arduino.h"
#define FASTRUN

#include "AnalogReader.h"
#include "telexio.h"

//#include <ResponsiveAnalogRead.h>

#define MAXPOT 16379

/*
 * simple constructor for unipolar things (like pots)
 */
AnalogReader::AnalogReader(TelexIO& telex, int address) : AnalogReader(telex, address, false)
{
}

/*
 * advanced constructor for inverted, bipolar things (like CV)
 */
AnalogReader::AnalogReader(TelexIO& telex, int address, bool reverse) :
  _telex(telex),
  _address(address),
  _reverse(reverse)
{
  if (!_reverse) _bottom = 0;

  /* NOT NEEDED ON COMPUTERCARD
  // set the appropriate smoothing for CV (_reverse) and potentiometers
  if (_reverse)
    _analog = new ResponsiveAnalogRead(0, false);
  else
    _analog = new ResponsiveAnalogRead(0, true, .0001);
    
  _analog->setAnalogResolution(1<<13);
   */

  _calibrationData[0] = -16384;
  _calibrationData[1] = 0;
  _calibrationData[2] = 16383;
}

/*
 *  reads the analog input
 */
int FASTRUN AnalogReader::Read() {
/* NOT NEEDED ON COMPUTERCARD
  // read the value from the pin
  _readValue = analogRead(_address);
  _analog->update(_readValue);
  _readValue = _analog->getValue();
  */

  _readValue = _telex.telexIinput(_address);
 /*
  // if it is a potentiometer (not reversed) constrain and scale to the MAXPOT range
  if (!_reverse){
    _readValue = constrainInt(_readValue, 0, MAXPOT);
    _readValue = mapInt(_readValue, 0, MAXPOT, 0, 16383);
  }

  // shift it, flip it and reverse it (_reverse is for CV)
   _readValue = _readValue << (_reverse ? 2 : 1);  // not needed on COMPUTERCARD
  if (_reverse) _readValue = 16383 - _readValue;   // not needed on COMPUTERCARD
  
  // scale if this input is actively calibrated
  if (_calibrated) _readValue = Scale(_readValue);

  // map it (if we are mapping values)
  if (_map){
    _readValue = mapInt(_readValue, _reverse ? BOTTOM : 0, TOP, _bottom, _top); 
  }
*/
  // store as latest value and return
  _latestValue = _readValue;
  return _latestValue;
  
}

/*
 * retuns the latest read value
 */
int AnalogReader::GetLatest() {
  return _latestValue;
}


void AnalogReader::SetTop(int top){
  SetMap(top, _bottom);
}

void AnalogReader::SetBottom(int bottom){
  SetMap(_top, bottom);
}

void AnalogReader::SetMap(int top, int bottom){
  if (_top != top) _top = top;
  if (_bottom != bottom) _bottom = bottom;
  _map = _top != TOP || _bottom != (_reverse ? BOTTOM : 0);
}

/*
 * returns if calibration is active or not
 */
bool AnalogReader::GetCalibrated(){
  return _calibrated;
}

/*
 * manually set the calibration state
 */
void AnalogReader::SetCalibrated(bool calibrated){
  _calibrated = calibrated;
}

/*
 * perform a calibration for a given measure
 * measure can be less than zero, zero, or greater than zero
 */
void AnalogReader::Calibrate(int measure){} // TODO, use COMPUTERCARD EEPROM

/*
void AnalogReader::Calibrate(int measure){

    // set this reader as calibrated
    _calibrated = true;

    // pull the accumulated value
    int value = _analog->getValue();
    
    // flip and reverse
    value = value << (_reverse ? 2 : 1);
    if (_reverse) value = 16383 - value;

    // set the calibration data
    // removed value protections so that fun things can be done with scaling
    if (measure == 0) {
      _calibrationData[1] = value;
    } else if (measure > 0) {
      _calibrationData[2] = value;
    } else if (measure < 0){
      _calibrationData[0] = value;
    }

}
*/

/*
 * returns the calibration data (for eeprom parameter storage)
 */
void AnalogReader::GetCalibrationData(int data[3]){
  for (_i=0; _i < 3; _i++) data[_i] = _calibrationData[_i];
}

/*
 * manually sets the calibration data (for eeprom parameter retrieval)
 */
void AnalogReader::SetCalibrationData(int measure, int value){
  _calibrationData[measure] = value;
}


/*
 * performs the scaling function based on calibration
 */
int FASTRUN AnalogReader::Scale(int value) {

  // map the polar values
  if (value >= _calibrationData[1]){
    value = mapInt(value, _calibrationData[1], _calibrationData[2], 0, 16383);
  } else {
    value = mapInt(value, _calibrationData[0], _calibrationData[1], -16384, 0);
  }

  // constrain to our TI range
  value = constrainInt(value, -16384, 16383);
  
  return value;
}

