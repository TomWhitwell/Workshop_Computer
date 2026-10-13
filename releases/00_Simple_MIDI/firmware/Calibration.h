#pragma once
#include "WebInterfaceComputerCard.h"
#include "hardware/sync.h"
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
	// Measurement accumulators. Only ever modified by core 1 (ProcessSample).
	// Core 0 sets snapshotRequest; core 1 then copies acc to snapshot, zeroes acc
	// and clears snapshotRequest, so no accumulated sample is lost or double-counted.
	struct Accumulators
	{
		int32_t durationSum[2], durationCount[2];
		int32_t rawSum[2], rawSumCv[2];
		int32_t calMvSumAudio[2], calMvSumCv[2];
		int32_t rawMaxAbs[2];
		int32_t sampleCount;
	};
	Accumulators acc = {};
	Accumulators snapshot = {};
	volatile bool snapshotRequest = false;

	RisingEdgeCounter rec[2];

	bool eepromWriteReady = false;
	uint8_t eepromBuf[EEPROM_NUM_BYTES] = {};

	bool inputEepromWriteReady = false;
	uint8_t inputEepromBuf[EEPROM_INPUT_NUM_BYTES] = {};

	// Set by core 0 SysEx handler, consumed by CalibrationMIDICore: which regions to erase
	bool eepromClearReady = false;
	bool eepromClearCvOuts = false;
	bool eepromClearInputs = false;

	// Write up to one 16-byte page to EEPROM. Returns false on I2C failure or timeout.
	bool WritePageToEEPROM(unsigned int eeAddress, const uint8_t *data, int length)
	{
		if (length > 16)
		{
			length = 16;
		}
		uint8_t deviceAddress = EEPROM_PAGE_ADDRESS | ((eeAddress >> 8) & 0x0F);
		uint8_t data2[17];
		data2[0] = eeAddress & 0xFF;
		for (int i = 0; i < length; i++)
		{
			data2[i + 1] = data[i];
		}
		if (i2c_write_timeout_us(i2c0, deviceAddress, data2, length + 1, false, 10000) != length + 1)
		{
			return false;
		}

		// Acknowledge polling: EEPROM NAKs until its internal write cycle (typ. < 5 ms) completes
		absolute_time_t deadline = make_timeout_time_ms(20);
		uint8_t dummy;
		while (i2c_read_timeout_us(i2c0, deviceAddress, &dummy, 1, false, 1000) != 1)
		{
			if (time_reached(deadline))
			{
				return false;
			}
		}
		return true;
	}

	// Write a buffer to EEPROM, splitting at page boundaries. Returns false on failure.
	bool FlushToEEPROM(unsigned int startAddr, const uint8_t *buf, int total)
	{
		unsigned int eeAddr = startAddr;
		int remaining = total, offset = 0;
		while (remaining > 0)
		{
			int pageSize = 16 - (int)(eeAddr % 16);
			if (pageSize > remaining)
			{
				pageSize = remaining;
			}
			if (!WritePageToEEPROM(eeAddr, buf + offset, pageSize))
			{
				return false;
			}
			eeAddr += pageSize;
			offset += pageSize;
			remaining -= pageSize;
		}
		return true;
	}

	// Reply to the web UI after an EEPROM operation: "<op>|" on success, "F|<op>|" on failure
	void SendEEPROMResult(bool ok, char op)
	{
		if (ok)
		{
			uint8_t msg[] = { (uint8_t)op, '|' };
			SendSysEx(msg, 2);
		}
		else
		{
			uint8_t msg[] = { 'F', '|', (uint8_t)op, '|' };
			SendSysEx(msg, 4);
		}
	}

	void CalibrationMIDICore()
	{
		static uint32_t lastTimingSend = 0;
		static uint32_t lastConnSend = 0;

		if (eepromWriteReady)
		{
			eepromWriteReady = false;
			SendEEPROMResult(FlushToEEPROM(0, eepromBuf, EEPROM_NUM_BYTES), 'S');
		}

		if (inputEepromWriteReady)
		{
			inputEepromWriteReady = false;
			SendEEPROMResult(FlushToEEPROM(EEPROM_INPUT_ADDR, inputEepromBuf, EEPROM_INPUT_NUM_BYTES), 'S');
		}

		// Erase the selected calibration regions (0xFF = erased EEPROM state).
		// Invalid magic numbers mean default calibration is used on next power-up.
		if (eepromClearReady)
		{
			eepromClearReady = false;
			uint8_t blank[EEPROM_NUM_BYTES > EEPROM_INPUT_NUM_BYTES ? EEPROM_NUM_BYTES : EEPROM_INPUT_NUM_BYTES];
			memset(blank, 0xFF, sizeof(blank));
			bool ok = true;
			if (eepromClearCvOuts)
			{
				ok = FlushToEEPROM(0, blank, EEPROM_NUM_BYTES) && ok;
			}
			if (eepromClearInputs)
			{
				ok = FlushToEEPROM(EEPROM_INPUT_ADDR, blank, EEPROM_INPUT_NUM_BYTES) && ok;
			}
			SendEEPROMResult(ok, 'X');
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

			// Ask core 1 to snapshot and zero the accumulators; it does so within one sample period
			snapshotRequest = true;
			while (snapshotRequest)
			{
			}
			__dmb();
			const Accumulators &sn = snapshot;

			auto average = [](int32_t sum, int32_t count) -> float {
				return count > 0 ? (float)sum / (float)count : 0.0f;
			};

			float freq[2], a[2], cv[2], aMv[2], cvMv[2];
			for (int i = 0; i < 2; i++)
			{
				freq[i] = average(sn.durationSum[i],   sn.durationCount[i]);
				a[i]    = average(sn.rawSum[i],        sn.sampleCount);
				cv[i]   = average(sn.rawSumCv[i],      sn.sampleCount);
				aMv[i]  = average(sn.calMvSumAudio[i], sn.sampleCount);
				cvMv[i] = average(sn.calMvSumCv[i],    sn.sampleCount);
			}
			int sig0 = sn.rawMaxAbs[0] > 500 ? 1 : 0;
			int sig1 = sn.rawMaxAbs[1] > 500 ? 1 : 0;
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
			// Jack connections, then whether CV out / input calibration was loaded from EEPROM at power-up
			char buf[24];
			int len = snprintf(buf, sizeof(buf), "K|%d|%d|%d|%d|%d|%d|",
			                   Connected(Audio1), Connected(Audio2), Connected(CV1), Connected(CV2),
			                   CVOutsCalibrated(), InputsCalibrated());
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
			int32_t d = rec[i].GetDuration(sample);
			if (d > 0)
			{
				int32_t freq = (48000 << 8) / d;
				if (freq > 15 && freq < 10000)
				{
					acc.durationSum[i] += d;
					acc.durationCount[i]++;
				}
			}

			int32_t absSample = sample < 0 ? -sample : sample;
			if (absSample > acc.rawMaxAbs[i])
			{
				acc.rawMaxAbs[i] = absSample;
			}

			acc.rawSum[i] += sample;
			acc.calMvSumAudio[i] += AudioInMillivolts(i);
			acc.rawSumCv[i] += CVIn(i);
			acc.calMvSumCv[i] += CVInMillivolts(i);
		}
		acc.sampleCount++;

		if (snapshotRequest)
		{
			snapshot = acc;
			acc = {};
			__dmb();
			snapshotRequest = false;
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

		// "X|<target>|" - clear calibration data from EEPROM.
		// target: 'C' = CV outs only, 'I' = inputs (audio and CV) only, anything else = both
		if (data[0] == 'X' && data[1] == '|')
		{
			uint8_t target = size >= 4 ? data[2] : 'A';
			eepromClearCvOuts = (target != 'I');
			eepromClearInputs = (target != 'C');
			eepromClearReady = true;
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
