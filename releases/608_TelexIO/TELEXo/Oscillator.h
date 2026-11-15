/*
 * TELEXo Eurorack Module
 * (c) 2016, 2017 Brendon Cassidy
 * MIT License
 */
 
//
// Note!
// bugs fixed - incorrect asignment of blepone and bleptwo in setwave in cpp file
//            - incorrect applicaiton of blep to square wave

#ifndef Oscillator_h
#define Oscillator_h

#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt))) // replace constrain from Arduino

#include <cstdint>
#include <cstdlib> 
#include "pico/stdlib.h"
#include "pico/rand.h" // look at it later, added to linked libraries in makefile also
#define random(a, b) ((int)(get_rand_32() & 0xFFFF)) // look at it later

#include "Wavetables.h"
#include "samplerate.h"
#include "lut.h"

#define TABLERANGE 512
#define TABLERANGEDIV2 256
#define TABLESIZE 513

#define MORPHRANGE 100

#define PHASEBITS 18
#define TABLEBITS 9
#define REDUCEBITS 23 // 32 - TABLEBITS
#define PHASEMASK 8388607 // ( 1 << REDUCEBITS ) - 1
// #define PHASESCALE = 1.1920928955078125e-7 // 1.0 / ( 1 << REDUCEBITS )

#define FQ20K FNUM_LO

class Oscillator
{
  public:
  
    Oscillator();
    int32_t Oscillate();
    
    void SetFrequency(int freq);
    void TargetFrequency(int freq);
    void SetFloatFrequency(float freq);
    void TargetFloatFrequency(float freq);
    void SetLFO(int millihertz);
    void TargetLFO(int millihertz);

    void SetWaveform(int wave);
    void ResetPhase(long polarity);
    void SetPhaseOffset(int phase);
    void SetWidth(int width);
    void SetRectify(int mode);

    void SetPortamentoMs(unsigned long milliseconds);

    float GetFrequency();

    uint32_t ratio_q15(uint32_t u, uint32_t x);
    

  protected:

    void SetFreq(float freq);
    void TargetFreq(float freq);

    int32_t PolyBlepFixedNew(unsigned long ulT);

    const int peaks[2] = { 128, 256 };
  

  private:

  uint16_t _wave = 0;
  uint16_t _morphWave = 1;
  //int _morph = 0;
  //int _invMorph = MORPHRANGE;
  bool _morphing = false;
  int _morphValue = 0;
  uint16_t morphQ10 = 0;
  uint16_t invMorphQ10 = 1023 - morphQ10;

  volatile float _frequency = 0;
  unsigned long _ulstep = 0;
  unsigned long _oldPhase = 0;
  int _phaseOffset = 0;
  unsigned long _actualPhase = 0;
  int _phaseDelta = 0;
  
  //int _location;  // NOT NEEDED COMPUTERCARD.h
  float _remainder;
  
  int _lastValue;
  
  //int _width = TABLERANGEDIV2; //Unused?
  //float _fWidth = .5; // unsused?
  unsigned long _ulWidth = FULLPHASEL >> 1;

  int8_t _rectify = 0;
  bool _doRect = false;

  //double _phasescale = 1.0 / ( 1 << REDUCEBITS );

  // portamento
  unsigned long _targetUlstep = 0;
  unsigned long _stepsCalculated = 0;
  unsigned long _steps = 0;
  unsigned long _delta = 0;
  bool _portamento = false;
  bool _sign = true;

  // polyblep
  //double t = 0.0;
  bool _blepItOne = false;
  bool _blepItTwo = false;
  int _blepOne = 0.0;
  int _blepTwo = 0.0;

};

// __attribute__((always_inline))
// inline int32_t __not_in_flash_func(Oscillator::Oscillate)() {
// if (_portamento) {
//     if (_steps-- <= 0) {
//         _ulstep = _targetUlstep;
//         _portamento = false;
//     } else {
//         _ulstep = _sign ? _ulstep + _delta : _ulstep - _delta;
//     }
// }

// _actualPhase += _ulstep;

// if (_wave == SAW_WAVE) {
//     _lastValue = (int)(_actualPhase >> 16) - 32767;
// }

// return _lastValue;
// }



/*
 * The primary function called once per sample.
 * Every operation counts here; if you can, do math elsewhere.
 */


