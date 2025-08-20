// Talkie library
// Copyright 2011 Peter Knight
// This code is released under GPLv2 license.

// Modified by Chris Johnson 2024 to
// - be run from a per-audio-sample callback
// - separate frame and sample evaluation, allowing independent
//   speaking speed and pitch modification

#pragma once

#include <inttypes.h>
#ifdef ARDUINO
#include "Print.h"
#endif
#include "Vocab_Special.h"
#include "Vocab_US_Large.h"
//#include "Vocab_Toms_Diner.h"

#define CHIRP_SIZE 41
#define FS 8000  // Speech engine sample rate
#define LENGT_OF_FLOAT_STRING 14

/**
 * @brief Talkie is a software implementation of the Texas Instruments speech
 * synthesis architecture (Linear Predictive Coding) from the late 1970s.
 *
 *  @author Peter Knight, Phil Schatzmann
 */

class TalkiePCM {
 public:
  TalkiePCM() = default;
#ifdef ARDUINO
  TalkiePCM(Print& out, int channelCount = 1) {
    setOutput(out);
    channels = channelCount;
  }
#endif
  /// converts samples to csv string format
  void setOutputAsText(bool flag) { isOutputText = flag; }

  /// converts the provided word into samples
  void say(const uint8_t* address) {
    setPtr(address);
  }

  /// outputs silence for the indicated milliseconds
  void silence(uint16_t ms) {
    int samples = 8000 * ms / 1000;
    for (int j = 0; j < samples; j++) {
      writeSample(0);
    }
  }

  void sayPause() { say(spPAUSE1); }

  void sayDigit(char aDigit) { return sayNumber(aDigit - '0'); }


  void sayTimeout() {
    say(sp2_TIME);
    say(sp2_OUT);
  }

  /// say any number between -999,999 and 999,999
  void sayNumber(long aNumber) {
	newWord = true;
    if (aNumber < 0) {
      say(sp2_MINUS);
      sayNumber(-aNumber);
    } else if (aNumber == 0) {
      say(sp2_ZERO);
    } else {
      if (aNumber >= 1000) {
        int thousands = aNumber / 1000;
        sayNumber(thousands);
        say(sp2_THOUSAND);
        aNumber %= 1000;
        if ((aNumber > 0) && (aNumber < 100)) say(sp2_AND);
      }
      if (aNumber >= 100) {
        int hundreds = aNumber / 100;
        sayNumber(hundreds);
        say(sp2_HUNDRED);
        aNumber %= 100;
        if (aNumber > 0) say(sp2_AND);
      }
      if (aNumber > 19) {
        int tens = aNumber / 10;
        switch (tens) {
          case 2:
            say(sp2_TWENTY);
            break;
          case 3:
            say(sp2_THIR_);
            say(sp2_T);
            break;
          case 4:
            say(sp2_FOUR);
            say(sp2_T);
            break;
          case 5:
            say(sp2_FIF_);
            say(sp2_T);
            break;
          case 6:
            say(sp2_SIX);
            say(sp2_T);
            break;
          case 7:
            say(sp2_SEVEN);
            say(sp2_T);
            break;
          case 8:
            say(sp2_EIGHT);
            say(sp2_T);
            break;
          case 9:
            say(sp2_NINE);
            say(sp2_T);
            break;
        }
        aNumber %= 10;
      }
      switch (aNumber) {
        case 1:
          say(sp2_ONE);
          break;
        case 2:
          say(sp2_TWO);
          break;
        case 3:
          say(sp2_THREE);
          break;
        case 4:
          say(sp2_FOUR);
          break;
        case 5:
          say(sp2_FIVE);
          break;
        case 6:
          say(sp2_SIX);
          break;
        case 7:
          say(sp2_SEVEN);
          break;
        case 8:
          say(sp2_EIGHT);
          break;
        case 9:
          say(sp2_NINE);
          break;
        case 10:
          say(sp2_TEN);
          break;
        case 11:
          say(sp2_ELEVEN);
          break;
        case 12:
          say(sp2_TWELVE);
          break;
        case 13:
          say(sp2_THIR_);
          say(sp2__TEEN);
          break;
        case 14:
          say(sp2_FOUR);
          say(sp2__TEEN);
          break;
        case 15:
          say(sp2_FIF_);
          say(sp2__TEEN);
          break;
        case 16:
          say(sp2_SIX);
          say(sp2__TEEN);
          break;
        case 17:
          say(sp2_SEVEN);
          say(sp2__TEEN);
          break;
        case 18:
          say(sp2_EIGHT);
          say(sp2__TEEN);
          break;
        case 19:
          say(sp2_NINE);
          say(sp2__TEEN);
          break;
      }
    }
  }


#ifdef ARDUINO
  /// Defines the Arduino data target
  void setOutput(Print& out) { p_print = &out; }
#endif

