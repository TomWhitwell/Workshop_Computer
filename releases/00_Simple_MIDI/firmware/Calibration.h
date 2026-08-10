#pragma once
#include "WebInterfaceComputerCard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

/*
  Calibration mode, activated by holding down the Z switch on power on

  In this mode the WS Computer just responds to messages sent by the web UI.
  The calibration logic is all within the web UI.

 */


// On a rising edge sample, GetDuration returns time since last rising edge in 256ths of a sample.
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


class Calibration : public WebInterfaceComputerCard
{
	volatile int32_t durationSum[2], durationCount[2];
	volatile int32_t rawSum[2], rawCount[2], rawSumCv[2], rawCountCv[2];
	volatile int32_t calMvSumAudio[2], calMvCountAudio[2];
	volatile int32_t calMvSumCv[2], calMvCountCv[2];
	volatile int32_t rawMaxAbs[2] = { 0, 0 };

	RisingEdgeCounter rec[2];

	bool eepromWriteReady = false;
	uint8_t eepromBuf[EEPROM_NUM_BYTES] = {};

	bool inputEepromWriteReady = false;
	uint8_t inputEepromBuf[EEPROM_INPUT_NUM_BYTES] = {};

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

	void flushToEEPROM(unsigned int startAddr, const uint8_t *buf, int total)
	{
		unsigned int eeAddr = startAddr;
		int remaining = total, offset = 0;
		while (remaining > 0)
		{
			int pageSize = 16 - (int)(eeAddr % 16);
			if (pageSize > remaining) pageSize = remaining;
			writePageToEEPROM(eeAddr, buf + offset, pageSize);
			eeAddr += pageSize;
			offset += pageSize;
			remaining -= pageSize;
		}
		uint8_t msg[] = { 'S', '|' };
		SendSysEx(msg, 2);
	}

	void CalibrationMIDICore()
	{
		static uint32_t lastTimingSend = 0;
		static uint32_t lastConnSend = 0;

		if (eepromWriteReady)
		{
			eepromWriteReady = false;
			flushToEEPROM(0, eepromBuf, EEPROM_NUM_BYTES);
		}

		if (inputEepromWriteReady)
		{
			inputEepromWriteReady = false;
			flushToEEPROM(EEPROM_INPUT_ADDR, inputEepromBuf, EEPROM_INPUT_NUM_BYTES);
		}

		uint32_t now = time_us_32();
		if (lastTimingSend == 0)
		{
			lastTimingSend = now;
			lastConnSend = now;
		}

		if (now - lastTimingSend >= 20000)
		{
			char buf[128];

			// Atomically read and zero a sum/count accumulator pair, returning the average.
			auto drain = [](volatile int32_t &sum, volatile int32_t &count) -> float {
				int32_t n = count; count = 0;
				int32_t s = sum;   sum   = 0;
				return n > 0 ? (float)s / (float)n : 0.0f;
			};

			float freq[2], a[2], cv[2], aMv[2], cvMv[2];
			for (int i = 0; i < 2; i++)
			{
				freq[i] = drain(durationSum[i],    durationCount[i]);
				a[i]    = drain(rawSum[i],          rawCount[i]);
				cv[i]   = drain(rawSumCv[i],        rawCountCv[i]);
				aMv[i]  = drain(calMvSumAudio[i],   calMvCountAudio[i]);
				cvMv[i] = drain(calMvSumCv[i],      calMvCountCv[i]);
			}
			int sig0 = rawMaxAbs[0] > 500 ? 1 : 0;
			int sig1 = rawMaxAbs[1] > 500 ? 1 : 0;
			rawMaxAbs[0] = 0;
			rawMaxAbs[1] = 0;
			int len = snprintf(buf, sizeof(buf), "D|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%d|%d|%.2f|%.2f|%.2f|%.2f",
			                   (double)freq[0], (double)freq[1],
			                   (double)a[0], (double)a[1],
			                   (double)cv[0], (double)cv[1],
			                   sig0, sig1,
			                   (double)aMv[0], (double)aMv[1],
			                   (double)cvMv[0], (double)cvMv[1]);
			SendSysEx((uint8_t *)buf, (uint32_t)len);

			lastTimingSend = now;
		}

		if (now - lastConnSend >= 100000)
		{
			char buf[16];
			int len = snprintf(buf, sizeof(buf), "K|%d|%d|%d|%d|",
			                   Connected(Audio1), Connected(Audio2), Connected(CV1), Connected(CV2));
			SendSysEx((uint8_t *)buf, (uint32_t)len);
			lastConnSend = now;
		}
	}

public:
	void __not_in_flash_func(ProcessSample)() override final
	{
		LedBrightness(0, 2000);
		LedBrightness(1, 4000);
		LedBrightness(2, 4000);
		LedBrightness(4, 2000);
		LedBrightness(5, 4000);

		for (int i = 0; i < 2; i++)
		{
			int32_t sample = AudioIn(i);
			int32_t filtered = sample;
			int32_t d = rec[i].GetDuration(filtered);
			if (d > 0)
			{
				int32_t freq = (48000 << 8) / d;
				if (freq > 15 && freq < 10000)
				{
					durationSum[i] += d;
					durationCount[i]++;
				}
			}

			int32_t absSample = sample < 0 ? -sample : sample;
			if (absSample > rawMaxAbs[i]) rawMaxAbs[i] = absSample;

			rawSum[i] += sample;
			rawCount[i]++;
			calMvSumAudio[i] += AudioInMillivolts(i);
			calMvCountAudio[i]++;

			rawSumCv[i] += CVIn(i);
			rawCountCv[i]++;
			calMvSumCv[i] += CVInMillivolts(i);
			calMvCountCv[i]++;
		}
	}

