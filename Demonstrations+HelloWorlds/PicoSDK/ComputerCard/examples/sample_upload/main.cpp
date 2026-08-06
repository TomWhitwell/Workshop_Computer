#include "ComputerCard.h"
#include <vector>

#include <pico/bootrom.h>


////////////////////////////////////////
// A very simple WAV file parsing class.
//
// Takes a WAV file mapped into memory (e.g. in flash)
// and gives an interface to the samples and basic format info
// Currently hard-coded to 16-bit mono PCM files

class WAVFile
{
private:

	// Read 16- and 32-bit little-endian values from a buffer
	// (Can't just cast the 32-bit values, as they are not
	//  always aligned to 4-byte boundary)
	static uint16_t GetU16(uint8_t *&ptr)
	{
		uint16_t a1=*ptr++;
		uint16_t a2=*ptr++;
		return (a2<<8)+a1;
	}

	static uint32_t GetU32(uint8_t *&ptr)
	{
		uint32_t a1=*ptr++;
		uint32_t a2=*ptr++;
		uint32_t a3=*ptr++;
		uint32_t a4=*ptr++;
		return (a4<<24)+(a3<<16)+(a2<<8)+a1;
	}

public:
	WAVFile()
	{
		dataptr = nullptr;
		numSamples = 0;
		sampleRate = 0;
		fileSize = 0;
	}

	// Get raw sample
	int16_t operator[](uint32_t index)
	{
		if (!dataptr || index >= numSamples)
		{
			return 0;
		}
		
		return dataptr[index];
	}

	// Get sample value at fractional position index/256, linearly interpolated.
	// Interpolation at endpoint assumes that sample is looping
	int16_t Interp8(uint32_t index)
	{
		if (!dataptr || index >= (numSamples<<8))
		{
			return 0;
		}

		int32_t r = index & 0xFF;
		index >>= 8;

		uint32_t nextIndex = index+1;
		if (nextIndex > numSamples) nextIndex -= numSamples;
		
		return (dataptr[index]*(256-r) + dataptr[nextIndex]*r)>>8;
	}

	uint32_t SampleRate() {return sampleRate;}
	uint32_t FileSize() {return fileSize;}
	uint32_t NumSamples() {return numSamples;}
	uint16_t NumChannels() {return numChannels;}
	
	int Load(uint8_t *startptr)
	{
		uint8_t *ptr = startptr;
		uint32_t riffMarker = GetU32(ptr);
		if (riffMarker != 0x46464952) return 1;
	
		fileSize = GetU32(ptr) + 8;
		
		uint32_t waveMarker = GetU32(ptr);
		if (waveMarker != 0x45564157) return 2;

		uint32_t fmtMarker = GetU32(ptr);
		if (fmtMarker != 0x20746d66) return 3;

		uint32_t subChunkSize = GetU32(ptr);

		uint16_t audioFormat = GetU16(ptr);
		if (audioFormat != 1) return 4; // must be PCM format

		numChannels = GetU16(ptr);
		if (numChannels != 1) return 5; // must be mono

		sampleRate = GetU32(ptr);

		ptr += 6; // skip 'byte rate' and 'block align' fields
	
		uint16_t bitsPerSample  = GetU16(ptr);
		if (bitsPerSample != 16) return 6; // must be 16-bit

		// skip any 'extra params' which may or may not be in the format chunk
		if (subChunkSize>16) {ptr += subChunkSize-16;}
	
		uint32_t dataMarker = GetU32(ptr);
		if (dataMarker != 0x61746164) return 7;

		numSamples = GetU32(ptr) / numChannels / (bitsPerSample/8);

		
		// WAV file format specifies that all data is an even number
		// of bytes long, so 16-bit samples are aligned, so we can just cast here
		// (WAV and Arm Cortex M0+ both little endian too)
		dataptr = (int16_t*)ptr;

		return 0;
	}
	
	int16_t *dataptr;
	uint32_t numSamples;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t fileSize;
};


////////////////////////////////////////
// Card that loads and plays WAV files from flash memory
//
// WAV files must all be 16-bit mono

class SampleUpload : public ComputerCard
{
public:
	
	SampleUpload()
	{
		sampleIndex = 0;
		numFiles = 0;
		currentFile = 0;
		switchDownCount = 0;
		LoadWAVsFromFlash();
	}

