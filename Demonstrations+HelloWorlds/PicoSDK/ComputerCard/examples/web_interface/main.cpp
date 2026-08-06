#include "ComputerCard.h"
#include "pico/multicore.h"
#include "tusb.h"
#include <cstdlib>


/*
  Web interface demo using WebMIDI and SysEx to transfer data both ways
  between Workshop System and the browser.

  Both the Javascript in interface.html and C++ firmware in main.cpp
  provide two functions:
  SendSysEx
  ProcessIncomingSysEx
  which take an array of bytes (values 0 to 127) and provide bidirectional
  communication between the web interface and WS Computer firmware.

  On in both the Javascript and C++ sides, developers should fill out
  out an implementation of ProcessIncomingSysex to handle the incoming
  messages, and call SendSysEx to send messages.


  When SendSysEx is called on the WS Computer, the ProcessIncomingSysEx 
  function is called on the web interface, with the same data.

  And vice versa, if SendSysEx is called by the web interface,
  ProcessIncomingSysex is then called on the WS computer, with this data.


  Here a very simple protocol is used where the first byte of the message
  is used to indicate what type of message it is.
  All sorts of alternatives are possible, including sending 
  (7-bit ascii) text such as JSON.

  On the firmware side, both MIDI sends and receives are designed to cope
  with long SysEx messages. This mandates an approach where the message may
  span multiple tud_midi_stream_read calls, and where tud_midi_stream_write
  may not be able to write all bytes of a message at once.
*/


////////////////////////////////////////////////////////////////////////////////
// Class to add MIDI SysEx connectivity to ComputerCard

// This is not a carefully written generic library, but abstracts out the
// MIDI/SysEx handling that may not need to change much, if at all, between different cards

class WebInterfaceComputerCard : public ComputerCard
{
public:
	WebInterfaceComputerCard()
	{
		// Start the second core
		multicore_launch_core1(core1);
	}

	// Boilerplate static function to call member function as second core
	static void core1()
	{
		((WebInterfaceComputerCard *)ThisPtr())->USBCore();
	}

	// Call to send (potentially large amounts of) data over MIDI.
	// Blocks until all data has been queued for sending.
	// A single tud_midi_stream_write call will fail if TinyUSB buffer is full,
	// which seems to occur in a single message more than ~48 bytes of data
	void MIDIStreamWriteBlocking(uint8_t cable, uint8_t const* data, uint32_t size)
	{
		uint32_t sent = 0;
		while (sent < size)
		{
			uint32_t n = tud_midi_stream_write(cable, data + sent, size - sent);
			sent += n;
			
			if (!n)
			{
				tud_task();
			}
		}
	}

	// Send a SysEx message of arbitrary length
	void SendSysEx(uint8_t *data, uint32_t size)
	{
		uint8_t header[] = {0xF0, MIDI_MANUFACTURER_ID};
		uint8_t footer[] = {0xF7};
		MIDIStreamWriteBlocking(0, header, 2);
		MIDIStreamWriteBlocking(0, data, size);
		MIDIStreamWriteBlocking(0, footer, 1);
	}

	// Code for second RP2040 core. Blocking.
	// Listens for SysEx messages over MIDI.
	void USBCore()
	{
		sysexBuf = (uint8_t*) malloc(sysexBufSize*sizeof(uint8_t));
		rxBuf = (uint8_t*) malloc(rxBufSize*sizeof(uint8_t));
		sysexActive = false;
		sysexLen = 0;
		
		// Initialise TinyUSB
		tusb_init();


		// This loop waits for and processes MIDI messages
		while (1)
		{
			tud_task();

			////////////////////////////////////////
			// Receiving MIDI
			while (tud_midi_available())
			{
				// Read MIDI input.
				// Result can be no data, part of a message (particularly a long SysEx),
				// one message, or multiple messages. 
				uint32_t bytesReceived = tud_midi_stream_read(rxBuf, rxBufSize);
				if (bytesReceived > 0)
				{
					ParseMIDIBytes(rxBuf, bytesReceived);
				}
			}
		   
			MIDICore();
		}
	}

	// Parse SysEx out of a MIDI stream and pass data in it to ProcessIncomingSysEx.
	// To process non-SysEx MIDI messages too, we'd need to modify this.
	void ParseMIDIBytes(uint8_t *rxBuf, unsigned bytesReceived)
	{
		// Iterate through received MIDI data byte-by-byte and apply a finite state machine
		for (unsigned i=0; i<bytesReceived; i++)
		{
			uint8_t b = rxBuf[i];

			// If we're not mid-message, look start of SysEx message
			if (!sysexActive)
			{
				if (b == 0xF0)
				{
					sysexActive = true;
					sysexLen = 0;
					sysexBuf[sysexLen++] = b;
				}
			}
			else // we're in the middle of a sysex message
			{
				// data byte
				if (sysexLen < sysexBufSize)
				{
					sysexBuf[sysexLen++] = b;
				}

				// End of message
				if (b == 0xF7)
				{
					if (sysexBuf[1] == MIDI_MANUFACTURER_ID) // check manufacturer before processing
					{
						// Chop off 0xF0, single-byte manufacturer ID, and 0xF7 terminator
						// before sneding to ProcessIncomingSysEx
						ProcessIncomingSysEx(sysexBuf+2, sysexLen-3);
					}
					sysexActive = false;
					sysexLen = 0;
				}
			}
		}
	}