  /// Defines the data callback that receives the generated samples
  void setDataCallback(void (*cb)(int16_t* data, int len)) {
    data_callback = cb;
  }

  /// Defines the number of generated output channels (2=stereo). Default is 1 =
  /// mono.
  void setChannels(uint16_t ch) { channels = ch; }

  /// Volume factor: > 1.0f means amplify; < 1.0f means lower volume. Default
  /// is 1.0f
  void setVolume(float vol) { volume = vol; }

 protected:
#ifdef ARDUINO
  Print* p_print = nullptr;
#endif
	bool newWord = false;
  uint16_t channels = 1;
  bool isOutputText = false;
  const uint8_t* ptrAddr = nullptr;
  uint8_t ptrBit;
  uint8_t synthPeriod;
  int16_t synthK1, synthK2;
  int8_t synthK3, synthK4, synthK5, synthK6, synthK7, synthK8, synthK9,
      synthK10;
	
  int16_t synthKv[11];
  float volume = 1.0f;
  void (*data_callback)(int16_t* data, int len) = nullptr;

  const uint8_t tmsEnergy[0x10] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x0a, 0x0f,
                             0x14, 0x20, 0x29, 0x39, 0x51, 0x72, 0xa1, 0xff};
  const uint8_t tmsPeriod[0x40] = {
      0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
      0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
      0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2D, 0x2F, 0x31, 0x33,
      0x35, 0x36, 0x39, 0x3B, 0x3D, 0x3F, 0x42, 0x45, 0x47, 0x49, 0x4D,
      0x4F, 0x51, 0x55, 0x57, 0x5C, 0x5F, 0x63, 0x66, 0x6A, 0x6E, 0x73,
      0x77, 0x7B, 0x80, 0x85, 0x8A, 0x8F, 0x95, 0x9A, 0xA0};

	const int16_t pitchcv[0x40]=  {0, 1133, 1104, 1075, 1049, 1023,  999,  977,  955,  934,  914,  894,  876,  858,  841,  824,  808,  792,  777,  762,  748,  734,  721,  707,  695,  682,  670,  658,  647,  624,  603,  582,  563,  544,  534,  508,  491,  474,  458,  436,  414,  400,  386,  360,  347,  335,  311,  300,  272,  256,  236,  221,  202,  184,  162,  145,  129,  109,   91,   72,   55,   35,   18, 0};

  const uint16_t tmsK1[0x20] = {
      0x82C0, 0x8380, 0x83C0, 0x8440, 0x84C0, 0x8540, 0x8600, 0x8780,
      0x8880, 0x8980, 0x8AC0, 0x8C00, 0x8D40, 0x8F00, 0x90C0, 0x92C0,
      0x9900, 0xA140, 0xAB80, 0xB840, 0xC740, 0xD8C0, 0xEBC0, 0x0000,
      0x1440, 0x2740, 0x38C0, 0x47C0, 0x5480, 0x5EC0, 0x6700, 0x6D40};
  const uint16_t tmsK2[0x20] = {
      0xAE00, 0xB480, 0xBB80, 0xC340, 0xCB80, 0xD440, 0xDDC0, 0xE780,
      0xF180, 0xFBC0, 0x0600, 0x1040, 0x1A40, 0x2400, 0x2D40, 0x3600,
      0x3E40, 0x45C0, 0x4CC0, 0x5300, 0x5880, 0x5DC0, 0x6240, 0x6640,
      0x69C0, 0x6CC0, 0x6F80, 0x71C0, 0x73C0, 0x7580, 0x7700, 0x7E80};
  const uint8_t tmsK3[0x10] = {0x92, 0x9F, 0xAD, 0xBA, 0xC8, 0xD5, 0xE3, 0xF0,
                         0xFE, 0x0B, 0x19, 0x26, 0x34, 0x41, 0x4F, 0x5C};
  const uint8_t tmsK4[0x10] = {0xAE, 0xBC, 0xCA, 0xD8, 0xE6, 0xF4, 0x01, 0x0F,
                         0x1D, 0x2B, 0x39, 0x47, 0x55, 0x63, 0x71, 0x7E};
  const uint8_t tmsK5[0x10] = {0xAE, 0xBA, 0xC5, 0xD1, 0xDD, 0xE8, 0xF4, 0xFF,
                         0x0B, 0x17, 0x22, 0x2E, 0x39, 0x45, 0x51, 0x5C};
  const uint8_t tmsK6[0x10] = {0xC0, 0xCB, 0xD6, 0xE1, 0xEC, 0xF7, 0x03, 0x0E,
                         0x19, 0x24, 0x2F, 0x3A, 0x45, 0x50, 0x5B, 0x66};
  const uint8_t tmsK7[0x10] = {0xB3, 0xBF, 0xCB, 0xD7, 0xE3, 0xEF, 0xFB, 0x07,
                         0x13, 0x1F, 0x2B, 0x37, 0x43, 0x4F, 0x5A, 0x66};
  const uint8_t tmsK8[0x08] = {0xC0, 0xD8, 0xF0, 0x07, 0x1F, 0x37, 0x4F, 0x66};
  const uint8_t tmsK9[0x08] = {0xC0, 0xD4, 0xE8, 0xFC, 0x10, 0x25, 0x39, 0x4D};
  const uint8_t tmsK10[0x08] = {0xCD, 0xDF, 0xF1, 0x04, 0x16, 0x20, 0x3B, 0x4D};
  const uint8_t chirp[CHIRP_SIZE] = {
      0x00, 0x2a, 0xd4, 0x32, 0xb2, 0x12, 0x25, 0x14, 0x02, 0xe1, 0xc5,
      0x02, 0x5f, 0x5a, 0x05, 0x0f, 0x26, 0xfc, 0xa5, 0xa5, 0xd6, 0xdd,
      0xdc, 0xfc, 0x25, 0x2b, 0x22, 0x21, 0x0f, 0xff, 0xf8, 0xee, 0xed,
      0xef, 0xf7, 0xf6, 0xfa, 0x00, 0x03, 0x02, 0x01};
  int16_t nextSample = 0;
  uint8_t periodCounter = 0;
  int16_t x[10] = {0};

  void setPtr(const uint8_t* addr) {
    ptrAddr = addr;
    ptrBit = 0;
  }

  // The ROMs used with the TI speech were serial, not byte wide.
  // Here's a handy routine to flip ROM data which is usually reversed.
  uint8_t rev(uint8_t a) {
    // 76543210
    a = (a >> 4) | (a << 4);  // Swap in groups of 4
    // 32107654
    a = ((a & 0xcc) >> 2) | ((a & 0x33) << 2);  // Swap in groups of 2
    // 10325476
    a = ((a & 0xaa) >> 1) | ((a & 0x55) << 1);  // Swap bit pairs
    // 01234567
    return a;
  }

  uint8_t getBits(uint8_t bits) {
    // prevent NPE
    if (ptrAddr == nullptr) return 0;
    uint8_t value;
    uint16_t data;
    data = rev(*(ptrAddr)) << 8;
    if (ptrBit + bits > 8) {
      data |= rev(*(ptrAddr + 1));
    }
    data <<= ptrBit;
    value = data >> (16 - bits);
    ptrBit += bits;
    if (ptrBit >= 8) {
      ptrBit -= 8;
      ptrAddr++;
    }
    return value;
  }

  int clip(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
  }

  void writeSample(int16_t sample) {
    // scale to 16 bits;
    int16_t outSample;
    if (volume == 1.0f)
      outSample = clip(static_cast<int>(sample) << 6, -32768, 32767);
    else
      outSample = clip(volume * (static_cast<int>(sample) << 6), -32768, 32767);

    // provide data via callback
    if (data_callback) {
      int16_t out[channels] = {0};
      for (int j = 0; j < channels; j++) out[j] = outSample;
      data_callback(out, channels);
    }

#ifdef ARDUINO
    // provide data to Arduino Print
    if (p_print) {
      ;
      if (isOutputText) {
        for (int j = 0; j < channels; j++) {
          if (j > 0) p_print->print(", ");
          p_print->print(outSample);
        }
        p_print->println();
      } else {
        int16_t out[channels] = {0};
        for (int j = 0; j < channels; j++) out[j] = outSample;
        p_print->write((uint8_t*)&(out[0]), sizeof(out));
      }
    }
#endif
  }

  /**
   * In the original implementation the processEnergy logic was executed with the help
   * of a timer interrupt.
   * The say method was generating the sample values in parallel by updating the
   * synthEnergy value with the logic find in the calculateSamples method.
   *
   * We drive the process by calling calculateSample() which itself executes the
   * process logic.
   */

	int16_t processEnergy(uint16_t synthEnergy, bool replaceExciter, int16_t inputTone, int16_t &outPreFilter)
	{
		
		static uint16_t synthRand = 1;
		int16_t u[11] = {0};
/*
	if (synthKv[1]<synthK1) synthKv[1]++;
	if (synthKv[1]>synthK1) synthKv[1]--;
	if (synthKv[2]<synthK2) synthKv[2]++;
	if (synthKv[2]>synthK2) synthKv[2]--;
	if (synthKv[3]<synthK3) synthKv[3]++;
	if (synthKv[3]>synthK3) synthKv[3]--;
	if (synthKv[4]<synthK4) synthKv[4]++;
	if (synthKv[4]>synthK4) synthKv[4]--;
	if (synthKv[5]<synthK5) synthKv[5]++;
	if (synthKv[5]>synthK5) synthKv[5]--;
	if (synthKv[6]<synthK6) synthKv[6]++;
	if (synthKv[6]>synthK6) synthKv[6]--;
	if (synthKv[7]<synthK7) synthKv[7]++;
	if (synthKv[7]>synthK7) synthKv[7]--;
	if (synthKv[8]<synthK8) synthKv[8]++;
	if (synthKv[8]>synthK8) synthKv[8]--;
	if (synthKv[9]<synthK9) synthKv[9]++;
	if (synthKv[9]>synthK9) synthKv[9]--;
	if (synthKv[10]<synthK10) synthKv[10]++;
	if (synthKv[10]>synthK10) synthKv[10]--;
*/
    // original logic: update pwm
    int16_t retval = nextSample;

    if (synthPeriod) {
      // Voiced source
      if (periodCounter < synthPeriod) {
        periodCounter++;
      } else {
        periodCounter = 0;
      }
      if (periodCounter < CHIRP_SIZE) {
		  u[10] = ((chirp[periodCounter]) * (uint32_t)synthEnergy) >> 8;
      } else {
        u[10] = 0;
      }
    } else {
      // Unvoiced source
      synthRand = (synthRand >> 1) ^ ((synthRand & 1) ? 0xB800 : 0);
      u[10] = (synthRand & 1) ? synthEnergy : -synthEnergy;
    }
	outPreFilter = u[10];
	if (replaceExciter)
	{
		u[10] = (inputTone>>3);
		if (!synthPeriod)
		{
			synthRand = (synthRand >> 1) ^ ((synthRand & 1) ? 0xB800 : 0);
			u[10] += ((synthRand & 1) ? synthEnergy : -synthEnergy)>>1;
		}
	}
    // Lattice filter forward path
    u[9] = u[10] - (((int16_t)synthKv[10] * x[9]) >> 7);
    u[8] = u[9] - (((int16_t)synthKv[9] * x[8]) >> 7);
    u[7] = u[8] - (((int16_t)synthKv[8] * x[7]) >> 7);
    u[6] = u[7] - (((int16_t)synthKv[7] * x[6]) >> 7);
    u[5] = u[6] - (((int16_t)synthKv[6] * x[5]) >> 7);
    u[4] = u[5] - (((int16_t)synthKv[5] * x[4]) >> 7);
    u[3] = u[4] - (((int16_t)synthKv[4] * x[3]) >> 7);
    u[2] = u[3] - (((int16_t)synthKv[3] * x[2]) >> 7);
    u[1] = u[2] - (((int32_t)synthKv[2] * x[1]) >> 15);
    u[0] = u[1] - (((int32_t)synthKv[1] * x[0]) >> 15);

    // Output clamp
    u[0] = clip(u[0], -512, 511);

    // Lattice filter reverse path
    x[9] = x[8] + (((int16_t)synthKv[9] * u[8]) >> 7);
    x[8] = x[7] + (((int16_t)synthKv[8] * u[7]) >> 7);
    x[7] = x[6] + (((int16_t)synthKv[7] * u[6]) >> 7);
    x[6] = x[5] + (((int16_t)synthKv[6] * u[5]) >> 7);
    x[5] = x[4] + (((int16_t)synthKv[5] * u[4]) >> 7);
    x[4] = x[3] + (((int16_t)synthKv[4] * u[3]) >> 7);
    x[3] = x[2] + (((int16_t)synthKv[3] * u[2]) >> 7);
    x[2] = x[1] + (((int32_t)synthKv[2] * u[1]) >> 15);
    x[1] = x[0] + (((int32_t)synthKv[1] * u[0]) >> 15);
    x[0] = u[0];

    // nextPwm = (u[0] >> 2) + 0x80;
    nextSample = u[0];
	return retval;
  }
