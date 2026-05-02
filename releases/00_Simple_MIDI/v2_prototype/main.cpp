#include "WebInterfaceComputerCard.h"
#include "NoteDownStore.h"
#include "AnalogueToMIDI.h"

#include "hardware/vreg.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

////////////////////////////////////////////////////////////////////////////////
// Frequency counter
//
// On a rising edge sample, GetDuration returns time since last rising edge in 256ths of a sample
// On a non-rising-edge sample, GetDuration returns 0.
class RisingEdgeCounter
{
	volatile int32_t time;
	int32_t lastSample, lastRisingEdge, lastRisingEdgeSubsample;
	int32_t numberWithinDurationWindow;

public:
	RisingEdgeCounter()
	{
		numberWithinDurationWindow = 0;
		time = 0;
		lastSample = 1;
		lastRisingEdge = 0;
		lastRisingEdgeSubsample = 0;
	}

	int32_t GetDuration(int32_t sample)
	{
		int32_t retval = 0;
		if (sample > 0 && lastSample <= 0)
		{
			int32_t risingEdge = time - 1;
			int32_t risingEdgeSubsample = (256 * lastSample) / (lastSample - sample);

			int32_t duration256 = 256 * (risingEdge - lastRisingEdge) + (risingEdgeSubsample - lastRisingEdgeSubsample);

			retval = duration256;

			lastRisingEdge = risingEdge;
			lastRisingEdgeSubsample = risingEdgeSubsample;
		}
		lastSample = sample;

		time++;
		return retval;
	}

};


class SimpleMIDI : public WebInterfaceComputerCard
{

public:
	SimpleMIDI()
	{
	}

	// Shared state for MIDI interface mode (written by MIDICore, read by ProcessSample)
	volatile int16_t midiAudioOut[2] = { 0, 0 }; // driven by CC42
	volatile uint8_t lastPlayedNote[2] = { 60, 60 }; // last active/played MIDI note
	volatile bool midiGate[2] = { false, false }; // driven by note on/off
	volatile int16_t pitchbend[2] = { 0, 0 }; // -8192 to +8191, ±2 semitones
	volatile int16_t ccInputs[8] = {}; // CC34–41 source values, sampled safely on Core 1

	NoteDownStore nds[2];

	// AnalogueToMIDI processors for CC34–41 outputs
	// Knobs 0-4095, switch 0-127, audio/CV in -2048 to 2047
	AnalogueToMIDI ccProcessors[8] = {
		{0, 4095}, {0, 4095}, {0, 4095}, {0, 127},
		{-2048, 2047}, {-2048, 2047}, {-2048, 2047}, {-2048, 2047}
	};

	volatile bool modeDetected = false;
	volatile bool calibrationMode = false;

	////////////////////////////////////////////////////////////////////////////////
	// AUDIO CORE
	////////////////////////////////////////////////////////////////////////////////

