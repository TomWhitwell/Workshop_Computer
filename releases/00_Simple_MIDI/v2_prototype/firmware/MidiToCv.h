#pragma once
#include "WebInterfaceComputerCard.h"
#include "NoteDownStore.h"
#include "AnalogueToMIDI.h"

/*
    MIDI <---> CV/gate mode
	(the normal operation of the card)
*/

class MidiToCv : public WebInterfaceComputerCard
{
	// Shared state: written by MIDICore (Core 0), read by ProcessSample (Core 1)
	volatile int16_t midiAudioOut[2] = { 0, 0 };
	volatile uint8_t lastPlayedNote[2] = { 60, 60 };
	volatile bool midiGate[2] = { false, false };
	volatile int16_t pitchbend[2] = { 0, 0 };
	volatile int16_t ccInputs[8] = {};

	NoteDownStore nds[2];

	// AnalogueToMIDI processors for CC34–41 outputs
	// Knobs 0-4095, switch 0-127, audio/CV in -2048 to 2047
	AnalogueToMIDI ccProcessors[8] = {
		{0, 4095}, {0, 4095}, {0, 4095}, {0, 127},
		{-2048, 2047}, {-2048, 2047}, {-2048, 2047}, {-2048, 2047}
	};

	constexpr static uint8_t NOTE_ON = 0x90, NOTE_OFF = 0x80, MIDI_CC = 0xB0, PITCH_BEND = 0xE0;
	void MIDIInterfaceCore()
	{

		// Receiving MIDI messages
		while (midiQueueRead != midiQueueWrite)
		{
			MIDIMessage msg = midiQueue[midiQueueRead];
			midiQueueRead = (midiQueueRead + 1) % midiQueueSize;

			uint8_t type = msg.status & 0xF0;
			uint8_t ch = msg.status & 0x0F;

			if (ch < 2)
			{
				if (type == NOTE_ON && msg.data2 > 0) // data2 = velocity
				{
					nds[ch].NoteOn(msg.data1, msg.data2);
					lastPlayedNote[ch] = msg.data1;
					midiGate[ch] = true;
				}
				else if (type == NOTE_OFF || (type == NOTE_ON && msg.data2 == 0)) 
				{
					nds[ch].NoteOff(msg.data1);
					int8_t lnd = nds[ch].LastNoteDown();
					if (lnd >= 0)
						lastPlayedNote[ch] = lnd;
					else
						midiGate[ch] = false;
				}
				else if (type == MIDI_CC && msg.data1 == 42) // MIDI CC 42 output on audio out jacks
				{
					midiAudioOut[ch] = (int16_t)(msg.data2 * 16);
				}
				else if (type == PITCH_BEND)
				{
					pitchbend[ch] = (int16_t)((msg.data1 | ((uint16_t)msg.data2 << 7)) - 8192);
				}
			}
		}

		// Sending MIDI messages
		static uint32_t lastSendUs = 0;
		static constexpr uint8_t ccNums[8] = { 34, 35, 36, 37, 38, 39, 40, 41 };

		uint32_t now = time_us_32();
		if (now - lastSendUs >= 2000)
		{
			lastSendUs = now;

			for (int i = 0; i < 8; i++)
			{
				ccProcessors[i](ccInputs[i]);
			}
			
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

public:
	void __not_in_flash_func(ProcessSample)() override final
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

		// Sample all sources for MIDI CCs to be sent
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

	void MIDICore() override
	{
		MIDIInterfaceCore();
	}
};
