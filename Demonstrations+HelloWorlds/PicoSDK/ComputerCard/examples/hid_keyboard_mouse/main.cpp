/*
USB HID host demo

Supports connection of standard mouse or keyboard

ComputerCard runs on core 1, USB runs on core 0


Keyboard press flashes LED 0 and plays typewriter sounds
out of audio out 1 (bell, on enter key) or audio out 2 (other keys)

Mouse lights LEDs 1, 2, 3 on three buttons, and mouse X/Y position
control CV out 1 and 2.

*/

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "samples.h"
#include "tusb.h"

#include <algorithm>

#define MAX_REPORT 4

// Parsed report descriptor info for generic (non-boot-protocol) HID devices
static struct
{
	uint8_t report_count;
	tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];


class HIDKeyboardMouse : public ComputerCard
{
	WAVPlayer bell, key;
	uint8_t last_key_events = 0;
	uint8_t last_enter_events = 0;

	// Static state: written by USB core (core 0), read by audio core (core 1)
	static volatile bool key_pressed;
	static volatile uint8_t new_key_events, new_enter_events;
	static volatile int32_t mouse_x, mouse_y;
	static volatile uint8_t mouse_buttons;
	static hid_keyboard_report_t prev_report;

	static bool KeyInReport(hid_keyboard_report_t const *r, uint8_t kc)
	{
		for (int i = 0; i < 6; i++) // up to six keys in a HID keyboard report
			if (r->keycode[i] == kc) return true;
		return false;
	}

public:
	HIDKeyboardMouse()
	{
		bell.Load(bell_wav);
		key.Load(key_wav);
	}

	virtual void ProcessSample()
	{
		// Keyboard: trigger sound on new key events (enter key, or other key)
		uint8_t ke = new_key_events;
		uint8_t ee = new_enter_events;
		if (ke != last_key_events)
		{
			last_key_events = ke;
			key.Start();
		}
		if (ee != last_enter_events)
		{
			last_enter_events = ee;
			bell.Start();
		}

		// Bell and key click on separate outputs
		AudioOut1(bell.Tick());
		AudioOut2(key.Tick());

		// Mouse: accumulated position to CV outputs
		CVOut1(mouse_x);
		CVOut2(mouse_y);

		// LED 0: any keyboard key held
		// LEDs 1-3: mouse left / right / middle buttons
		LedOn(0, key_pressed);
		LedOn(1, mouse_buttons & 0x01);
		LedOn(2, mouse_buttons & 0x02);
		LedOn(3, mouse_buttons & 0x04);
	}



	static void ProcessKeyboard(hid_keyboard_report_t const *r)
	{
		for (int i = 0; i < 6; i++)
		{
			uint8_t kc = r->keycode[i];
			if (kc == 0)
			{
				continue;
			}

			if (!KeyInReport(&prev_report, kc))
			{
				if (kc == HID_KEY_ENTER)
				{
					new_enter_events++;
				}
				else
				{
					new_key_events++;
				}
			}
		}

		if (r->modifier & ~prev_report.modifier)
			new_key_events++;

		key_pressed = (r->modifier != 0);
		for (int i = 0; i < 6; i++)
			if (r->keycode[i]) key_pressed = true;

		prev_report = *r;
	}

	static void ProcessMouse(hid_mouse_report_t const *r)
	{
		// invert mouse y so that mouse up is higher voltage
		mouse_x = std::clamp<int32_t>(mouse_x + r->x, -2048, 2047);
		mouse_y = std::clamp<int32_t>(mouse_y - r->y, -2048, 2047);
		mouse_buttons = r->buttons;
	}

	static void ResetKeyboard()
	{
		key_pressed = false;
		prev_report = {};
	}

	static void ResetMouse()
	{
		mouse_buttons = 0;
	}
};

