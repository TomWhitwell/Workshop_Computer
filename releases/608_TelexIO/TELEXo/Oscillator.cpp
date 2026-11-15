/*
 * TELEXo Eurorack Module
 * (c) 2016-2018 Brendon Cassidy
 * MIT License
 */

#include "Oscillator.h"

/*
 * Constructor; requires the sampling rate
 */
Oscillator::Oscillator() {
}

/*
 * Sets the frequency of the oscillator
 */
void __not_in_flash_func(Oscillator::SetFreq)(float freq){
  _frequency = freq;
  _portamento = false;
  //_ulstep = (int)((freq / SAMPLINGRATE) * FULLPHASE);
  _ulstep = (int)(freq * FULLPHASE_SAMPLINGRATE);
  #ifdef DEBUG
  Serial.printf("FQ: %f - %lu\n", freq, _ulstep); 
  #endif
}

/*
 * Targets the frequency for the oscillator (when portamento is active)
 */
void __not_in_flash_func(Oscillator::TargetFreq)(float freq){
  _frequency = freq;
  if (_stepsCalculated == 0){
    SetFreq(freq);
  } else {
    _targetUlstep = (int)((freq / SAMPLINGRATE) * FULLPHASE);
    if (_targetUlstep > _ulstep){
      _delta = (_targetUlstep - _ulstep) / _stepsCalculated;
      _sign = true;
    } else {
      _delta = (_ulstep - _targetUlstep) / _stepsCalculated;
      _sign = false;
    }
    _steps = _stepsCalculated;
    _portamento = true;
  }
}


/*
 * Sets the freqency via an integer
 */
void __not_in_flash_func(Oscillator::SetFrequency)(int freq) {
  freq = constrain(freq, 0, SAMPLINGRATEDIV2);
  SetFreq(freq);
}

/*
 * Targets the freqency via an integer (when portamento is active)
 */
void __not_in_flash_func(Oscillator::TargetFrequency)(int freq){
  freq = constrain(freq, 0, SAMPLINGRATEDIV2);
  TargetFreq(freq);
}

/*
 * Sets a floating point frequency
 */
void __not_in_flash_func(Oscillator::SetFloatFrequency)(float freq) {
  freq = constrain(freq, 0, SAMPLINGRATEDIV2);
  SetFreq(freq);
}

/*
 * Targets a floating point frequency (when portamento is active)
 */
void __not_in_flash_func(Oscillator::TargetFloatFrequency)(float freq){
  freq = constrain(freq, 0, SAMPLINGRATEDIV2);
  TargetFreq(freq);
}

/*
 * Sets the LFO in millihertz (10^3 Hz)
 */
void __not_in_flash_func(Oscillator::SetLFO)(int millihertz) {
  millihertz = constrain(millihertz, 0, 32767);
  //SetFreq((float)millihertz / 1000.);
  SetFreq((float)millihertz * 0.001f);
}

/*
 * Tarets the LFO in millihertz (10^3 Hz)
 */
void __not_in_flash_func(Oscillator::TargetLFO)(int millihertz) {
  millihertz = constrain(millihertz, 0, 32767);
  //TargetFreq((float)millihertz / 1000.);
  TargetFreq((float)millihertz * 0.001f);
}

/*
 * Sets the width of the pulse wave (0-100)
 */
void __not_in_flash_func(Oscillator::SetWidth)(int width) {
  width = constrain(width, 0, 100);
  _ulWidth = ((uint64_t)width * (FULLPHASEL - 1)) / 100; // inserted to avoid old float code
  //_fWidth = (float)width / 100.;
  //_ulWidth = _fWidth * (FULLPHASE - 1);
  //_width = _fWidth * (TABLERANGE - 1);

  #ifdef DEBUG
  Serial.printf("width: %d; _fWidth: %f; _ulWidth: %lu; _width: %d\n",width, _fWidth, _ulWidth, _width);
  #endif
  
}

