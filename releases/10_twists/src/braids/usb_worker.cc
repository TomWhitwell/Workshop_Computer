#include "braids/usb_worker.h"
#include "braids/midi_message.h"
#include "bsp/board.h"
#include "tusb.h"

#include "pico/multicore.h"

namespace braids {

void UsbWorker::Init(midi_in_callback_t midiInCallback) {
	midi_in_callback = midiInCallback;
}

void UsbWorker::PostConfigProcessing() {
    settings.SetAvailableShapes(config_[OPT_SHAPE_1], 
        config_[OPT_SHAPE_2], 
        config_[OPT_SHAPE_3], 
        config_[OPT_SHAPE_4], 
        config_[OPT_SHAPE_5], 
        config_[OPT_SHAPE_6]);
	settings.SetValue(SETTING_AD_VCA, config_[OPT_ADVCA]);
	settings.SetValue(SETTING_AD_ATTACK, config_[OPT_ADATTACK]);
	settings.SetValue(SETTING_AD_DECAY, config_[OPT_ADDECAY]);
	settings.SetValue(SETTING_MIDICHANNEL_ENGINE, config_[OPT_MIDICHANENGINE]);
	settings.SetValue(SETTING_MIDICHANNEL_OUT1, config_[OPT_MIDICHANOUT1]);
	settings.SetValue(SETTING_MIDICHANNEL_OUT2, config_[OPT_MIDICHANOUT2]);
}

void UsbWorker::SetConfigFromFlash() {
	for (int i=0; i<CONFIG_LENGTH; i++)
	{
		config_[i] = *((uint8_t*)(XIP_BASE+configFlashAddr_+i));
	}
    // if we've saved settings previously...
    if(config_[0] == OPT_MAGIC) {
	    PostConfigProcessing();
    } else {
		config_[0] = OPT_MAGIC;
		memcpy(config_+1, settings.GetAvailableShapes(), NUM_AVAILABLE_SHAPES);
		config_[OPT_ADVCA] = settings.GetValue(SETTING_AD_VCA);
		config_[OPT_ADATTACK] = settings.GetValue(SETTING_AD_ATTACK);
		config_[OPT_ADDECAY] = settings.GetValue(SETTING_AD_DECAY);
		config_[OPT_MIDICHANENGINE] = settings.GetValue(SETTING_MIDICHANNEL_ENGINE);
		config_[OPT_MIDICHANOUT1] = settings.GetValue(SETTING_MIDICHANNEL_OUT1);
		config_[OPT_MIDICHANOUT2] = settings.GetValue(SETTING_MIDICHANNEL_OUT2);
	}
}

int UsbWorker::SetConfigFromSysEx(uint8_t *packet) {
	if (packet[SYSEX_INDEX_LENGTH] != CONFIG_LENGTH)
		return 1; //error
	
	// copy sysex to RAM config
	memcpy(config_, &(packet_[4]), CONFIG_LENGTH);
	if(config_[0] != OPT_MAGIC)
		return 1;
	
	// run any application-specific processing
	PostConfigProcessing();	
	return 0; // success
}

void UsbWorker::ProcessSysExCommand(uint8_t *packet) {
	if (packet[SYSEX_INDEX_MANUFACTURER] != SYSEX_MANUFACTURER_DEV)
		return;
	
	switch (packet[SYSEX_INDEX_COMMAND])
	{
	case SYSEX_COMMAND_PREVIEW: // preview: save config from sysex to ram, but not to flash
		SetConfigFromSysEx(packet);
		break;
		
	case SYSEX_COMMAND_WRITE_FLASH: // save config from sysex to ram, and also to flash card
		if (!SetConfigFromSysEx(packet))
		{
			// shut down the other core
			multicore_lockout_start_blocking();
			// erase page of flash
			uint32_t ints = save_and_disable_interrupts();
			flash_range_erase(configFlashAddr_, 4096);
			restore_interrupts(ints);

			// write config to flash
			ints = save_and_disable_interrupts();
			flash_range_program(configFlashAddr_, config_, CONFIG_LENGTH);
			restore_interrupts(ints);

			// restore other core
			multicore_lockout_end_blocking();
		}
		break;

	case SYSEX_COMMAND_READ:
		packet[0]=0xF0; // start sysex
		packet[SYSEX_INDEX_MANUFACTURER] = SYSEX_MANUFACTURER_DEV;
		packet[SYSEX_INDEX_COMMAND] = SYSEX_COMMAND_READ; // indicate what command this is in response to
		packet[SYSEX_INDEX_LENGTH] = CONFIG_LENGTH;
		memcpy(&(packet_[4]), config_, CONFIG_LENGTH); // copy config into sysex packet
		packet[CONFIG_LENGTH+4]=0xF7; // end sysex
		tud_midi_stream_write(0, packet_, CONFIG_LENGTH+5);
		break;

	default:
		break;
	}
}

void UsbWorker::MidiTask()
{
	// Read incoming packet if availabie
	while (tud_midi_available())
	{
		uint32_t bytesToProcess = tud_midi_stream_read(packet_, sizeof(packet_));

		uint8_t *bufferPtr = packet_;
		
		while (bytesToProcess > 0)
		{
			if (packet_[0] == 0xF0) // If sysex, handle with standard routine
			{
				ProcessSysExCommand(packet_);
			}
			else //if(midi_in_callback != NULL)
			{
				// else pass on to application midi message handler
				midi_in_callback(packet_);
			}

			// Move pointer to next midi message in buffer, if it exists
			// by scanning until we see a MIDI command byte (most significant bit set)
			do 
			{
				bufferPtr++;
				bytesToProcess--;
			} while (bytesToProcess > 0 && (!(*bufferPtr & 0x80)));			
		}
	}
}

void UsbWorker::Run() {
  board_init();
  tusb_init();

  SetConfigFromFlash();
  while(1)
  {
    tud_task();
    MidiTask();
  }
}

}