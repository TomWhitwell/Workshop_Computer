/*
 * TELEXo Eurorack Module
 * (c) 2016-2018 Brendon Cassidy
 * MIT License
 */
 
#include "CVOutput.h"
#include "TriggerOutput.h" 
#include "TxHelper.h"
#include "samplerate.h"
#include "Oracle.h"

/*
 * Constructor for Setting up the Output
 */
//CVOutput::CVOutput(int output, int led, DAC& dac) : Output(output, led){
CVOutput::CVOutput(TelexIO& telex, int output, int led) : Output(telex, output, led){
// store the DAC reference
  //_dac = dac;
  // initialize the Quantizers
  _quantizer = new Quantizer(0);
  _oscQuantizer = new Quantizer(0);
  // initialize the oscillator
  _oscillator = new Oscillator();
  // re-initialize using the reset command (to keep things the same at start-up as on init)
  Reset();
}

/*
 * Creates a link between this CV output and a particular trigger output (for EOR/EOF envelope triggers)
 */
void CVOutput::ReferenceTriggers(TriggerOutput (*triggerOutputs[]), int count){
  _triggerOutputs = triggerOutputs;
  _triggerOutputCount = count;
}


/*
 * Sets the Value without Slew
 */
void __not_in_flash_func(CVOutput::SetValue)(int value){
  // target is the delivered value plus the configured offset
  _tempTarget = Constrain(value + (_offset + _calibration)) << 15;
  if (_envelopeMode){
    if (_envTarget != _tempTarget) {
      _envTarget = _tempTarget;
      RecomputeEnvelopes();
    }
  } else {
    _target = _tempTarget;
    // this boolean tells the update method to skip slewing
    _set = true;
  }
}

/*
 * Sets the Target to Slew to
 */
void __not_in_flash_func(CVOutput::TargetValue)(int value){
  // target is the delivered value plus the configured offset
  _tempTarget = Constrain(value + (_offset + _calibration)) << 15;
  if (_envelopeMode){
    if (_envTarget != _tempTarget) {
      _envTarget = _tempTarget;
      RecomputeEnvelopes();
    }
  } else {
    _target = _tempTarget;
    // false indicates that we want it to slew
    _set = false;
    // calculate the new slew increment based on the new value
    CalculateSlewValue();
  }

}

/*
 * Sets the Slew Time (using the protected _time variable)
 */
void __not_in_flash_func(CVOutput::SetSlew)(int value, short format){
  // create the slew value from the passed integer in several formats (0=ms; 1=sec; 2=min)
  _slewTime = TxHelper::ConvertMs(value, format);
  CalculateSlewValue();  
  // ensure that we are slewing to our destination
  _set = false;
}

/*
 * Store a New Offset Value
 */
void __not_in_flash_func(CVOutput::SetOffset)(int value){

  // neutralize old offset and add new offset to target
  _tempTarget = Constrain(((_envelopeMode ? _envTarget : _target) >> 15) + (value - _offset)) << 15;
  // store the new offset
  _offset = value;
  _lOffset = (_offset + _calibration) << 15;
  
  if (_envelopeMode){
    if (_envTarget != _tempTarget) {
      _envTarget = _tempTarget;
      _target = _lOffset;
      RecomputeEnvelopes();
    }
  } else {
    _target = _tempTarget;
  }

  // only do this if the envelope isn't in the attack phase (_envelopeActive) or decaying (_decaying)  
  if (!_envelopeActive && !_decaying){
    // if slewing - calculate a new slew value; else set the offset directly
    if (_slewTime != 0) 
      CalculateSlewValue();
    else
      _set = true;
  }
}

/*
 * Calibrates the output by making the current offset permanent
 * Returns the calibration value for storage
 */
int __not_in_flash_func(CVOutput::Calibrate)(){
  _calibration = _offset + _calibration;
  _offset = 0;
  // Serial.printf("calibration: %d; Offset: %d\n", _calibration, _offset);

  return _calibration;
}

/*
 * Resets the calibration value to 0 and resets the output
 */