	// New virtual function, overridden in specific class
	virtual void MIDICore() {}
	
	// New virtual function, overridden in specific class
	virtual	void ProcessIncomingSysEx(uint8_t */*data*/, uint32_t /*size*/) {}

private:
	static constexpr unsigned sysexBufSize = 512;// sets the largest SysEx message we can parse

	// 0x7D = prototyping, test, private use
	static constexpr uint8_t MIDI_MANUFACTURER_ID = 0x7D;
	static constexpr unsigned rxBufSize = 64;
	bool sysexActive;
	unsigned sysexLen;
	uint8_t *sysexBuf=nullptr, *rxBuf=nullptr;
};




////////////////////////////////////////////////////////////////////////////////
// Interface class for this specific program card

// SysEx Message IDs
constexpr uint8_t MESSAGE_MAIN_KNOB_POSITION = 0x01; // firmware -> HTML
constexpr uint8_t MESSAGE_SLIDER_POSITION = 0x02; // HTML -> firmware
constexpr uint8_t MESSAGE_INTERFACE_VERSION = 0x03; // HTML -> firmware
constexpr uint8_t MESSAGE_FIRMWARE_VERSION = 0x04;  // firmware -> HTML

// Firmware version
constexpr uint8_t FIRMWARE_VERSION_MAJOR = 0x00;
constexpr uint8_t FIRMWARE_VERSION_MINOR = 0x01;
constexpr uint8_t FIRMWARE_VERSION_PATCH = 0x00;



class WebInterfaceDemo : public WebInterfaceComputerCard
{
	// MIDICore is called continuously from the non-audio core.
	// It's a good place to send any SysEx back to the web interface.
	void MIDICore()
	{
		int32_t mainKnob = KnobVal(Main);
		if (lastMainKnob != mainKnob)
		{
			lastMainKnob = mainKnob;
			uint8_t vals[] = {MESSAGE_MAIN_KNOB_POSITION, uint8_t(mainKnob >> 5), uint8_t(mainKnob & 0x1F)};
			SendSysEx(vals, sizeof(vals));
		}
	}

	// Called whenever a message is received from the web interface.
	// This function receives just the SysEx data, not the header, (1-byte) manufacturer ID, or footer.
	void ProcessIncomingSysEx(uint8_t *data, uint32_t size)
	{
		if (data[0] == MESSAGE_SLIDER_POSITION && size == 2)
		{
			// Two byte message from interface, starting with 0x02 = position from slider
			sliderVal = data[1];
		}
		else if (data[0] == MESSAGE_INTERFACE_VERSION && size == 4)
		{
			// user interface sends its own version number. Respond with ours
			uint8_t vals[] = {MESSAGE_FIRMWARE_VERSION,
				FIRMWARE_VERSION_MAJOR,
				FIRMWARE_VERSION_MINOR,
				FIRMWARE_VERSION_PATCH};
			SendSysEx(vals, sizeof(vals));
		}
		else
		{
			// Otherwise, for debugging, just send back any SysEx that we were sent
			SendSysEx(data, size);
		}
	}

	
	// 48kHz audio processing function; runs on audio core
	virtual void ProcessSample()
	{
		// No audio I/O, so just flash an LED
		// to indicate that the card is running
		static int32_t frame=0;
		LedOn(5,(frame>>13)&1);
		frame++;


		// Set all CV and audio outs, and top 4 LEDs,
		// to value received from web interface slider
		CVOut1(sliderVal << 4);
		CVOut2(sliderVal << 4);
		AudioOut1(sliderVal << 4);
		AudioOut2(sliderVal << 4);
		LedBrightness(0, sliderVal << 4);
		LedBrightness(1, sliderVal << 4);
		LedBrightness(2, sliderVal << 4);
		LedBrightness(3, sliderVal << 4);
	}

private:
	// Volatile variable to communicate between the MIDI and audio cores
	volatile uint32_t sliderVal = 0;

	int32_t lastMainKnob = 0;
};


int main()
{
	set_sys_clock_khz(144000, true);

	WebInterfaceDemo wid;
	wid.Run();
}

  