__attribute__((always_inline))
inline int32_t __not_in_flash_func(Oscillator::Oscillate)() {

  #define INTERP_BITS 15
  #define INTERP_SHIFT (REDUCEBITS - INTERP_BITS)
  #define INTERP_MASK ((1 << INTERP_BITS) - 1)

  // slew frequency?
  if (_portamento) {
    if (_steps-- <= 0){
      _ulstep = _targetUlstep;
      _portamento = false;
    } else {
      _ulstep = _sign ? _ulstep + _delta : _ulstep - _delta;
    }
  }

  // unsigned long automatically wraps
  _actualPhase += _ulstep;

  // reduce this down to meet the tablesize range
  //_location = _actualPhase >> REDUCEBITS;
  uint32_t location = _actualPhase >> REDUCEBITS;

  // too expensive to do this for the primary and morphing waveforms -
  // we do the PolyBlep calculations once for both
  if (_blepItOne && _ulstep >= FQ20K){
    _blepOne = PolyBlepFixedNew(_actualPhase);
    if (_blepItTwo){
      _blepTwo = PolyBlepFixedNew((FULLPHASEL - _ulWidth + 1) + _actualPhase);
    }
  }

  // optimized to chained if statements
  if (_wave == SQUARE_WAVE) { 
    _lastValue =  _actualPhase < _ulWidth ? 32767 : -32767;    
  #ifdef TURBO
    // polyblep frequencies above 20k
    if (_ulstep >= FQ20K){
      _lastValue += _blepOne;
      _lastValue -= _blepTwo;     
    }
  #endif
  } else if (_wave == SAW_WAVE) {  
    // do actual calculations when we have the CPU
    _lastValue = (int)(_actualPhase >> 16) - 32767;
  #ifdef TURBO
    // polyblep frequencies above 20k
    if (_ulstep >= FQ20K)
      _lastValue -= _blepOne;      
  } else if (_wave == TRIANGLE_WAVE) { 
    // do actual calculations when we have the CPU  
    _lastValue = _actualPhase & 0x80000000 ? (int)((FULLPHASEL - _actualPhase) >> 15) - 32767 : (int)(_actualPhase >> 15) - 32767;
  #endif 
  // fall back on the table if we don't have the CPU to spare
  } else if (_wave < WAVETABLECOUNT) {
    #ifdef BASIC
    if (_portamento || _morphing || _doRect){
      // no interpolation or rounding
      _lastValue =  wavetables[_wave][location];
    } else {  
    #endif
      // interpolate using some fixed math magic (and a floating point scaler)
      // CONVERTED TO INT _lastValue = wavetables[_wave][_location] + (_actualPhase & PHASEMASK) * _phasescale * (wavetables[_wave][_location + 1] - wavetables[_wave][_location]);
      // fixed point version:
      uint16_t frac = (_actualPhase >> INTERP_SHIFT) & INTERP_MASK;
      _lastValue = wavetables[_wave][location] + ((frac * (wavetables[_wave][location + 1] - wavetables[_wave][location])) >> INTERP_BITS);
    #ifdef BASIC
    }
    #endif
  } else if (_wave == WAVETABLECOUNT) {
    // generate a new number if we have flipped
    if (_actualPhase < _oldPhase)
      _lastValue = random(0, 65536) - 32768;
    _oldPhase = _actualPhase;
  } else {
    _lastValue =  0;
  }

  // optimized by moving to chained if statements
  if (_morphing){
    if (_morphWave == SQUARE_WAVE) {
      _morphValue =  _actualPhase & 0x80000000 ? 32767 : -32767;   
    #ifdef TURBO
      // polyblep frequencies above 20k
      if (_ulstep >= FQ20K){
        _morphValue += _blepOne;  
        _morphValue -= _blepTwo;     
      }
    #endif
    } else if (_morphWave == SAW_WAVE) {  
      // do actual calculations when we have the CPU
      _morphValue = (int)(_actualPhase >> 16) - 32767;
    #ifdef TURBO
      // polyblep frequencies above 20k
      if (_ulstep >= FQ20K)
        _morphValue -= _blepOne;      
    } else if (_morphWave == TRIANGLE_WAVE) { 
      // do actual calculations when we have the CPU 
      _morphValue =  _actualPhase & 0x80000000 ? (int)((FULLPHASEL - _actualPhase) >> 15) - 32767 : (int)(_actualPhase >> 15) - 32767 ;  
    #endif 
    // fall back on the table if we don't have the CPU to spare
    } else if (_morphWave < WAVETABLECOUNT){
      #ifdef BASIC
      _morphValue =  wavetables[_morphWave][location];
      #else
      // REPLACED WITH INT _morphValue =  wavetables[_morphWave][location] + (_actualPhase & PHASEMASK) * _phasescale * (wavetables[_morphWave][location + 1] - wavetables[_morphWave][location]);
      uint16_t frac = (_actualPhase >> INTERP_SHIFT) & INTERP_MASK;
      _morphValue = wavetables[_morphWave][location] + ((frac * (wavetables[_morphWave][location + 1] - wavetables[_morphWave][location])) >> INTERP_BITS);
    #endif
    } else if (_morphWave == WAVETABLECOUNT) {
      if (_actualPhase < _oldPhase)
        _morphValue = random(0, 65536) - 32768;
      _oldPhase = _actualPhase;
    } else {
      _morphValue =  0;
    }
    //_lastValue = (_lastValue * _invMorph + _morphValue * _morph) / MORPHRANGE;
    _lastValue = (_lastValue * invMorphQ10 + _morphValue * morphQ10) >> 10;
  }

  // optimized by moving to sequential if statements and a rect bool
  if (_doRect){
    if(_rectify == -2){
      _lastValue = -abs(_lastValue);
    } else if (_rectify == -1) {
      _lastValue = _lastValue <= 0 ? _lastValue : 0;
    } else if (_rectify == 1) {
      _lastValue = _lastValue >= 0 ? _lastValue : 0;
    } else if (_rectify == 2) {
      _lastValue = abs(_lastValue);
    }
  }
  
  return _lastValue;
  
}