void __not_in_flash_func(CVOutput::ResetCalibration)(){
  
  // neutralize old calibration (ug) and add new offset to target
  _tempTarget = Constrain(((_envelopeActive ? _envTarget : _target) >> 15) - _calibration) << 15;
  _lOffset = _offset << 15;
  
  // Serial.printf("target: %d; temptarget: %d\n", _target, _tempTarget);

  // nuke the calibration
  _calibration = 0;
  // Serial.printf("calibration: %d; Offset: %d\n", _calibration, _offset);

  if (_envelopeMode){
    if (_envTarget != _tempTarget) {
      _envTarget = _tempTarget;
      RecomputeEnvelopes();
    }
  } else {
    _target = _tempTarget;
     // if slewing - calculate a new slew value; else set the offset directly
    if (_slewTime != 0) 
      CalculateSlewValue();
    else
      _set = true;
  }
  
}

/*
 * Sets the calibration to a particular value (used by the start-up procedure)
 */
void __not_in_flash_func(CVOutput::SetCalibrationValue)(int value){
  SetOffset(value);
  Calibrate();
}

/*
 * Sets the logarithmic translation mode
 */
void __not_in_flash_func(CVOutput::SetLog)(int value){
  _logRange = value - 1;
  _doLog = value > 0;
}

/*
 * Stop Slew Activity and Jump to Target
 */
void __not_in_flash_func(CVOutput::Kill)(){
  // stop slewing
  _set = true;
}

/*
 * Reset the CV Output
 */
void __not_in_flash_func(CVOutput::Reset)(){
  SetOffset(0);
  SetValue(0);
  SetSlew(1,0);
  SetQuantizationScale(0);
  SetLog(0);
  
  SetFrequency(0);
  SetOscQuantizationScale(0);
  SetWaveform(0);
  SetPhaseOffset(0);
  SetRectify(0);
  SetWidth(50);
  SetFrequencySlew(0, 0);
  SetCenter(0);
  
  SetEnvelopeMode(0);
  SetAttack(12, 0);
  SetDecay(250, 0);

  SetEOR(-1);
  SetEOC(-1);
  SetLoop(1);
  
}

/*
 * Sets Output Quantization Mode for the CV Outputs
 */
void __not_in_flash_func(CVOutput::SetQuantizationScale)(int scale){
    _quantizer->SetScale(scale);
}

/*
 * Quantizes and sets a value to the current scale
 */
void __not_in_flash_func(CVOutput::SetQuantizedValue)(int note){
  int16_t neg = note < 0 ? -1 : 1;
  SetValue((neg * _quantizer->Quantize(abs(note)).Value) << 1);
}

/*
 * Quantizes and targets a value (with slew) to the current scale
 */
void __not_in_flash_func(CVOutput::TargetQuantizedValue)(int note){
  int16_t neg = note < 0 ? -1 : 1;
  TargetValue((neg * _quantizer->Quantize(abs(note)).Value) << 1);
}

/*
 * Sets a CV Value by Note Number
 * (against the active Quantization Scale)
 */
void __not_in_flash_func(CVOutput::SetNote)(int note){
  int16_t neg = note < 0 ? -1 : 1;
    SetValue((neg * (int)_quantizer->GetValueForNote(abs(note))) << 1);
}

/*
 * Sets a CV Value by Note Number
 * (against the active Quantization Scale)
 */
void __not_in_flash_func(CVOutput::TargetNote)(int note){
  int16_t neg = note < 0 ? -1 : 1;
  TargetValue((neg * (int)_quantizer->GetValueForNote(abs(note))) <<  1);
}

/*
 * Sets the Format for the Slew Time Value
 */
void __not_in_flash_func(CVOutput::SetTimeFormat)(int format){
  // call the base class
  // Output::SetTimeFormat(format);
}

/*
 * Shared Function for Oscillator Setup
 */
