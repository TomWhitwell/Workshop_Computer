#include "ComputerCard.h"
#include "TalkiePCM.h"

#include <cstdlib> // for abs

class TalkiePCMCard : public ComputerCard
{
public:
	TalkiePCMCard()
	{
		sampleRamp = 0;
		sample = 0;
		voice.sayNumber(RandomDigit());
	}

	int16_t cvenergy, cvpitch;
	int32_t cvenergy2, cvpitch2;
	
	virtual void ProcessSample()
	{
		Switch s = SwitchVal();
		
		// Filter speed (pitch) set by CV 1 with knob X as attenuverter, added to main knob
		int incr = KnobVal(Knob::Main) + (CVIn1() * (KnobVal(Knob::X)-2048) >> 11);
		if (incr<0) incr = 0;
		sampleRamp += incr;

		// Frame speed (speaking speed) set by Knob Y + CV 2
		int frameIncr = (KnobVal(Knob::Y)>>3) + (CVIn2()>>3);
		if (frameIncr<0) frameIncr = 0;
		bool finished = voice.calculateNextFrame(frameIncr,cvenergy, cvpitch);

		cvenergy2 = (15*int32_t(cvenergy2) + (cvenergy<<3))>>4;
		cvpitch2 = (15*int32_t(cvpitch2) + cvpitch)>>4;
		CVOut1(cvenergy2);
		CVOut2(cvpitch);
		// If: word has finished and switch up,
		//     or switch pulled down,
		//     or rising edge on pulse 1,
		//  then say a new digit
		if ((finished && s == Switch::Up)
			|| (s == Switch::Down && lasts != Switch::Down)
			|| PulseIn1RisingEdge())
		{
			// Say the new digit
			voice.sayNumber(RandomDigit());
			
			// Start a new pulse on Pulse out 1 and LED 1
			pulseTimer=100; // 100 is ~2ms, enough to trigger Slopes
			PulseOut1(true);
			LedOn(1, true);
		}

		// If a pulse is ongoing, keep counting until it ends
		if (pulseTimer)
		{
			pulseTimer--;
			if (pulseTimer==0) // pulse ends
			{
				PulseOut1(false);
				LedOn(1, false);
			}
		}
		
		int16_t preFilterOutput;
		if (sampleRamp>8192) // Each new sample
		{
			sampleRamp-=8192;

			// Calculate new sample
			sample = voice.calculateNextSample(Connected(Input::Audio1), AudioIn1(), preFilterOutput);
			
			// Light LED 0 according to signal
			LedBrightness(0, abs(sample)<<1);
		}

		// Play the last sample through audio out, no fancy interpolation
		AudioOut1(sample>>4);
		AudioOut2(preFilterOutput<<4);
		lasts = s;
	}
	
private:
	int RandomDigit()
	{
		// Random number up to 2^32-1 divided down to get digits 0-9
		static uint32_t lcg_seed = 1;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed/429496730;
	}
	
	TalkiePCM voice;
	int sampleRamp;
	int16_t sample;
	int pulseTimer;
	Switch lasts;
};



int main()
{
	TalkiePCMCard tpcm;
	tpcm.EnableNormalisationProbe();
	tpcm.Run();
}

  
