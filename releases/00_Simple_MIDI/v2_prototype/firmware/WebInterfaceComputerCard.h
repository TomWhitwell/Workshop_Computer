#ifndef WEBINTERFACECOMPUTERCARD_H
#define WEBINTERFACECOMPUTERCARD_H

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "usb_midi_host.h"


////////////////////////////////////////////////////////////////////////////////
// Class to add MIDI SysEx connectivity to ComputerCard
//
// Used essentially verbatim from web_interface/main.cpp, with host mode added.
// Changes from original: sysexBufSize increased to 4096 for large patch strings;
// tud_midi_stream_read size argument corrected from sizeof(rxBuf) to rxBufSize;
// host mode support added using midi_device_host example as reference.

class WebInterfaceComputerCard : public ComputerCard
{
public:
	struct MIDIMessage
	{
		uint8_t status, data1, data2;
	};

	// Incoming SPSC ring buffer - designed for potential reading from ProcessSample
	static constexpr unsigned midiQueueSize = 32;
	MIDIMessage midiQueue[midiQueueSize];
	volatile uint32_t midiQueueWrite = 0;
	volatile uint32_t midiQueueRead = 0;


	// True when acting as USB MIDI host (DFP port); false for device mode (UFP/Unsupported)
	bool isUSBMIDIHost = false;
	// Device address of the connected MIDI device when in host mode (0 = none)
	uint8_t midiDevAddr = 0;

	// Return the singleton instance cast to this type (for use by C callbacks)
	static WebInterfaceComputerCard *GetInstance()
	{
		return (WebInterfaceComputerCard *)ThisPtr();
	}


	WebInterfaceComputerCard()
	{
		sysexLen = 0;
		sysexActive = false;
		sysexBuf = nullptr;
	}


	// Detect host/device mode from USB power state and set isUSBMIDIHost.
	// DFP (downstream-facing port) means we are the host.
	// UFP or Unsupported means we are a device.
	void SetDeviceHostMode()
	{
		isUSBMIDIHost = (USBPowerState() == DFP);
	}

	// Call to send (potentially large amounts of) data over MIDI.
	// Blocks until all data has been queued for sending.
	void MIDIStreamWriteBlocking(uint8_t cable, uint8_t const *data, uint32_t size)
	{
		uint32_t sent = 0;
		while (sent < size)
		{
			uint32_t n;
			if (isUSBMIDIHost)
			{
				n = (midiDevAddr != 0);
				if (n)
				{
					n = tuh_midi_stream_write(midiDevAddr, cable, data + sent, size - sent);
				}
			}
			else
			{
				n = tud_midi_stream_write(cable, data + sent, size - sent);
			}
			sent += n;

			if (!n)
			{
				if (isUSBMIDIHost)
					tuh_task();
				else
					tud_task();
			}
		}
	}

	// Send a SysEx message of arbitrary length
	void SendSysEx(const uint8_t *data, uint32_t size)
	{
#ifdef WICC_NO_HOST_SYSEX
		if (isUSBMIDIHost) return;
#endif
		uint8_t header[] = { 0xF0, MIDI_MANUFACTURER_ID };
		uint8_t footer[] = { 0xF7 };
		MIDIStreamWriteBlocking(0, header, 2);
		MIDIStreamWriteBlocking(0, data, size);
		MIDIStreamWriteBlocking(0, footer, 1);
		// In host mode, explicitly flush buffered TX data to the device
		if (isUSBMIDIHost && midiDevAddr != 0)
			tuh_midi_stream_flush(midiDevAddr);
	}

	// Code for second RP2040 core. Blocking.
	// Listens for SysEx messages over MIDI.
	void USBCore()
	{
		sysexBuf = new uint8_t[sysexBufSize];

		sysexActive = false;
		sysexLen = 0;

		// Wait for USB power state to settle, then choose host or device mode
		sleep_us(150000);
		SetDeviceHostMode();

		if (isUSBMIDIHost)
		{
			tuh_init(TUH_OPT_RHPORT);
		}
		else
		{
			tud_init(TUD_OPT_RHPORT);
		}

		// This loop waits for and processes MIDI messages
		while (1)
		{
			if (isUSBMIDIHost)
			{
				// tuh_task() triggers tuh_midi_rx_cb() -> ParseMIDIBytes() for incoming data
				tuh_task();
				// Flush any outgoing MIDI queued
				if (midiDevAddr != 0 && tuh_midi_configured(midiDevAddr))
					tuh_midi_stream_flush(midiDevAddr);
			}
			else
			{
				tud_task();
				while (tud_midi_available())
				{
					uint32_t bytesReceived = tud_midi_stream_read(rxBuf, rxBufSize);
					if (bytesReceived > 0)
					{
						ParseMIDIBytes(rxBuf, bytesReceived);
					}
				}
			}

			MIDICore();
		}
	}