void __not_in_flash_func(CVOutput::SharedOscil)(int value){

  // reset the phase if it isn't currently oscillation mode
  if (!_oscilMode)
    _oscillator->ResetPhase(_target);

  // determine if we are entering oscil mode
  _oscilMode = value > 0;

  // setup oscillation conditions or turn them off
  if (_oscilMode) {
     //_dacCenter = DACCENTER - _oscilCenter;
  } else {
    _set = true;
    //_dacCenter = DACCENTER;
  }
    
}

/*
 * Set the centerpoint for oscillation
 */
void __not_in_flash_func(CVOutput::SetCenter)(int value){
  _oscilCenter = value;
  //if (_oscilMode) 
     //_dacCenter = DACCENTER - _oscilCenter;
}

/*
 * Sets the oscillation frequency in Hz
 */
void __not_in_flash_func(CVOutput::SetFrequency)(int freq){

  // call shared oscil setup function
  SharedOscil(freq);
  
  if (_oscilMode)
    _oscillator->SetFrequency(freq);

}

/*
 * Targets the oscillation frequency in Hz (for slew)
 */
void __not_in_flash_func(CVOutput::TargetFrequency)(int freq){

  // call shared oscil setup function
  SharedOscil(freq);
  
  if (_oscilMode)
    _oscillator->TargetFrequency(freq);

}

/*
 * Sets the oscillation frequency using the TT integer value
 * using the current quantizer scale
 */
void __not_in_flash_func(CVOutput::SetQuantizedVOct)(int value){

  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->SetFloatFrequency(_quantizer->Quantize(value).Frequency);

}

void __not_in_flash_func(CVOutput::SetQuantizedVOct_withoracle)(int value){

  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->SetFloatFrequency(oracle.floatAnswer);
    // note: oracle.floatAnswer = cvOutputs[oracle.output]->QuantizedVOct_oraclewrapper(oracle.value);

}


/*
 * Targets the oscillation frequencty using the TT integer value
 * using the current quantizer scale
 */
void __not_in_flash_func(CVOutput::TargetQuantizedVOct)(int value){

  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->TargetFloatFrequency(_quantizer->Quantize(value).Frequency);

}

/*
 * Targets the oscillation frequencty using the TT integer value
 * using the current quantizer scale
 */
void __not_in_flash_func(CVOutput::TargetQuantizedVOct_withoracle)(int value){

  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->TargetFloatFrequency(oracle.floatAnswer);
  // note: oracle.floatAnswer = cvOutputs[oracle.output]->QuantizedVOct_oraclewrapper(oracle.value);

}

float __not_in_flash_func(CVOutput::QuantizedVOct_oraclewrapper)(int value){
    return _quantizer->Quantize(value).Frequency;
}

/*
 * Sets the slew amount for the frequency (portamento) in the supplied format
 */
void __not_in_flash_func(CVOutput::SetFrequencySlew)(int slew, short format){
  _oscillator->SetPortamentoMs(TxHelper::ConvertMs(slew, format));
}

/*
 * Sets the oscillation frequency using the TT integer value
 */
void __not_in_flash_func(CVOutput::SetVOct)(int value){

  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->SetFloatFrequency(TxHelper::VOct2Frequency(value));

}

/*
 * Sets the oscillation frequency using the TT integer value
 */
void __not_in_flash_func(CVOutput::SetVOct_withoracle)(int value){

  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->SetFloatFrequency(oracle.floatAnswer);

}


/*
 * Targets the oscillation frequency using the TT integer value
 */
void __not_in_flash_func(CVOutput::TargetVOct)(int value){

  // call shared oscil setup function
  SharedOscil(value);

  if (_oscilMode)
    _oscillator->TargetFloatFrequency(TxHelper::VOct2Frequency(value));
}

/*
 * Targets the oscillation frequency using the TT integer value
 */
void __not_in_flash_func(CVOutput::TargetVOct_withoracle)(int value){

  // call shared oscil setup function
  SharedOscil(value);

  if (_oscilMode)
    _oscillator->TargetFloatFrequency(oracle.floatAnswer);
}

/*
 * Sets the oscillation frequency via note number 
 * (against the current quantized scale)
 */