public:
	bool calculateNextFrame(int incr, int16_t &outEnergy, int16_t &outPitch) {
  	static uint8_t energy = 0;
	//   static uint16_t synthEnergy = 0;
	static int frame = 10000000, subframe = 0;
	static  int16_t smoothedEnergy=0, smoothedpitchcv=0, thispitchcv=0;
	frame += incr;
	if (frame >= 10000)
	{
		frame -= 10000;
		
		synthKv[1] = (synthKv[1]*3 + synthK1)>>2;
		synthKv[2] = (synthKv[2]*3 + synthK2)>>2;
		synthKv[3] = (synthKv[3]*3 + synthK3)>>2;
		synthKv[4] = (synthKv[4]*3 + synthK4)>>2;
		synthKv[5] = (synthKv[5]*3 + synthK5)>>2;
		synthKv[6] = (synthKv[6]*3 + synthK6)>>2;
		synthKv[7] = (synthKv[7]*3 + synthK7)>>2;
		synthKv[8] = (synthKv[8]*3 + synthK8)>>2;
		synthKv[9] = (synthKv[9]*3 + synthK9)>>2;
		synthKv[10] = (synthKv[10]*3 + synthK10)>>2;
		smoothedEnergy = (smoothedEnergy*3 + synthEnergy)>>2;
		smoothedpitchcv = (smoothedpitchcv*3 + thispitchcv)>>2;
		outEnergy = smoothedEnergy;
		outPitch = smoothedpitchcv;
		
		subframe++;
		if (subframe==10)
		{
			subframe=0;
			// Read speech data, processing the variable size frames.
			if (newWord)
			{
				energy = 0;
				newWord = false;
			}
			if (energy != 0xf)
			{
				energy = getBits(4);
				if (energy == 0) {
					// Energy = 0: rest frame
					synthEnergy = 0;
					thispitchcv = 0;
				} else if (energy == 0xf) {
					// Energy = 15: stop frame. Silence the synthesiser.
					thispitchcv = 0;
					synthEnergy = 0;
					synthK1 = 0;
					synthK2 = 0;
					synthK3 = 0;
					synthK4 = 0;
					synthK5 = 0;
					synthK6 = 0;
					synthK7 = 0;
					synthK8 = 0;
					synthK9 = 0;
					synthK10 = 0;
				} else {
					synthEnergy = tmsEnergy[energy];
					bool repeat = getBits(1);
					int pitchInd = getBits(6);
					thispitchcv = pitchcv[pitchInd];
					synthPeriod = tmsPeriod[pitchInd];
					// A repeat frame uses the last coefficients
					if (!repeat) {
						// All frames use the first 4 coefficients
						synthK1 = tmsK1[getBits(5)];
						synthK2 = tmsK2[getBits(5)];
						synthK3 = tmsK3[getBits(4)];
						synthK4 = tmsK4[getBits(4)];
						if (synthPeriod) {
							// Voiced frames use 6 extra coefficients.
							synthK5 = tmsK5[getBits(4)];
							synthK6 = tmsK6[getBits(4)];
							synthK7 = tmsK7[getBits(4)];
							synthK8 = tmsK8[getBits(3)];
							synthK9 = tmsK9[getBits(3)];
							synthK10 = tmsK10[getBits(3)];
						}
					}
				}
			}
		}
	}
	return energy==0xf;
	}
	
	int16_t calculateNextSample(bool replaceExciter, int16_t inputTone, int16_t &preFilterOutput)
	{
		return clip(static_cast<int>(processEnergy(synthEnergy, replaceExciter, inputTone, preFilterOutput)) << 6, -32768, 32767);
	}
	uint16_t synthEnergy;
};