	void MIDICore() override
	{
		CalibrationMIDICore();
	}

	void ProcessIncomingSysEx(uint8_t *data, uint32_t size) override
	{
		if (size < 3) return;

		auto hexVal = [](uint8_t c) -> uint8_t {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			return 0;
		};

		// Decode hex pairs from data[2..] into dest (payload starts after the two-char command prefix).
		auto decodeHex = [&](uint8_t *dest, uint32_t count) {
			for (uint32_t i = 0; i < count; i++)
				dest[i] = (hexVal(data[2 + i * 2]) << 4) | hexVal(data[2 + i * 2 + 1]);
		};

		// Parse a decimal integer from the payload starting at data[offset].
		auto parseInt = [&](uint32_t offset) -> int {
			char buf[24];
			uint32_t len = size - offset < sizeof(buf) - 1 ? size - offset : sizeof(buf) - 1;
			memcpy(buf, data + offset, len);
			buf[len] = '\0';
			return atoi(buf);
		};

		// "E|<176 hex chars>|" - 88 bytes of CV out calibration data, write to EEPROM at offset 0
		if (data[0] == 'E' && data[1] == '|' && size >= EEPROM_NUM_BYTES * 2 + 3)
		{
			decodeHex(eepromBuf, EEPROM_NUM_BYTES);
			eepromWriteReady = true;
			return;
		}

		// "I|<76 hex chars>|" - 38 bytes of input calibration data, write to EEPROM at offset 88
		if (data[0] == 'I' && data[1] == '|' && size >= EEPROM_INPUT_NUM_BYTES * 2 + 3)
		{
			decodeHex(inputEepromBuf, EEPROM_INPUT_NUM_BYTES);
			inputEepromWriteReady = true;
			return;
		}
		
		// "M|<mv>|" - set CV out 1 to given millivolt value (used during input calibration sweep)
		if (data[0] == 'M' && data[1] == '|')
		{
			CVOut1Millivolts(parseInt(2));
			return;
		}

		// "M2|<mv>|" - set CV out 2 to given millivolt value
		if (size >= 5 && data[0] == 'M' && data[1] == '2' && data[2] == '|')
		{
			CVOut2Millivolts(parseInt(3));
			return;
		}


		// "C|<value>|" - set CV out 1 to raw DAC value (uncalibrated, for output calibration sweep)
		if (data[0] == 'C' && data[1] == '|')
		{
			CVOut1Precise(parseInt(2));
			return;
		}

		// "C2|<value>|" - set CV out 2 to raw DAC value
		if (size >= 5 && data[0] == 'C' && data[1] == '2' && data[2] == '|')
		{
			CVOut2Precise(parseInt(3));
			return;
		}
	}
};