void __not_in_flash_func(CVOutput::SetOscNote)(int note){

  // call shared oscil setup function
  SharedOscil(1);
  
  _oscillator->SetFloatFrequency((_oscQuantizer->GetFrequencyForNote(note)));
}

/*
 * Targets the oscillation frequency via note number 
 * (against the current quantized scale)
 */
void __not_in_flash_func(CVOutput::TargetOscNote)(int note){

  // call shared oscil setup function
  SharedOscil(1);
  
  _oscillator->TargetFloatFrequency((_oscQuantizer->GetFrequencyForNote(note)));
}

/*
 * Sets the duration of a single cycle
 */
void __not_in_flash_func(CVOutput::SetCycle)(int value, short format){

  value = TxHelper::ConvertMs(value, format);
  
  // call shared oscil setup function
  SharedOscil(value);

  if (_oscilMode)
    _oscillator->SetFloatFrequency(1000.0f / value);
}

/*
 * Targets the duration of a single cycle
 */
void __not_in_flash_func(CVOutput::TargetCycle)(int value, short format){

   value = TxHelper::ConvertMs(value, format);
  
  // call shared oscil setup function
  SharedOscil(value);
  
  if (_oscilMode)
    _oscillator->TargetFloatFrequency(1000.0f / value);
}

/*
 * Sets the pulse width of the square wave waveform
 */
void __not_in_flash_func(CVOutput::SetWidth)(int width){
  _oscillator->SetWidth(width);
}

/*
 * Sets the rectification mode (-2 to + 2)
 * 0 = No Rectification
 * -1/+1 = Partial Rectification (lops off values on the other side of zero)
 * -2/+2 = Full Rectification (does an ABS on the waveform and forces polarity)
 */
void __not_in_flash_func(CVOutput::SetRectify)(int mode){
  _oscillator->SetRectify(mode);
}

/*
 * Set the oscillator frequency in Millihertz (1 Hz = .001 mHz)
 */
void __not_in_flash_func(CVOutput::SetLFO)(int millihertz){
  
  // call shared oscil setup function
  SharedOscil(millihertz);
  
  if (_oscilMode)
    _oscillator->SetLFO(millihertz);

}

/*
 * Target the oscillator frequency in Millihertz (1 Hz = .001 mHz)
 */
void __not_in_flash_func(CVOutput::TargetLFO)(int millihertz){

  // call shared oscil setup function
  SharedOscil(millihertz);
  
  if (_oscilMode)
    _oscillator->TargetLFO(millihertz);

}

/*
 * Resets the phase of the oscillator
 */
void __not_in_flash_func(CVOutput::Sync)(){
  if (_oscilMode)
    _oscillator->ResetPhase(0);
  else
    _oscillator->ResetPhase(_target);
}

/*
 * Set the oscillator phase offset
 */
void __not_in_flash_func(CVOutput::SetPhaseOffset)(int phase){
  _oscillator->SetPhaseOffset(phase);
}

/*
 * Selects the oscillator waveform
 * 0    = Sine
 * 1000 = Triangle
 * 2000 = Saw
 * 3000 = Square
 * 4000 = Noise / Sample-and-Hold
 */
void __not_in_flash_func(CVOutput::SetWaveform)(int wave){
  _oscillator->SetWaveform(wave);
}

/*
 * Sets the quantization scale based on the included scales 
 * (see the Quantizer for the list)
 */
void __not_in_flash_func(CVOutput::SetOscQuantizationScale)(int scale){
  _oscQuantizer->SetScale(scale);
}

/*
 * Sets the attack rate for the envelope generator
 */
void __not_in_flash_func(CVOutput::SetAttack)(int att, short format){
  _attack = TxHelper::ConvertMs(max(att, 1), format);
  _attackSlew = CalculateRawSlew(_attack, _envTarget, _lOffset);
  if (_envelopeActive && !_decaying){
    SlewSteps tempSlew = CalculateRawSlew(_attack, _envTarget, _lOffset);
    tempSlew.Steps = (_envTarget - _current) / tempSlew.Delta;
    _slew = tempSlew;
  }
}

/*
 * Sets the decay rate for the envelope generator
 */