// Static member definitions
volatile bool HIDKeyboardMouse::key_pressed = false;
volatile uint8_t HIDKeyboardMouse::new_key_events = 0;
volatile uint8_t HIDKeyboardMouse::new_enter_events = 0;
volatile int32_t HIDKeyboardMouse::mouse_x = 0;
volatile int32_t HIDKeyboardMouse::mouse_y = 0;
volatile uint8_t HIDKeyboardMouse::mouse_buttons = 0;
hid_keyboard_report_t HIDKeyboardMouse::prev_report = {};


void core1()
{
	HIDKeyboardMouse card;
	card.Run();
}


////////////////////////////////////////////////////////////////////////////////
// TinyUSB host HID callbacks

// Callback when a HID device is mounted
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
	if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_NONE)
	{
		hid_info[instance].report_count = tuh_hid_parse_report_descriptor(
		    hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
	}

	if (!tuh_hid_receive_report(dev_addr, instance))
		return; // failed to queue receive; device will not send reports
}


// Callback when a HID device is unmounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
	if (proto == HID_ITF_PROTOCOL_KEYBOARD)
	{
		HIDKeyboardMouse::ResetKeyboard();
	}
	else if (proto == HID_ITF_PROTOCOL_MOUSE)
	{
		HIDKeyboardMouse::ResetMouse();
	}
	else
	{
		// Generic HID: check parsed report descriptor for keyboard/mouse usage
		for (uint8_t i = 0; i < hid_info[instance].report_count; i++)
		{
			tuh_hid_report_info_t *info = &hid_info[instance].report_info[i];
			if (info->usage_page == HID_USAGE_PAGE_DESKTOP)
			{
				if (info->usage == HID_USAGE_DESKTOP_KEYBOARD) HIDKeyboardMouse::ResetKeyboard();
				if (info->usage == HID_USAGE_DESKTOP_MOUSE) HIDKeyboardMouse::ResetMouse();
			}
		}
	}

	hid_info[instance] = {};
}


// Callback when HID report received - process with mouse or keyboard handler
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
	uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

	if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
	{
		HIDKeyboardMouse::ProcessKeyboard((hid_keyboard_report_t const *)report);
	}
	else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
	{
		HIDKeyboardMouse::ProcessMouse((hid_mouse_report_t const *)report);
	}
	else
	{
		// Generic HID: find the report by ID, dispatch by usage
		uint8_t rpt_count = hid_info[instance].report_count;
		tuh_hid_report_info_t *arr = hid_info[instance].report_info;
		tuh_hid_report_info_t *info = nullptr;

		if (rpt_count == 1 && arr[0].report_id == 0)
		{
			info = &arr[0]; // single report, no report-ID byte prefix
		}
		else if (len > 0)
		{
			for (uint8_t i = 0; i < rpt_count; i++)
			{
				if (report[0] == arr[i].report_id)
				{
					info = &arr[i];
					report++;
					len--;
					break;
				}
			}
		}

		if (info && info->usage_page == HID_USAGE_PAGE_DESKTOP)
		{
			if (info->usage == HID_USAGE_DESKTOP_KEYBOARD && len >= sizeof(hid_keyboard_report_t))
			{
				HIDKeyboardMouse::ProcessKeyboard((hid_keyboard_report_t const *)report);
			}
			else if (info->usage == HID_USAGE_DESKTOP_MOUSE && len >= sizeof(hid_mouse_report_t))
			{
				HIDKeyboardMouse::ProcessMouse((hid_mouse_report_t const *)report);
			}
		}
	}

	if (!tuh_hid_receive_report(dev_addr, instance))
	{
		return; // failed to re-queue; polling stops until device is re-mounted
	}
}


////////////////////////////////////////////////////////////////////////////////

int main()
{
	sleep_ms(50);
	multicore_launch_core1(core1);
	sleep_ms(50);

	tusb_rhport_init_t host_init = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_TUH_RHPORT, &host_init);

	while (1)
	{
		tuh_task();
	}
}