	// Load WAV files uploaded to the RP2040 using a UF2 from the generate_sample_uf2.html page
	int LoadWAVsFromFlash()
	{
		// Look in last 256-byte block of flash to get start address and number of WAV files
		uint32_t *wavStart = ((uint32_t *)(XIP_BASE+ PICO_FLASH_SIZE_BYTES - 256 )); 
		uint32_t wavStartAddress = *wavStart;
		wavStart++; // advance to next 4-byte region
		numFiles = *wavStart; // get number of WAV files

		// If an invalid number of files, probably no valid sample UF2 was uploaded
		if (numFiles == 0 || numFiles > 256)
		{
			return -1;
		}
		
		// If start address is not in the right range, probably no valid sample UF2 was uploaded
		if (wavStartAddress < XIP_BASE || wavStartAddress >= XIP_BASE + PICO_FLASH_SIZE_BYTES)
		{
			return -1;
		}

		// If above tests pass, create list of wav files
		wavfiles = std::vector<WAVFile>(numFiles);

		// Loop through wav files, processing each in turn
		uint8_t *wavptr = ((uint8_t *)(wavStartAddress));
		for (unsigned i=0; i<numFiles; i++)
		{
			int res = wavfiles[i].Load(wavptr);
			if (res) // if this load failed, give up here
			{
				numFiles = i;
				return -2;
			}
			else
			{
				// Increment file start pointer to next wav file
				wavptr += wavfiles[i].FileSize();
			}
		}

		return 0; // success
	}
	
	virtual void ProcessSample()
	{
		////////////////////////////////////////////////////////////////////////////////
		// If the switch is held down for >2s, reboot into sample upload mode

		// count number of samples that switch is held down
		switchDownCount = (SwitchVal() == Down) ? switchDownCount + 1 : 0;

		// Light LEDs as switch is held
		LedOn(4, switchDownCount>0);
		LedOn(2, switchDownCount>32000);
		LedOn(0, switchDownCount>64000);

		// If we reach 2 seconds, exit from this ComputerCard and reboot into USB bootloader
		if (switchDownCount >= 96000)
		{
			Abort();
		}


		
		////////////////////////////////////////////////////////////////////////////////
		// If we failed to load any WAV files, turn on bottom right LED and 
		if (numFiles == 0)
		{
			LedOn(5);
			return;
		}
		

		////////////////////////////////////////////////////////////////////////////////
		// Play audio

		// Advance sample index to resample from original sample rate to 48kHz
		// Speed controlled by Knob Y + CV in 2
		// speed=1024 gives original playback speed
		uint32_t speed = std::max(0l, KnobVal(Y) + CVIn2());
		
		sampleIndex += (speed*wavfiles[currentFile].SampleRate())/(48000<<2);

		
		// If we go past the end of the file...
		if (sampleIndex >= (wavfiles[currentFile].NumSamples()<<8))
		{
			// Wrap the sample index
			sampleIndex -= wavfiles[currentFile].NumSamples()<<8;
    
			// At the end of a sample, if switch is up, advance through samples in turn
			if (SwitchVal() == Up)
			{
				currentFile++;
				if (currentFile >= numFiles)
				{
					currentFile = 0;
				}
			}
			else // If switch not up, select next sample with main knob + CV in 1
			{
				int32_t kb = KnobVal(Main) + CVIn1();
				if (kb < 0) kb = 0;
				if (kb > 4095) kb = 4095;
				
				currentFile = (numFiles*kb)>>12;
			}
		}

		// Look up interpolated sample
		int16_t sample = wavfiles[currentFile].Interp8(sampleIndex);

		sample >>= 4; // convert from 16-bit WAV to 12-bit DAC output

		// Output on both audio outputs
		AudioOut1(sample); 
		AudioOut2(sample);
	}

private:
	uint32_t sampleIndex;

	unsigned numFiles, currentFile;
	std::vector<WAVFile> wavfiles;

	int switchDownCount;
};


int main()
{
	set_sys_clock_khz(144000, true);

	SampleUpload su;
	su.Run();

	
	// We only get here if Abort() called in the SampleUpload ProcessSample().
	// Reboot into UF2 upload mode
	rom_reset_usb_boot(1<<11, 0); // pin 11 (top right LED) is USB activity
	return 0;
}

  