	void __not_in_flash_func(ProcessSample)() override final
	{
		if (!modeDetected) return;

		if (calibrationMode)
		{
			for (int i = 0; i < 2; i++) // for each channel
			{
				// Collect audio samples and set up average duration
				int32_t sample = AudioIn(i);
				int32_t filtered = sample; // bp[i].Process(sample >> 2);
				int32_t d = rec[i].GetDuration(filtered);
				if (d > 0)
				{
					// evaluate frequency
					int32_t freq = (48000 << 8) / d;

					// Only process if frequency is 15Hz-10kHz
					// (range C0 - C9, where C4 is middle C)
					if (freq > 15 && freq < (10000))
					{
						durationSum[i] += d;
						durationCount[i]++;
					}
				}

				// Track peak absolute value for signal-presence detection
				int32_t absSample = sample < 0 ? -sample : sample;
				if (absSample > rawMaxAbs[i]) rawMaxAbs[i] = absSample;

				// Accumulate raw inputs, used for CV in calibration
				rawSum[i] += sample;
				rawCount[i]++;

				rawSumCv[i] += CVIn(i);
				rawCountCv[i]++;
			}
		}
		else
		{
			for (int i = 0; i < 2; i++)
			{
				AudioOut(i, midiAudioOut[i]);
				LedBrightness(i, midiAudioOut[i] << 1);
				// Apply pitch bend: ±2 semitones, pitchbend ±8192 -> subNote ±512 (1/256 semitone units)
				int32_t currentPitch = ((int32_t)lastPlayedNote[i] << 8) + (pitchbend[i] >> 4);
				if (currentPitch < 0) currentPitch = 0;
				if (currentPitch > (127 << 8)) currentPitch = 127 << 8;
				CVOutMIDINote8(i, (uint8_t)(currentPitch >> 8), currentPitch & 0xFF);
				LedBrightness(2 + i, lastPlayedNote[i] << 5);
				PulseOut(i, midiGate[i]);
				LedOn(4 + i, midiGate[i]);
			}

			// Sample all CC source values here on Core 1, after disconnection zeroing has been applied
			ccInputs[0] = (int16_t)KnobVal(Main);
			ccInputs[1] = (int16_t)KnobVal(X);
			ccInputs[2] = (int16_t)KnobVal(Y);
			Switch s = SwitchVal();
			ccInputs[3] = (int16_t)(s == Switch::Down ? 0 : s == Switch::Middle ? 64 : 127);
			ccInputs[4] = AudioIn1();
			ccInputs[5] = AudioIn2();
			ccInputs[6] = CVIn1();
			ccInputs[7] = CVIn2();
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	// Non-audio core
	////////////////////////////////////////////////////////////////////////////////

	// Called from MIDI core (Core 0) continuously.
	void MIDICore() override
	{
		if (!modeDetected)
		{
			// Detect mode here, after the 150ms USB settling delay, so ADC readings
			// are stable and SwitchVal() reflects the actual switch position.
			calibrationMode = (SwitchVal() == Switch::Down);
			modeDetected = true;

			if (calibrationMode)
			{
				// make a 'C' for calibration
				LedBrightness(0, 2000);
				LedBrightness(1, 1500);
				LedBrightness(2, 2000);
				LedBrightness(4, 1500);
				LedBrightness(5, 2000);
			}
		}

		if (calibrationMode)
		{
			CalibrationMIDICore();
		}
		else
		{
			MIDIInterfaceCore();
		}
	}

	// Calibration mode: sends timing and connection data back to the browser.
	void CalibrationMIDICore()
	{
		static uint32_t lastTimingSend = 0;
		static uint32_t lastConnSend = 0;


		// Write calibration to EEPROM if requested via SysEx
		if (eepromWriteReady)
		{
			eepromWriteReady = false;
			unsigned int eeAddr = 0;
			int toWrite = 88, bufOff = 0;
			while (toWrite > 0)
			{
				int pageSize = 16 - (int)(eeAddr % 16);
				if (pageSize > toWrite) pageSize = toWrite;
				writePageToEEPROM(eeAddr, &eepromBuf[bufOff], pageSize);
				eeAddr += pageSize;
				bufOff += pageSize;
				toWrite -= pageSize;
			}
			// Confirm to browser
			uint8_t msg[] = { 'S', '|' };
			SendSysEx(msg, 2);
		}

		uint32_t now = time_us_32();
		if (lastTimingSend == 0)
		{
			lastTimingSend = now;
			lastConnSend = now;
		}

		// Send frequency measurements and raw ADC readings every 20 ms
		if (now - lastTimingSend >= 20000)
		{
			char buf[128];

			// Calculate frequencies for audio channels and averaged raw audio and cv data,
			// and send with 'D' message
			float freq[2] = { 0.0f, 0.0f }, a[2], cv[2];

			for (int i = 0; i < 2; i++)
			{
				int32_t count = durationCount[i];
				durationCount[i] = 0;
				int32_t sum = durationSum[i];
				durationSum[i] = 0;
				if (count > 0)
				{
					freq[i] = (float)sum / (float)count;
				}

				int32_t n = rawCount[i];
				rawCount[i] = 0;
				int32_t s = rawSum[i];
				rawSum[i] = 0;
				int32_t ncv = rawCountCv[i];
				rawCountCv[i] = 0;
				int32_t scv = rawSumCv[i];
				rawSumCv[i] = 0;

				a[i] = (n > 0) ? ((float)s / (float)n) : 0.0f;
				cv[i] = (ncv > 0) ? ((float)scv / (float)ncv) : 0.0f;
			}
			int sig0 = rawMaxAbs[0] > 500 ? 1 : 0;
			int sig1 = rawMaxAbs[1] > 500 ? 1 : 0;
			rawMaxAbs[0] = 0;
			rawMaxAbs[1] = 0;
			int len = snprintf(buf, sizeof(buf), "D|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%d|%d",
			                   (double)freq[0], (double)freq[1],
			                   (double)a[0], (double)a[1],
			                   (double)cv[0], (double)cv[1],
			                   sig0, sig1);
			SendSysEx((uint8_t *)buf, (uint32_t)len);

			lastTimingSend = now;
		}

		// Send jack connection status every 100 ms
		if (now - lastConnSend >= 100000)
		{
			char buf[16];
			int len = snprintf(buf, sizeof(buf), "K|%d|%d|%d|%d|",
			                   Connected(Audio1), Connected(Audio2), Connected(CV1), Connected(CV2));
			SendSysEx((uint8_t *)buf, (uint32_t)len);
			lastConnSend = now;
		}
	}

	// MIDI interface mode: receive note, pitchbend and CC -> CV/audio/gate outputs;
	// send knob/switch/input values as CC34–41.
	void MIDIInterfaceCore()
	{
		// Receive incoming MIDI
		while (midiQueueRead != midiQueueWrite)
		{
			MIDIMessage msg = midiQueue[midiQueueRead];
			midiQueueRead = (midiQueueRead + 1) % midiQueueSize;

			uint8_t type = msg.status & 0xF0;
			uint8_t ch = msg.status & 0x0F; // 0 = MIDI ch1, 1 = MIDI ch2

			if (ch < 2)
			{
				if (type == 0x90 && msg.data2 > 0) // note on
				{
					nds[ch].NoteOn(msg.data1, msg.data2);
					lastPlayedNote[ch] = msg.data1;
					midiGate[ch] = true;
				}
				else if (type == 0x80 || (type == 0x90 && msg.data2 == 0)) // note off
				{
					nds[ch].NoteOff(msg.data1);
					int8_t lnd = nds[ch].LastNoteDown();
					if (lnd >= 0)
					{
						lastPlayedNote[ch] = lnd; // resume previously held note
					}
					else
					{
						midiGate[ch] = false;
					}
				}
				else if (type == 0xB0 && msg.data1 == 42) // CC42 -> audio out
				{
					midiAudioOut[ch] = (int16_t)(msg.data2 * 16);
				}
				else if (type == 0xE0) // pitch bend
				{
					pitchbend[ch] = (int16_t)((msg.data1 | ((uint16_t)msg.data2 << 7)) - 8192);
				}
			}
		}

		// --- Send CC34–41 at 500 Hz polling
		static uint32_t lastSendUs = 0;
		static constexpr uint8_t ccNums[8] = { 34, 35, 36, 37, 38, 39, 40, 41 };

		uint32_t now = time_us_32();
		if (now - lastSendUs >= 2000)
		{
			lastSendUs = now;

			for (int i = 0; i < 8; i++)
				ccProcessors[i](ccInputs[i]);

			for (int i = 0; i < 8; i++)
			{
				int8_t v = ccProcessors[i].GetMIDIValueIfNew();
				if (v >= 0)
				{
					uint8_t out[3] = { 0xB0, ccNums[i], (uint8_t)v };
					MIDIStreamWriteBlocking(0, out, 3);
				}
			}
		}
	}


	void writePageToEEPROM(unsigned int eeAddress, const uint8_t *data, int length)
	{
		if (length > 16) length = 16;
		uint8_t deviceAddress = EEPROM_PAGE_ADDRESS | ((eeAddress >> 8) & 0x0F);
		uint8_t data2[17];
		data2[0] = eeAddress & 0xFF;
		for (int i = 0; i < length; i++) data2[i + 1] = data[i];
		i2c_write_blocking(i2c0, deviceAddress, data2, length + 1, false);
		uint8_t dummy;
		while (i2c_read_blocking(i2c0, deviceAddress, &dummy, 1, false) != 1) {}
	}

	void ProcessIncomingSysEx(uint8_t *data, uint32_t size) override
	{
		if (!calibrationMode) return;
		if (size < 3) return;

		// "E|<176 hex chars>|" - write calibration to EEPROM
		if (data[0] == 'E' && data[1] == '|' && size >= 179)
		{
			auto hexVal = [](uint8_t c) -> uint8_t
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				return 0;
			};
			for (uint32_t i = 0; i < 88; i++)
				eepromBuf[i] = (uint8_t)((hexVal(data[2 + i * 2]) << 4) | hexVal(data[2 + i * 2 + 1]));
			eepromWriteReady = true;
			return;
		}

		// "C|<value>|" - set CVOut1Precise
		if (data[0] == 'C' && data[1] == '|')
		{
			char buf[24];
			uint32_t len = size - 2 < sizeof(buf) - 1 ? size - 2 : sizeof(buf) - 1;
			memcpy(buf, data + 2, len);
			buf[len] = '\0';
			CVOut1Precise(atoi(buf));
			return;
		}

		// "C2|<value>|" - set CVOut2Precise
		if (size >= 5 && data[0] == 'C' && data[1] == '2' && data[2] == '|')
		{
			char buf[24];
			uint32_t len = size - 3 < sizeof(buf) - 1 ? size - 3 : sizeof(buf) - 1;
			memcpy(buf, data + 3, len);
			buf[len] = '\0';
			CVOut2Precise(atoi(buf));
			return;
		}
	}

	volatile int32_t durationSum[2], durationCount[2];
	volatile int32_t rawSum[2], rawCount[2], rawSumCv[2], rawCountCv[2];
	volatile int32_t rawMaxAbs[2] = { 0, 0 }; // peak |sample| in current D-message window

	RisingEdgeCounter rec[2];

	bool eepromWriteReady = false;
	uint8_t eepromBuf[88] = {};
};



////////////////////////////////////////////////////////////////////////////////



SimpleMIDI *smPtr = nullptr;
static void core1()
{
	smPtr->Run();
}

int main()
{
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	set_sys_clock_khz(192000, true);


	static SimpleMIDI sm;
	smPtr = &sm;
	sm.EnableNormalisationProbe();
	multicore_launch_core1(core1);
	sm.USBCore();
}