__attribute__((always_inline))
inline uint32_t __not_in_flash_func(Oscillator::ratio_q15)(uint32_t u, uint32_t x) // q15 actually
{
    // 0 <= u < x
    // Algorithm returns (u / x) * 32768 (i.e. Q15)

    // Note! Bounds defined in samplerate.h are assumed throughout this algo. See comments.
    if (u == 0 || x < FNUM_LO || x > FNUM_HI) return 0;

    // Normalise denominator
    // implement: uint32_t ex = 31u - __builtin_clz(x); 
    uint32_t ex = 31u - clz_lut16[x>>16]; // bound FNUM_LO used; implies lower word never contains leading zeros
    uint32_t mx = x >> (ex - 15u);

    // Normalise numerator 
    // implement:
    // uint32_t eu = 31u - __builtin_clz(u);
    // uint32_t mu = (eu >= 15u) ? (u >> (eu - 15u)) : (u << (15u - eu));   
    uint32_t eu,mu; 
    if (u <= (1<<15))
    {
      eu = 31u - (15u + clz_lut16[u>>1]); // uses u>=1, and also application of 16bit lut to u>>1 misses counting the top 15 bits 
      mu = (u << (15u - eu));             
    } else {
      eu = 31u - clz_lut16[u>>16];  // handles case u=((1<<15)+1)..((1<<16)-1) because that implies top word of u is 00000000
      mu = (u >> (eu - 15u));       // bound FNUM_HI used, lut construcgtion uses u<FNUM_HI to make sure the index remains valid 
    }

    // Take Q15 reciprocal of normalized denominator from LUT
    uint32_t index = (mx - 32768u) >> 5;
    //if (index > 1023) index = 1023;  // bound guaranteed by construction
    uint32_t r = reciprocal_lut[index];

    // Q15 * Q15 -> Q15 safe for uint32_t
    uint32_t p = (mu * r) >> 15;

    // Restore relative exponent:
    // u/x = (mu/mx) * 2^(eu-ex)
    // Since u < x, ex >= eu
    p = p >> (ex - eu);
 
    return p;
}



__attribute__((always_inline))
inline int32_t __not_in_flash_func(Oscillator::PolyBlepFixedNew)(unsigned long ulT)
{
    if (ulT < _ulstep)
    {
        int32_t t = (int32_t)(ratio_q15(ulT, _ulstep));
        int32_t t2 = (int32_t)(((uint32_t)t * (uint32_t)t) >> 15);

        return t+t-t2-32767;
    }
    else if (ulT > FULLPHASEL - _ulstep)
    {
        uint32_t u = FULLPHASEL - ulT;

        int32_t t = (int32_t)ratio_q15(u, _ulstep);
        int32_t t2 = (int32_t)(((uint32_t)t * (uint32_t)t) >> 15);

        return -t-t+t2+32767;
    }

    return 0;
}

#endif