void __not_in_flash_func(CVOutput::SetDecay)(int dec, short format){
  _decay = TxHelper::ConvertMs(max(dec, 1), format);
  _decaySlew = CalculateRawSlew(_decay, _lOffset, _envTarget);
  if (!_envelopeActive && _decaying){
    SlewSteps tempSlew = CalculateRawSlew(_decay, _lOffset, _envTarget);
    tempSlew.Steps = (_lOffset - _current) / tempSlew.Delta;
    _slew = tempSlew;
  }
}

/*
 * Turns envelopes on (1) and off (0) and initializes them
 */
void __not_in_flash_func(CVOutput::SetEnvelopeMode)(int mode){

  // thanks to @scanner_darkly for suggesting this bugfix
  // only want to set the targets if the envelope mode has changed
  bool eMode = mode != 0;
  if (eMode != _envelopeMode){
  
    _envelopeMode = eMode;
    
    if (_envelopeMode){
    
      _envTarget = _target;
      _target = _lOffset;
      RecomputeEnvelopes();
    
    } else {

      _target = _envTarget;
      _envLoop = false;
      _envelopeActive = false;
    }
    
    _set = true;
  }

}

// BELOW CONVERTED TO INT MATH AND INLINED IN HEADER
/*
 * Recomputes the Envlope Values
 */
/*
void CVOutput::RecomputeEnvelopes(){
  
  // calculate the interim slew if the envelope is currently active
  // the envelope will adjust its pace to meet the new peak or resting value
  // appropriate to the current phase
  if(_decaying){
    // envelope is in the decay phase
    unsigned long remaining = ((float)_slew.Steps / _decaySlew.Steps) * _decay;
    SlewSteps tempSlew = CalculateRawSlew(remaining, _lOffset, _current);
    _slew = tempSlew;     
  } else if (_envelopeActive) {
    // envelope is in the attack phase
    unsigned long remaining = ((float)_slew.Steps / _attackSlew.Steps) * _attack;
    SlewSteps tempSlew = CalculateRawSlew(remaining, _envTarget, _current);
    _slew = tempSlew;
  }

  
  _attackSlew = CalculateRawSlew(_attack, _envTarget, _lOffset);
  _decaySlew = CalculateRawSlew(_decay, _lOffset, _envTarget);
}
*/

/*
 * Triggers or Retriggers the current envelope
 */
/* // INLINED
void __not_in_flash_func(CVOutput::TriggerEnvelope)(){

  if (_envelopeMode) {

    if (_decaying) {
      
      // retrigger the envelope by going to zero/offset first
      _slew = CalculateRawSlew(RETRIGGERMS, _lOffset, _current);
      _target = _lOffset;
      _retrigger = true;
      
    } else {
            
      _current = _lOffset;
      _target = _envTarget;
      _slew = _attackSlew;

    }
    if (!_envLoop && _loopTimes != 1){
      _loopCount = 0;
      _envLoop = true;
    }
    _envelopeActive = true;
  }
  
}
*/

/*
 * Sets the envelope state to trigger attack (1) or decay (0)
 * Attack triggers the envelope; decay allows the decay (but does not retrigger)
 */
void __not_in_flash_func(CVOutput::SetENV)(int value){
  bool newState = value > 0;
  if (newState) TriggerEnvelope();
  _envelopeState = newState;
}

/*
 * Set the number of loops for the envelope (0 = inf)
 */
void __not_in_flash_func(CVOutput::SetLoop)(int loopEnv){
  _loopTimes = max(loopEnv, 0);
  _infLoop = _loopTimes == 0;
}

/*
 * Set the trigger for End of Rise (EOR)
 */
void __not_in_flash_func(CVOutput::SetEOR)(int trNumber){
  if (_triggerOutputCount > 0 && trNumber >= 0 && trNumber < _triggerOutputCount){
    _triggerForEOR = trNumber;
    _triggerEOR = true;
  } else {
    _triggerEOR = false;
  }
}

/*
 * Set the trigger for End of Cycle (EOC)
 */
