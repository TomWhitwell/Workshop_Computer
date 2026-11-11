/*
 * TELEXo Eurorack Module
 * (c) 2016, 2017 Brendon Cassidy
 * MIT License
 */
 
#pragma once

#ifndef CVOutput_h
#define CVOutput_h

#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include <cstdint>

#include "TELEXo/Output.h"
#include "TELEXo/TriggerOutput.h"
#include "TELEXo/Quantizer.h"
#include "TELEXo/Oscillator.h"
#include "TELEXo/ExpTable.h"

#include "telexio.h"

#define RETRIGGERMS 5
#define DACCENTER 32767

#define FASTRUN // get rid of this arduino keyword
#define max(a,b) ((a) > (b) ? (a) : (b)) // include the max keyword

// 50 microseconds per millisecond - 1000 / 50

struct SlewSteps {
  long Duration = 0;
  int Steps = 0; 
  long Delta = 0;
};

class CVOutput : public Output
{
  public:
  
    //CVOutput(int output, int led, DAC& dac);
    CVOutput(TelexIO& telex, int output, int led);

    void ReferenceTriggers(TriggerOutput (*triggerOutputs[]), int count);

    // audio-rate update method
    int16_t Update();

    void SetValue(int value);
    void TargetValue(int value);
    void SetSlew(int slew, short format);
    void SetOffset(int value);
    void SetLog(int value);

    int Calibrate();
    void ResetCalibration();
    void SetCalibrationValue(int value);

    // quantization
    void SetQuantizationScale(int scale);
    void SetQuantizedValue(int value);
    void TargetQuantizedValue(int value);
    void SetNote(int note);
    void TargetNote(int note);

    // OSC/LFO
    void SetFrequency(int freq);
    void TargetFrequency(int freq);
    void SetVOct(int value);
    void SetVOct_withoracle(int value); // oracle added
    void TargetVOct(int value);
    void TargetVOct_withoracle(int value); // oracle added
    void SetLFO(int millihertz);
    void TargetLFO(int millihertz);
    void SetWaveform(int wave);
    void SetWidth(int width);
    void SetRectify(int mode);
    void Sync();
    void SetPhaseOffset(int phase);
    void SetFrequencySlew(int slew, short format);
    void SetCycle(int value, short format);
    void TargetCycle(int value, short format);
    void SetCenter(int value);

    void SetOscQuantizationScale(int scale);
    void SetQuantizedVOct(int value);
    void SetQuantizedVOct_withoracle(int value);
    float QuantizedVOct_oraclewrapper(int value);
    void TargetQuantizedVOct(int value);
    void TargetQuantizedVOct_withoracle(int value);
    void SetOscNote(int note);
    void TargetOscNote(int note);

    // Envelope Generator
    void SetAttack(int att, short format);
    void SetDecay(int dec, short format);
    void SetEnvelopeMode(int mode);
    void TriggerEnvelope();
    void SetLoop(int loopEnv);

    void SetEOR(int trNumber);
    void SetEOC(int trNumber);

    void SetENV(int value);

    // reset
    void Reset();
    
    // overidden implementation
    void SetTimeFormat(int format);

    // virtual implementations
    void Kill();
    uint16_t UpdateLED();
    
  protected:

    static const uint8_t _ledMap[256];
    
  private:

    void RecomputeEnvelopes();
    
    volatile long _current = 0;
    long _target = 0;
    long _tempTarget = 0;

    int _smallCurrent = 0;
    
    bool _set = false;
    
    SlewSteps _slew;
    // 1ms is the teletypes default value for slew time
    unsigned long _slewTime = 1;
    
    //float _tempMS = 0.;
    
    int _offset = 0;
    long _lOffset = 0;

    int _calibration = 0;

    int _zero = 0;
    long _lZero = 0;

    int _ledHelper;
    volatile bool _updateLED = false;
    
    volatile int _cvHelper;

    //DAC _dac;

    int16_t UpdateDAC(int16_t value);
    void CalculateSlewValue();
    SlewSteps CalculateRawSlew(long value, long target, long current);
    int Constrain(int value);

    Quantizer *_quantizer;

