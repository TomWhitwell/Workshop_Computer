#include "ComputerCard.h"
#include "pico/multicore.h"
#include "tusb.h"



/*
  
   ComputerCard USB MIDI device example

   This example demonstrates sending and receiving MIDI messages,
   with the Workshop System Computer acting as a USB MIDI Device.

   Since the WS Computer is here a USB Device, it must be connected
   to a USB MIDI Host, such as a laptop/desktop computer.

   Audio (the ProcessSample function) is processed on one core of the RP2040,
   and MIDI is processed on the other core.


   MIDI messages received:
   - Note on/off messages turn on and off Pulse Out 1 and the top right LED.
   - Note on pitch is sent to CV out 1 (1V per octave)
   - MIDI CC 1 (Mod wheel) is sent to CV out 2, and to the middle right LED
   
   MIDI messages sent:
   - Turning the main knob sends MIDI CC 1 (Mod Wheel) messages, on channel 1.


   In this example, most of the jack/LED outputs from MIDI are controlled directly
   from the MIDI core. More typically MIDI data would be used by the audio core.

   As an example of inter-core communication, the volatile 'receivedCC1' member
   variable stores the MIDI CC 1 value received in the MIDI core,
   and the LED and CV out are set from this variable on the audio core.

 */


// Minimal MIDI message reading struct
struct MIDIMessage
{
	// Parse a MIDI message from the raw packet data
	MIDIMessage(uint8_t* packet)
	{
		channel = packet[0]&0x0F;
		command = static_cast<Command>(packet[0] & 0xF0);
		switch (packet[0]&0xF0)
		{
		case Command::NoteOn: // two separate data bytes
		case Command::NoteOff:
		case Command::Aftertouch:
		case Command::CC:
			note = packet[1];
			velocity = packet[2];
			break;
			
		case Command::PatchChange: // single data bytes
			instrument = packet[1];
			break;
			
		case Command::PitchBend: // pitch bend combines msb/lsb
			pitchbend = packet[1] + (packet[2]<<7) - 8192;
			break;
			
		default:
			command = Command::Unknown;
			break;
		}
	}
	
	enum Command {Unknown = 0x00, NoteOn = 0x90, NoteOff = 0x80,
		PitchBend = 0xE0, Aftertouch = 0xA0, CC = 0xB0,	PatchChange = 0xC0};
	Command command;
	uint8_t channel;
	union // Store MIDI data bytes in two-byte union
	{
		struct
		{
			union // data byte 1
			{
				uint8_t note; // note on/off/aftertouch
				uint8_t cc; // CC
				uint8_t instrument; // patch change
			};
			union // data byte 2
			{
				uint8_t velocity; // note on/off
				uint8_t touch; // aftertouch
				uint8_t value; // CC 
			};
		};
		int16_t pitchbend;
	};
};



class MIDIDevice : public ComputerCard
{
public:
	MIDIDevice()
	{
		receivedCC1 = 0;
		
		// Start the second core
		multicore_launch_core1(core1);
	}

	// Boilerplate static function to call member function as second core
	static void core1()
	{
		((MIDIDevice *)ThisPtr())->USBCore();
	}

	void HandleMIDIMessage(const MIDIMessage &m)
	{
		// Handle MIDI note on/off and mod wheel
		// Real MIDI note on/off handling should be more sophisticated, at least counting
		// the number of notes down, or possibly tracking the entire set of notes held down.

		// This approach does not cope with long messages (e.g. SysEx), or, potentially, with
		// very high message rates when tud_midi_stream_write doesn't write (all) bytes.
		// See web_interface demo for a way of resolving this.
		switch (m.command)
		{
		case MIDIMessage::NoteOn:
			if (m.velocity > 0) // real note on (velocity > 0)
			{
				CVOut1MIDINote(m.note);
				LedOn(1);
				PulseOut1(true);
			}
			else // note on with velocity 0 = note off
			{
				LedOff(1);
				PulseOut1(false);
			}
			break;
					
		case MIDIMessage::NoteOff:
			LedOff(1);
			PulseOut1(false);
			break;
					
		case MIDIMessage::CC:
			// Mod wheel -> CV out 2
			if (m.cc == 1)
			{
				receivedCC1 = m.value;
			}
			break;
					
		default:
			break;
		}
	}
	
	// Code for second RP2040 core. Blocking.
	void USBCore()
	{
		uint8_t lastCCOut = 0;
		uint8_t buffer[64];

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
				// Read MIDI input - this will be some number of MIDI messages (zero, one, or multiple)
				uint32_t bytesToProcess = tud_midi_stream_read(buffer, sizeof(buffer));
				uint8_t *bufPtr = buffer;

				while (bytesToProcess > 0)
				{
					// Read and process MIDI message
					MIDIMessage m(bufPtr);
					HandleMIDIMessage(m);
					
					// Move pointer to next midi message in buffer, if there is one
					// by scanning until we see a MIDI command byte (most significant bit set)
					do 
					{
						bufPtr++;
						bytesToProcess--;
					} while (bytesToProcess > 0 && (!(*bufPtr & 0x80)));			
				}
			}

			////////////////////////////////////////
			// Sending MIDI
			
			// Read main knob, and if value has changed, send 
			// MIDI CC 1 (Mod wheel) on channel 1 with knob position
			// (In a real application, would want to add some hysteresis to eliminate jitter)
			uint8_t ccOut = KnobVal(Knob::Main) >> 5;
			if (ccOut != lastCCOut) // if value that would be sent has changed since last time,
			{
				uint8_t ccmesg[3] = {0xB0, 0x01, ccOut}; // construct...
				tud_midi_stream_write(0, ccmesg, 3); // ...and send MIDI message
				lastCCOut = ccOut;
			}
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


		// Set CV out 2 and LED 3 according to value received from MIDI
		CVOut2(receivedCC1 << 4);
		LedBrightness(3, receivedCC1 << 4);
	}

private:
	// Variable to communicate between the MIDI and audio cores
	volatile uint32_t receivedCC1;
};


int main()
{
	set_sys_clock_khz(144000, true);

	MIDIDevice md;
	md.Run();
}

  
