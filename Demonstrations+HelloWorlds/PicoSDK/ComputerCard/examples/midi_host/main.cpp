// USB host MIDI example
// using https://github.com/rppicomidi/usb_midi_host
//
// USB host means that the MTM Computer connects to (and may provide power to) USB devices
// such as MIDI controllers/keyboards, the 8mu, etc.

// To connect with USB to a laptop/desktop computer (which itself acts as a USB host),
// see the usb_device example.


// This is a very slightly modified 

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_midi_host.h"




class MIDIHost : public ComputerCard
{
public:
	MIDIHost()
	{
		midi_dev_addr = 0;
		device_connected = 0;
		
		counter = 0;
		
		// Start the second core
		multicore_launch_core1(core1);
	}

	// Boilerplate to call member function as second core
	static void core1()
	{
		((MIDIHost *)ThisPtr())->USBCore();
	}

	// Send alternate note-on and note-off messages
	void SendNextNote()
	{
		static uint8_t noteOn[3] = {0x90, 0x5f, 0x7f};
		static uint8_t noteOff[3] = {0x80, 0x5f, 0x00};
		static bool noteOnNext = true;

		// Transmit the note message on the highest cable number
		uint8_t cable = tuh_midih_get_num_tx_cables(midi_dev_addr) - 1;


		// Toggle between sending note-on and note-off messages
		if (noteOnNext)
		{
			tuh_midi_stream_write(midi_dev_addr, cable, noteOn, sizeof(noteOn));
			noteOnNext = false;
		}
		else
		{
			tuh_midi_stream_write(midi_dev_addr, cable, noteOff, sizeof(noteOff));
			noteOnNext = true;
		}
	}

	
	// Code for second RP2040 core, blocking
	// Handles MIDI in/out messages
	// Untested on long SysEx messages
	void USBCore()
	{
		// Initialise TinyUSB
		board_init();
		tusb_init();
		
		while (1)
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

	
	// 48kHz audio processing function
	virtual void ProcessSample()
	{
		// No audio I/O, so just flash an LED
		// to indicate that the card is running
		LedOn(5, counter < 10000);

		// LED 4 indicates whether MIDI device is connected
		LedOn(4, device_connected);
		
		// Counter is reset by other core
		if (counter <= 30000)
			counter++;
	}
	
	static uint8_t device_connected;
	static uint8_t midi_dev_addr;
	
private:
	volatile uint32_t counter;
};


uint8_t MIDIHost::device_connected;
uint8_t MIDIHost::midi_dev_addr;


// Four callback functions that rppicomidi/usb_midi_host uses

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
{
	(void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx; // avoid unused variable warnings

	
	if (MIDIHost::midi_dev_addr == 0)
	{
		MIDIHost::midi_dev_addr = dev_addr;
		MIDIHost::device_connected = 1;
	}
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)instance;
	
	if (dev_addr == MIDIHost::midi_dev_addr)
	{
		MIDIHost::midi_dev_addr = 0;
		MIDIHost::device_connected = 0;
	}
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
	if (MIDIHost::midi_dev_addr != dev_addr)
		return;

	if (num_packets == 0)
		return;
	
	uint8_t cable_num;
	uint8_t buffer[48];
	while (1)
	{
		uint32_t bytes_read = tuh_midi_stream_read(dev_addr, &cable_num, buffer, sizeof(buffer));

		if (bytes_read == 0)
			return;
		
		for (uint32_t idx = 0; idx < bytes_read; idx++)
		{
			//buffer[idx]
		}
	}

}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}



int main()
{
	set_sys_clock_khz(144000, true);

	MIDIHost mh;
	mh.Run();
}

  