/*
 * Sets the rectification mode:
 * -2 - full negative rectification (-(abs)value)
 * -1 - half negative rectification (ignores the positive values)
 *  0 - no rectification
 * +1 - half positive rectification (ignores the negative values)
 * +2 - full positive rectification ((abs)value)
 */
void __not_in_flash_func(Oscillator::SetRectify)(int mode) {
  _rectify = constrain(mode, -2, 2);
  _doRect = _rectify != 0;
}

/*
 * Sets the waveform for the oscillator
 */
void __not_in_flash_func(Oscillator::SetWaveform)(int wave) {
  _wave = constrain((wave / MORPHRANGE) % (WAVETABLECOUNT + 1), 0, WAVETABLECOUNT);
  #ifdef DEBUG
  Serial.printf("Waveform: %d [%d]\n", _wave, wave);
  #endif
  _morphWave = _wave + 1;
  if (_morphWave > WAVETABLECOUNT) _morphWave = 0;
  //_morph = wave % MORPHRANGE;
  //_invMorph = MORPHRANGE - _morph;
  morphQ10 = ((uint32_t)(wave - (_wave * 100)) * 10486 + 512) >> 10;
  invMorphQ10 = 1023 - morphQ10;
  _morphing = morphQ10 != 0;

  // reworked the following from the original code, it seemed a bug
  _blepItOne = false;
  _blepItTwo = false; 
  if (_wave == SAW_WAVE || _morphWave == SAW_WAVE){
    _blepItOne = true;
  }

  if (_wave == SQUARE_WAVE || _morphWave == SQUARE_WAVE) {
    _blepItOne = true;
    _blepItTwo = true;
  }
}

/*
 * Resets the phase of the oscillator to its default
 */
void __not_in_flash_func(Oscillator::ResetPhase)(long polarity) {
  if (polarity == 0)
    _actualPhase = _phaseOffset << PHASEBITS;
  else
    _actualPhase = _wave < 2 ? (unsigned long)peaks[_wave] : 0 << REDUCEBITS;
}

/*
 * Sets the oscillator's phase offset
 */
void __not_in_flash_func(Oscillator::SetPhaseOffset)(int phase) {
  phase = constrain(phase, 0, 16384);
  _phaseDelta = phase - _phaseOffset;
  _phaseOffset = phase;
  _actualPhase += _phaseDelta << PHASEBITS;
}

/*
 * Sets the time for portamento in milliseconds
 */
void __not_in_flash_func(Oscillator::SetPortamentoMs)(unsigned long milliseconds){
  _stepsCalculated = milliseconds * KRATE;
  if (_portamento && _steps > 0){
      if (_targetUlstep > _ulstep){
        _delta = (_targetUlstep - _ulstep) / _stepsCalculated;
        _sign = true;
      } else {
        _delta = (_ulstep - _targetUlstep) / _stepsCalculated;
        _sign = false;
      }
    _steps = _stepsCalculated;
  }
}

/*
 * Returns the floating point frequency of the oscillator
 */
float __not_in_flash_func(Oscillator::GetFrequency)(){
  return _frequency;
}

/*
 * PolyBLEP by Tale (slightly modified several times)
 * http://www.kvraudio.com/forum/viewtopic.php?t=375517
 * http://www.martin-finke.de/blog/articles/audio-plugins-018-polyblep-oscillator/
 * http://research.spa.aalto.fi/publications/papers/smc2010-phaseshaping/phaseshapers.py
*/ 
/*
double __not_in_flash_func(Oscillator::PolyBlepFixed)(unsigned long ulT){
    // 0 <= t < 1
    if (ulT < _ulstep) {
        t = (double)ulT / _ulstep;
        return (t+t - t*t - 1.0) * 32767;
    }
    // -1 < t < 0
    else if (ulT > FULLPHASE - _ulstep) {
        t = ((double)ulT - FULLPHASE) / _ulstep;
        return (t*t + t+t + 1.0) * 32767;
    }
    // 0 otherwise
    else return 0.0;
}
*/