	// Parse MIDI channel messages and SysEx out of a MIDI byte stream.
	// Channel messages (note on/off, CC) are queued
	// SysEx with our manufacturer ID is passed to ProcessIncomingSysEx.
	void ParseMIDIBytes(uint8_t *buf, uint32_t bytesReceived)
	{
		for (uint32_t i = 0; i < bytesReceived; i++)
		{
			uint8_t b = buf[i];

			if (b == 0xF0)
			{
				sysexActive = true;
				sysexLen = 0;
				midiDataCount = 0;
				sysexBuf[sysexLen++] = b;
			}
			else if (sysexActive)
			{
				if (sysexLen < sysexBufSize)
				{
					sysexBuf[sysexLen++] = b;
				}
				if (b == 0xF7)
				{
#ifdef WICC_NO_HOST_SYSEX
					if (!isUSBMIDIHost)
#endif
						if (sysexBuf[1] == MIDI_MANUFACTURER_ID && sysexLen >= 3)
						{
							// Chop off 0xF0, single-byte manufacturer ID, and 0xF7 terminator
							ProcessIncomingSysEx(sysexBuf + 2, sysexLen - 3);
						}
					sysexActive = false;
					sysexLen = 0;
				}
			}
			else if (b >= 0x80 && b < 0xF0)
			{
				// Channel status byte - set running status
				midiRunningStatus = b;
				midiDataCount = 0;
				uint8_t type = b & 0xF0;
				// Program change (0xC0) and channel pressure (0xD0) have 1 data byte; others have 2
				midiDataNeeded = (type == 0xC0 || type == 0xD0) ? 1 : 2;
			}
			else if (b < 0x80 && midiRunningStatus)
			{
				// Data byte for current running status
				midiDataBuf[midiDataCount++] = b;
				if (midiDataCount >= midiDataNeeded)
				{
					uint8_t type = midiRunningStatus & 0xF0;
					// Queue note-off (0x80), note-on (0x90), CC (0xB0), pitchbend (0xE0)
					if (type == 0x80 || type == 0x90 || type == 0xB0 || type == 0xE0)
					{
						uint32_t next = (midiQueueWrite + 1) % midiQueueSize;
						if (next != midiQueueRead) // drop if full
						{
							midiQueue[midiQueueWrite] = { midiRunningStatus, midiDataBuf[0],
								                          midiDataNeeded > 1 ? midiDataBuf[1] : uint8_t(0) };
							midiQueueWrite = next;
						}
					}
					midiDataCount = 0;
				}
			}
		}
	}

	// New virtual function, overridden in specific class
	virtual void MIDICore() {}

	// New virtual function, overridden in specific class
	virtual void ProcessIncomingSysEx(uint8_t *, uint32_t) {} // data, size

	// Host mode callbacks - called from the tuh_midi_*_cb free functions below
	void OnMidiHostMount(uint8_t dev_addr)
	{
		if (midiDevAddr == 0)
		{
			midiDevAddr = dev_addr;
		}
	}

	void OnMidiHostUmount(uint8_t dev_addr)
	{
		if (dev_addr == midiDevAddr)
			midiDevAddr = 0;
	}

	void OnMidiHostRx(uint8_t dev_addr, uint32_t num_packets)
	{
		if (dev_addr != midiDevAddr || num_packets == 0) return;
		uint8_t cable_num;
		while (true)
		{
			uint32_t n = tuh_midi_stream_read(dev_addr, &cable_num, rxBuf, rxBufSize);
			if (n == 0) break;
			ParseMIDIBytes(rxBuf, n);
		}
	}

	uint8_t *sysexBuf = nullptr;

private:
	static constexpr unsigned sysexBufSize = 4096; // large enough for complex patch strings
	static constexpr uint8_t MIDI_MANUFACTURER_ID = 0x7D; // prototyping/private use
	static constexpr unsigned rxBufSize = 64;
	bool sysexActive;
	unsigned sysexLen;
	uint8_t rxBuf[rxBufSize];

	// State for parsing MIDI channel messages (running status)
	uint8_t midiRunningStatus = 0;
	uint8_t midiDataBuf[2] = {};
	uint8_t midiDataCount = 0;
	uint8_t midiDataNeeded = 0;
};


// USB MIDI host callbacks - required by usb_midi_host driver, called from tuh_task()

extern "C"
{

	void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
	                       uint8_t num_cables_rx, uint16_t num_cables_tx)
	{
		(void)in_ep;
		(void)out_ep;
		(void)num_cables_rx;
		(void)num_cables_tx;
		WebInterfaceComputerCard::GetInstance()->OnMidiHostMount(dev_addr);
	}

	void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
	{
		(void)instance;
		WebInterfaceComputerCard::GetInstance()->OnMidiHostUmount(dev_addr);
	}

	void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
	{
		WebInterfaceComputerCard::GetInstance()->OnMidiHostRx(dev_addr, num_packets);
	}

	void tuh_midi_tx_cb(uint8_t dev_addr)
	{
		(void)dev_addr;
	}

} // extern "C"


#endif