    Quantizer *_oscQuantizer;

    Oscillator *_oscillator;
    volatile bool _oscilMode = false;

    void SharedOscil(int value);

    //int _dacCenter = DACCENTER;
    int _oscilCenter = 0;

    //int const _peak = DAC_MAX_SCALE - 32769;

    unsigned long _attack = 12;
    unsigned long _decay = 250;

    SlewSteps _attackSlew;
    SlewSteps _decaySlew;
    SlewSteps _retriggerSlew;

    bool _envelopeState = false;
    bool _envelopeMode = false;
    long _envTarget = 0;
    bool _envelopeActive = false;
    bool _decaying = false;
    bool _retrigger = false;
    bool _envLoop = false;
    bool _infLoop = false;
    int _loopTimes = -1;
    int _loopCount = 0;

    volatile bool _peakLED = false;

    TriggerOutput **_triggerOutputs;
    int _triggerOutputCount = 0;

    bool _triggerEOR = false;
    int _triggerForEOR = -1;
    bool _triggerEOC = false;
    int _triggerForEOC = -1;

    bool _doLog = false;
    uint8_t _logRange = 1;
    bool _wasNg = false;

};

extern CVOutput* cvOutputs[4];

__attribute__((always_inline))
inline int16_t __not_in_flash_func(CVOutput::Update)() {

  if (_set || _slew.Steps == 1){
    
    _smallCurrent = _target >> 15;
    
    // set the CV directly (skipping any slew behavior)
    // UpdateDAC(_smallCurrent);
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
        {  
          _triggerOutputs[_triggerForEOR]->Pulse(); 
        }
        
      } else if (_envelopeState) {
        _updateLED = false;
      }
      
    } else {

      // pulse the EOC trigger (if set)
      if (_envelopeMode && _decaying && _triggerEOC) 
      {  
          _triggerOutputs[_triggerForEOR]->Pulse(); 
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
    // UpdateDAC(_smallCurrent);
    _updateLED = true;
    
  } else if (_oscilMode) { 
    
    // just update the dac
    // UpdateDAC(_smallCurrent);

  }

  return UpdateDAC(_smallCurrent);
}



__attribute__((always_inline))
inline int16_t __not_in_flash_func(CVOutput::UpdateDAC)(int16_t value){

  // do log translation
  if (_doLog){
    if (value < 0){
      value *= -1;
      _wasNg = true;
    } else {
      _wasNg = false;
    }
    value = ExpTable[constrain(value << _logRange, 0 , 32767)];
    if (_wasNg) value *= -1;
    value = value >> _logRange;
  }

  // invert for DAC circuit
  if (_oscilMode)
    {
      int16_t oscValue = static_cast<int16_t>(_oscillator->Oscillate());
      value = static_cast<int16_t>((static_cast<int32_t>(value) * oscValue) >> 15);
    }

  // added the conditional write only if the CV value changes
  // if (value != _cvHelper){
  //   _cvHelper = value;
  //   _dac.writeChannel(_output, (_dacCenter - _cvHelper));
  
  _cvHelper = value;
  return value;
}  

__attribute__((always_inline))
inline void __not_in_flash_func(CVOutput::RecomputeEnvelopes)()
{
    if (_decaying) {
        const unsigned long steps =
            static_cast<unsigned long>(_slew.Steps);

        const unsigned long remaining =
            (steps > 0) ? (steps - 1) / KRATE : 0;

        _slew = CalculateRawSlew(remaining, _lOffset, _current);
    }
    else if (_envelopeActive) {
        const unsigned long steps =
            static_cast<unsigned long>(_slew.Steps);

        const unsigned long remaining =
            (steps > 0) ? (steps - 1) / KRATE : 0;

        _slew = CalculateRawSlew(remaining, _envTarget, _current);
    }

    _attackSlew = CalculateRawSlew(
        _attack,
        _envTarget,
        _lOffset
    );

    _decaySlew = CalculateRawSlew(
        _decay,
        _lOffset,
        _envTarget
    );
}

__attribute__((always_inline))
inline void __not_in_flash_func(CVOutput::TriggerEnvelope)(){

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

#endif

