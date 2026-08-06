#include "ComputerCard.h"

#include "pico/multicore.h"
#include "tusb.h"
#include "usb_midi_host.h"


/*
  
   ComputerCard USB MIDI host and device example

   This example demonstrates sending and receiving MIDI messages,
   with the Workshop System Computer acting as either a USB MIDI Device,
   or a USB MIDI host.

   On boards made before 2025 (Proto 1 to proto 2.0.2; production version 1.0),
   only MIDI device mode is supported.

   This example is extremely minimal and primarily demonstrates the process for
   detecting the USB power state (downstream facing or upstream facing port) and
   setting up TinyUSB in Host or Device more accordingly.

   Only the sending of MIDI messages is demonstrated here. 
   The midi_device and midi_host examples demonstrate receiving MIDI messages.

   This approach does not cope with long messages (e.g. SysEx), or, potentially, with
   very high message rates when tud_midi_stream_write doesn't write (all) bytes.
   See web_interface demo for a way of resolving this.
 */

class MIDIDeviceHost : public ComputerCard
{
public:
	MIDIDeviceHost()
	{
		noteOnNext = true;
		isUSBMIDIHost = false;
		
		midi_dev_addr = 0;
		device_connected = 0;
		
		counter = 0;
		powerState = Unsupported;
		
		// Start the second core
		multicore_launch_core1(core1);
	}

	// Boilerplate to call member function as second core
	static void core1()
	{
		((MIDIDeviceHost *)ThisPtr())->USBCore();
	}


	// Send alternate note-on and note-off messages
	void SendNextNote()
	{
		static uint8_t noteOn[3] = {0x90, 0x5f, 0x7f};
		static uint8_t noteOff[3] = {0x80, 0x5f, 0x00};


		uint8_t *noteBuf = noteOnNext ? noteOn : noteOff;
		int noteBufSize = 3;

		// Send message, either as MIDI host (tuh_...) or MIDI device (tud_...)
		if (isUSBMIDIHost)
		{
			// Transmit the note message on the highest cable number
			uint8_t cable = tuh_midih_get_num_tx_cables(midi_dev_addr) - 1;
			tuh_midi_stream_write(midi_dev_addr, cable, noteBuf, noteBufSize);
		}
		else
		{
			tud_midi_stream_write(0, noteBuf, noteBufSize);
		}

		// Toggle between note on / note off
		noteOnNext = !noteOnNext;
	}

	void SetDeviceHostMode()
	{
		powerState = USBPowerState();

		// On reboot, choose between USB device or USB host mode
		// Support device mode only on 2024 boards with unsupported power state
		// Note that the USB power circuitry may only establish the right port state
		// when power is first appled to the WS; a reset of the RP2040 alone may not
		// be sufficient to switch the correct state.
		if (powerState == UFP || powerState == Unsupported)
		{
			isUSBMIDIHost = false;
		}
		else if (powerState == DFP)
		{
			isUSBMIDIHost = true;
		}

	}
	
	// Code for second RP2040 core, blocking
	// Handles MIDI in/out messages
	void USBCore()
	{
		uint8_t packet[32];

		// Wait 150ms for USB to decide which power state it is in
		sleep_us(150000); 
		// Set USB device/host flag from USB power state
		SetDeviceHostMode();

		// Setup TinyUSB in appropriate host/device mode
		if (isUSBMIDIHost)
		{
			tuh_init(TUH_OPT_RHPORT);
		}
		else
		{
			tud_init(TUD_OPT_RHPORT);
		}
			
		// Now the MIDI processing loop, here split into two parts for host/device.
		while (1)
		{
			if (!isUSBMIDIHost) // Computer is USB device
			{
				tud_task();
				while (tud_midi_available())
				{
					// Read and discard MIDI input
					tud_midi_stream_read(packet, sizeof(packet));

					// Use code from midi_device example here to process MIDI input
				}
				
				if (counter >= 20000)
				{
					SendNextNote();
					counter -= 20000;
				}
			}
			else  // Computer is USB host
			{
				tuh_task();
		
				bool connected = midi_dev_addr != 0 && tuh_midi_configured(midi_dev_addr);

				// device must be attached and have at least one endpoint ready to receive a message
				if (connected && tuh_midih_get_num_tx_cables(midi_dev_addr) >= 1)
				{

					if (counter >= 20000)
					{
						SendNextNote();
						counter -= 20000;
					}

					// Send a USB packet immediately (even though in this case,
					// we are not close to the 64-byte maximum payload)
					tuh_midi_stream_flush(midi_dev_addr);
				}
			}
		}

	}

	
	// 48kHz audio processing function
	virtual void ProcessSample()
	{

		// Top left LED on if we can't determine USB power state automatically
		// (and so will be in USB device mode)
		LedOn(0, powerState == Unsupported);

		// Flash LED 4 (bottom left) if acting as MIDI Host (DFP)
		// Flash LED 5 (bottom right) if acting as MIDI Device (UFP)
		int activeLED = (powerState==DFP)?4:5;
		int inactiveLED = (powerState==DFP)?5:4;
		LedOn(activeLED, !noteOnNext);
		LedOff(inactiveLED);


		// Middle two LEDs show 'live' USB power state
		// If the LED lit here is not in the same column as the flashing LED
		// in the bottom row, then the host/device mode and USB power DFP/UFP
		// are not aligned, and the RP2040 needs to be reset.
		LedOn(2, USBPowerState()==DFP);
		LedOn(3, USBPowerState()==UFP);
		
		// Counter increment here, but reset by other core
		if (counter <= 30000)
		{
			counter++;
		}
	}

	
	static uint8_t device_connected;
	static uint8_t midi_dev_addr;
	
private:
	volatile bool noteOnNext;
	
	volatile uint32_t counter;

	// powerState and isUSBMIDIHost have similar roles here
	// We keep powerState as a class member only so that we can indicate
	// when the board is unsupported
	volatile USBPowerState_t powerState;
	bool isUSBMIDIHost;
};

uint8_t MIDIDeviceHost::device_connected;
uint8_t MIDIDeviceHost::midi_dev_addr;


// Four callback functions that rppicomidi/usb_midi_host uses

// Mount USB host callback
void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
{
	(void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx; // avoid unused variable warnings

	
	if (MIDIDeviceHost::midi_dev_addr == 0)
	{
		MIDIDeviceHost::midi_dev_addr = dev_addr;
		MIDIDeviceHost::device_connected = 1;
	}
}

// Unmount USB host callback
void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)instance;
	
	if (dev_addr == MIDIDeviceHost::midi_dev_addr)
	{
		MIDIDeviceHost::midi_dev_addr = 0;
		MIDIDeviceHost::device_connected = 0;
	}
}

// USB host MIDI data received callback
void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
	if (MIDIDeviceHost::midi_dev_addr != dev_addr)
		return;

	if (num_packets == 0)
		return;
	
	uint8_t cable_num;
	uint8_t buffer[48];
	while (1)
	{
		// Read and discard MIDI data received as USB MIDI host
		// See midi_host example for how to process this
		int32_t bytesToProcess = tuh_midi_stream_read(dev_addr, &cable_num, buffer, sizeof(buffer));

		if (bytesToProcess == 0)
		{
			break;
		}
	}

}

// USB hist MIDI data sent callback (unused)
void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}






int main()
{
	set_sys_clock_khz(144000, true);

	MIDIDeviceHost mdh;
	mdh.Run();
}

  