void __not_in_flash_func(CVOutput::SetEOC)(int trNumber){
  if (_triggerOutputCount > 0 && trNumber >= 0 && trNumber < _triggerOutputCount){
    _triggerForEOC = trNumber;
    _triggerEOC = true;
  } else {
    _triggerEOC = false;
  }
}
   
/*
 * The Update Function to Fulfil the Virtual Requirement
 * Keep it Lean - You don't have Much CPU
 *
 * COMPUTERCARD - move to header and inline
 * 
void FASTRUN CVOutput::Update() {

  if (_set || _slew.Steps == 1){
    
    _smallCurrent = _target >> 15;
    
    // set the CV directly (skipping any slew behavior)
    UpdateDAC(_smallCurrent);
    _updateLED = true;

    if (_envelopeActive){

      if (_retrigger) {
         // do the attack
        _current = _lOffset;
        _target = _envTarget;
        _slew = _attackSlew;
        _retrigger = false;
        
      } else if (!_envelopeState) {
        // do the decay
        _envelopeActive = false;
        // force current to _envTarget in case of SR dip
        _current = _envTarget;
        _target = _lOffset;
        _slew = _decaySlew;
        _decaying = true;
        _peakLED = true;

        // pulse the EOR trigger (if set)
        if (_envelopeMode && _triggerEOR)
        {  //LATER
         // _triggerOutputs[_triggerForEOR]->Pulse(); 
        }
        
      } else if (_envelopeState) {
        _updateLED = false;
      }
      
    } else {

      // pulse the EOC trigger (if set)
      if (_envelopeMode && _decaying && _triggerEOC) 
      {   //LATER
         // _triggerOutputs[_triggerForEOR]->Pulse(); 
        }
      
      // set the current to the target and turn off the set boolean
      _current = _target;
      _set = false; 
      _slew.Steps = 0;  
      _decaying = false;

      // retrigger if looping and loop count has replays left
      if (_envLoop){
        if (_infLoop || ++_loopCount < _loopTimes)
          TriggerEnvelope();
        else
          _envLoop = false;
      }
      
    }

    _smallCurrent = _current >> 15;
    
  } else if (_slew.Steps > 1){
    
    _slew.Steps--;
    _current += _slew.Delta;

    _smallCurrent = _current >> 15;
    
     // update the DAC
    UpdateDAC(_smallCurrent);
    _updateLED = true;
    
  } else if (_oscilMode) { 
    
    // just update the dac
    UpdateDAC(_smallCurrent);

  }

}
*/

/*
 * Update the DAC
 * Keep it Very Lean - Hardly Any CPU to Spare!
 *
 * COMPUTERCARD moved to header for inlining
void FASTRUN CVOutput::UpdateDAC(int value){

  // do log translation
  if (_doLog){
    if (value < 0){
      value *= -1;
      _wasNg = true;
    } else {
      _wasNg = false;
    }
    value = ExpTable[constrain(value << _logRange, 0 , 32768)];
    if (_wasNg) value *= -1;
    value = value >> _logRange;
  }

  // invert for DAC circuit
  if (_oscilMode)
    value = (int)(value * (_oscillator->Oscillate() / 32768.));

  // added the conditional write only if the CV value changes
  if (value != _cvHelper){
    _cvHelper = value;
    _dac.writeChannel(_output, (_dacCenter - _cvHelper));
  }  
}_updateLED
*/


/*
 * Update the LED (runs at a slower rate than the DAC)
 */
uint16_t __not_in_flash_func(CVOutput::UpdateLED)() {
  // update the LED if changed OR the frequency rate is < 1 Hz
  //if (_updateLED || (_oscilMode && _oscillator->GetFrequency() <= 1)){
  if (_updateLED || _oscilMode ){
    _updateLED = false;
    // calculate the LED value and update it (and make sure you catch the envelope peak)
    // if the envelope has peaked, make sure to show the peak value to appear more responsive
    // (with its lower refresh rate, the LED usually misses the peak without this)
    if (_peakLED) {
      _ledHelper = abs(_envTarget) >> 22;
      _peakLED = false;
      _updateLED = true;
    } else {
      //_ledHelper = _oscilMode ? abs(_cvHelper) >> 7 : abs(_current) >> 22;
      _ledHelper = _oscilMode ? ((_cvHelper) >> 8)+128 : abs(_current) >> 22; 
    }
    _ledHelper = constrain(_ledHelper, 0, 255);
    // write the mapped LED value to the analog port
    //analogWrite(_led, _ledMap[_ledHelper]);
    //LedBrightness(_led, _ledMap[_ledHelper]);  // COMPUTERCARD implementation
  }
  return _ledMap[_ledHelper];
}

/*
 * Calculate the Slew Value (for increments)
 */
void __not_in_flash_func(CVOutput::CalculateSlewValue)(){
  _slew = CalculateRawSlew(_slewTime, _target, _current);
}

/*
 * Calculate the Slew Value from Raw MS for the value
 */
SlewSteps __not_in_flash_func(CVOutput::CalculateRawSlew)(long value, long target, long current){
  SlewSteps ret;
  ret.Duration = value;
  // split here so we don't divide by zero
  if (value == 0 || target == current){
    // if slew is zero - we just set the slew increment to the value we want to traverse
    ret.Steps = 1;
    ret.Delta = target - current;
  } else {
    // caculate the slew increment value
    ret.Steps = value * KRATE;
    ret.Delta = (target - current) / ret.Steps;
    // increment one to have the last step be the signalling step
    ret.Steps += 1;
  }
  return ret;
}

/*
 * Constrain the values to 16-bit Integer Range
 */
int __not_in_flash_func(CVOutput::Constrain)(int value){
  return constrain(value, -32768, 32767);
}

/*
 * Used to make the LEDs visible over a wider range of CV values
 */
const uint8_t CVOutput::_ledMap[] = {
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x1,0x1,0x1,0x1,0x1,0x1,
  0x1,0x1,0x1,0x2,0x2,0x2,0x2,0x2,0x2,0x2,0x2,0x3,0x3,0x3,0x3,0x3,
  0x4,0x4,0x4,0x4,0x4,0x5,0x5,0x5,0x5,0x5,0x6,0x6,0x6,0x6,0x7,0x7,
  0x7,0x8,0x8,0x8,0x9,0x9,0x9,0xa,0xa,0xa,0xb,0xb,0xb,0xc,0xc,0xd,
  0xd,0xd,0xe,0xe,0xf,0xf,0x10,0x10,0x11,0x11,0x12,0x12,0x13,0x13,0x14,0x14,
  0x15,0x16,0x16,0x17,0x17,0x18,0x19,0x19,0x1a,0x1b,0x1b,0x1c,0x1d,0x1d,0x1e,0x1f,
  0x20,0x20,0x21,0x22,0x23,0x23,0x24,0x25,0x26,0x27,0x28,0x28,0x29,0x2a,0x2b,0x2c,
  0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3b,0x3c,0x3d,
  0x3e,0x3f,0x40,0x42,0x43,0x44,0x45,0x47,0x48,0x49,0x4a,0x4c,0x4d,0x4f,0x50,0x51,
  0x53,0x54,0x56,0x57,0x58,0x5a,0x5b,0x5d,0x5f,0x60,0x62,0x63,0x65,0x67,0x68,0x6a,
  0x6c,0x6d,0x6f,0x71,0x72,0x74,0x76,0x78,0x7a,0x7b,0x7d,0x7f,0x81,0x83,0x85,0x87,
  0x89,0x8b,0x8d,0x8f,0x91,0x93,0x95,0x97,0x99,0x9b,0x9e,0xa0,0xa2,0xa4,0xa6,0xa9,
  0xab,0xad,0xb0,0xb2,0xb4,0xb7,0xb9,0xbc,0xbe,0xc1,0xc3,0xc6,0xc8,0xcb,0xcd,0xd0,
  0xd2,0xd5,0xd8,0xda,0xdd,0xe0,0xe3,0xe5,0xe8,0xeb,0xee,0xf1,0xf4,0xf7,0xfa,0xfd
};
