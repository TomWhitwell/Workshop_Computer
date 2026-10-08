/*
EightMU

A standalone class providing access to a Music Thing Modular 8mu USB MIDI
controller, intended to be used as a member of a ComputerCard subclass.

It gives access to the 8mu's eight faders, its pitch/roll/yaw accelerometer
axes and its four buttons, and lets the card set the brightness of the 8mu's
eight LEDs.  All of the USB MIDI host setup is handled internally, on the
second RP2040 core.

Usage:

	#include "ComputerCard.h"
	#include "EightMU.h"

	class MyCard : public ComputerCard
	{
		EightMU mu;
	public:
		MyCard()
		{
			mu.Start(); // claims core1 and runs the USB host stack there
		}

		virtual void ProcessSample()
		{
			CVOut1(mu.Fader(0) >> 1);
			mu.SetLed(0, mu.Fader(0));
		}
	};

Notes:

- The 8mu is a USB device, so the Computer must be acting as a USB host.

- Only one EightMU may exist, since there is only one USB port - hubs are not
  currently supported.

- On connecting, EightMU sends the 8mu a heartbeat/identify SysEx (0x21).
  As well as confirming that the attached device really is an 8mu, this
  makes the 8mu load its default configuration, so the fader and button
  assignments this class expects are guaranteed.  As a side effect the 8mu
  is switched to bank 1, and its bank buttons stop working until it is
  power-cycled.

- If more than one source file includes this header, #define EIGHTMU_NOIMPL
  before the include in all but one of them.

This is a single-file header: it carries the rppicomidi usb_midi_host driver
(MIT, Copyright (c) 2023 rppicomidi) and the host-mode TinyUSB configuration
that the USB stack needs, so nothing else has to be added to the build but
the libraries and one compile definition:

	target_link_libraries(mycard pico_multicore tinyusb_host tinyusb_board)
	target_compile_definitions(mycard PRIVATE CFG_TUSB_CONFIG_FILE="EightMU.h")

That definition is what makes this file serve as tusb_config.h as well.
Alternatively, drop a one-line tusb_config.h containing #include "EightMU.h"
next to the card's main.cpp and leave the definition out.

#define EIGHTMU_NOUSBDRIVER before the include to leave all of that out - the
TinyUSB configuration, the usb_midi_host declarations and the driver itself -
for cards that supply their own copy of the driver and their own
tusb_config.h, such as one running USB host and device modes together.

MIT License

Copyright (c) 2026 Chris Johnson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* TinyUSB host configuration.

   This block is what TinyUSB itself sees when EightMU.h is used as its
   configuration file (-DCFG_TUSB_CONFIG_FILE="EightMU.h"), which is how the
   host-mode tusb_config.h that the USB stack needs is supplied without a
   separate file.  It therefore has to sit outside, and before, the EIGHTMU_H
   include guard: tusb_option.h includes this file again from inside it, and
   is compiled as C, not C++.
*/
#ifndef EIGHTMU_NOUSBDRIVER
#ifndef EIGHTMU_TUSB_CONFIG_H
#define EIGHTMU_TUSB_CONFIG_H

#ifndef CFG_TUSB_MCU
 #error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_HOST
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

// Size of buffer to hold descriptors and other data used for enumeration.
// 512 rather than the default 256, in case any device's combined config
// descriptor is larger than 256 bytes (would otherwise stall enumeration).
#define CFG_TUH_ENUMERATION_BUFSIZE 512

// One hub instance, so an 8mu still works when plugged in through a hub.
// EightMU only ever talks to one device, so there is no need for the deeper
// cascaded-hub support that private_examples/multi_8mu enables.
#define CFG_TUH_HUB                 1
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
//NOTE: Do not #define CFG_TUH_MIDI 1 to enable MIDI Host. A code fragment in usbh.c breaks the build if you do that
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// max device support (excluding hub device)
#define CFG_TUH_DEVICE_MAX          2

// MIDI Host string support
#define CFG_MIDI_HOST_DEVSTRINGS    1

#endif // EIGHTMU_TUSB_CONFIG_H
#endif // EIGHTMU_NOUSBDRIVER


/* Everything below this point is C++, and is skipped when TinyUSB's own C
   sources pull this file in for the configuration above. */
#ifdef __cplusplus

#ifndef EIGHTMU_H
#define EIGHTMU_H

#include <stdint.h>

#include "pico/multicore.h"
#include "hardware/timer.h"
#include "bsp/board.h"
#include "tusb.h"
#ifdef EIGHTMU_NOUSBDRIVER
#include "usb_midi_host.h"
#else
//============================================================================
// BEGIN vendored rppicomidi usb_midi_host.h
//============================================================================
/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 rppicomidi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_MIDI_HOST_H_
#define _TUSB_MIDI_HOST_H_

#include "class/audio/audio.h"
#include "class/midi/midi.h"

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// Class Driver Configuration
//--------------------------------------------------------------------+
// Use this function in Arduino or other environments where modifying
// the tusb_config.h file is not practical.
// Call this before midih_init() gets called by the TinyUSB stack,
// which is before the application calls tusb_init() or tuh_init().
// Calling this function after midih_init() will cause problems.
//
// Note: To figure out how long a USB MIDI 1.0 stream needs to be
// in bytes, multiply the number of bytes in the stream by 4/3 and
// round up to the nearest 4 bytes.
// For exampe, to send a 146 byte SysEx message, 146*4/3 = 194.66...
// The next nearest 4 byte boundary is 196. So the buffer is 196 bytes,
// or 49 4-byte USB MIDI packets.
//
// Parameters:
// midi_rx_buffer_bytes is the number of bytes the USB Host can buffer
// from the device. This has to be at least equal to the maximum bulk
// transfer size of 64 bytes, but it is a good idea to set this to
// at least the maximum SysEx message size in MIDI packets to improve
// throughput.
//
// midi_tx_buffer_bytes is the maximum number of bytes the application
// can write out to the interface in a single transaction. This should
// be at least as large as the maximum bulk transfer size of 64 bytes.
// To send long SysEx messages, you should make this buffer at least as
// long as the longest message in MIDI packets or else SysEx message
// writes may get truncated.
//
// max_cables defaults to 16. If you know you only need to convert
// serial MIDI data to USB MIDI packets from cable numbers 0:N,
// and N is less than 15, you can save a small amount
// if RAM if you set this value to something less than 16.
// For example, if your application is to convert a single
// Serial Port MIDI to USB, then the highest cable number
// is 0, so you only need one virtual cable. You will save the
// deserialization buffer for 15 virtual cables (about 90 bytes).
//
void tuh_midih_define_limits(size_t midi_rx_buffer_bytes, size_t midi_tx_buffer_bytes, uint8_t max_cables);

#ifndef CFG_MIDI_HOST_DEVSTRINGS
#ifdef ARDUINO
#define CFG_MIDI_HOST_DEVSTRINGS 1
#else
#define CFG_MIDI_HOST_DEVSTRINGS 0
#endif
#endif

//--------------------------------------------------------------------+
// Application API (Single Interface)
//--------------------------------------------------------------------+

bool     tuh_midi_configured      (uint8_t dev_addr);

// return the number of virtual midi cables on the device's OUT endpoint
uint8_t tuh_midih_get_num_tx_cables (uint8_t dev_addr);

// return the number of virtual midi cables on the device's IN endpoint
uint8_t tuh_midih_get_num_rx_cables (uint8_t dev_addr);

// Queue a packet to the device. The application
// must call tuh_midi_stream_flush to actually have the
// data go out. It is up to the application to properly
// format this packet; this function does not check.
// Using this function with tuh_midi_stream_write()
// might produce undefined behavior.
// Returns true if the packet was successfully queued.
bool tuh_midi_packet_write (uint8_t dev_addr, uint8_t const packet[4]);

// Queue a message to the device. The application
// must call tuh_midi_stream_flush to actually have the
// data go out. Note that cable_num must be < CFG_TUH_CABLE_MAX
// (note CFG_TUH_CABLE_MAX default is 16)
uint32_t tuh_midi_stream_write (uint8_t dev_addr, uint8_t cable_num, uint8_t const* p_buffer, uint32_t bufsize);

/// Return true if the MIDI OUT FIFO has enough space for at
/// least one more message
bool tuh_midi_can_write_stream (uint8_t dev_addr);

// Send any queued packets to the device if the host hardware is able to do it
// Returns the number of bytes flushed to the host hardware or 0 if
// the host hardware is busy or there is nothing in queue to send.
uint32_t tuh_midi_stream_flush( uint8_t dev_addr);

// Get the MIDI stream from the device. Set the value pointed
// to by p_cable_num to the MIDI cable number intended to receive it.
// The MIDI stream will be stored in the buffer pointed to by p_buffer.
// Return the number of bytes added to the buffer.
// Note that this function ignores the CIN field of the MIDI packet
// because a number of commercial devices out there do not encode
// it properly.
uint32_t tuh_midi_stream_read (uint8_t dev_addr, uint8_t *p_cable_num, uint8_t *p_buffer, uint16_t bufsize);

// Read a raw MIDI packet from the connected device
// This function does not parse the packet format
// Return true if a packet was returned
bool tuh_midi_packet_read (uint8_t dev_addr, uint8_t packet[4]);

uint8_t tuh_midi_get_num_rx_cables(uint8_t dev_addr);
uint8_t tuh_midi_get_num_tx_cables(uint8_t dev_addr);
#if CFG_MIDI_HOST_DEVSTRINGS
uint8_t tuh_midi_get_rx_cable_istrings(uint8_t dev_addr, uint8_t* istrings, uint8_t max_istrings);
uint8_t tuh_midi_get_tx_cable_istrings(uint8_t dev_addr, uint8_t* istrings, uint8_t max_istrings);
uint8_t tuh_midi_get_all_istrings(uint8_t dev_addr, const uint8_t** istrings);
#endif
//--------------------------------------------------------------------+
// Internal Class Driver API
//--------------------------------------------------------------------+
bool midih_init       (void);
bool midih_deinit     (void);
bool midih_open       (uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *desc_itf, uint16_t max_len);
bool midih_set_config (uint8_t dev_addr, uint8_t itf_num);
bool midih_xfer_cb    (uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);
void midih_close      (uint8_t dev_addr);

//--------------------------------------------------------------------+
// Callbacks (Weak is optional)
//--------------------------------------------------------------------+

// Invoked when device with MIDI interface is mounted.
// If the MIDI host application requires MIDI IN, it should requst an
// IN transfer here. The device will likely NAK this transfer. How the driver
// handles the NAK is hardware dependent.
TU_ATTR_WEAK void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx);

// Invoked when device with MIDI interface is un-mounted
// For now, the instance parameter is always 0 and can be ignored
TU_ATTR_WEAK void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance);

TU_ATTR_WEAK void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets);
TU_ATTR_WEAK void tuh_midi_tx_cb(uint8_t dev_addr);
#ifdef __cplusplus
}
#endif

#endif /* _TUSB_MIDI_HOST_H_ */
//============================================================================
// END vendored rppicomidi usb_midi_host.h
//============================================================================
#endif // EIGHTMU_NOUSBDRIVER

class EightMU
{
public:
	static constexpr int numFaders = 8;
	static constexpr int numButtons = 4;
	static constexpr int numLeds = 8;

	EightMU()
	{
		instance = this;
	}

	/** \brief Start the USB host stack on the second core.

	    Launches core1, which then runs USB MIDI host handling forever.
	    Call this before ComputerCard::Run(), and do not use core1 for
	    anything else.  Cards that need core1 themselves should instead
	    call Poll() repeatedly from their own core1 loop.
	*/
	void Start()
	{
		instance = this;
		multicore_launch_core1(Core1Entry);
	}

	/** \brief Service USB MIDI, for cards that run their own core1 loop.

	    Call repeatedly from core1, after board_init() and tusb_init().
	    Not needed if Start() is used.
	*/
	void Poll()
	{
		tuh_task();
		Service();
	}

	/// True once an attached device has identified itself as an 8mu
	bool Connected() const {return connected;}

	/// 8mu firmware version, valid while Connected()
	uint8_t FirmwareMajor() const {return fwMajor;}
	uint8_t FirmwareMinor() const {return fwMinor;}
	uint8_t FirmwarePoint() const {return fwPoint;}

	/// Fader position (0-7), from 0 to 4064, matching the range of KnobVal
	int32_t Fader(int i) const
	{
		return (i >= 0 && i < numFaders) ? ccValues[i] : 0;
	}

	// Motion sensing, from -2032 to 2032, matching the range of the audio and
	// CV jacks.  The 8mu sends each of these as a pair of one-sided CCs, which
	// are subtracted here to give a signed value.  All are heavily smoothed by
	// the 8mu, so they respond over tens of milliseconds rather than instantly.
	//
	// Pitch, Roll and Flip are the three axes of the accelerometer, so for a
	// stationary 8mu they are the three components of one gravity vector, and
	// are not independent of each other: knowing two fixes the magnitude of the
	// third.  The 8mu clips them at 1g, which is the whole range gravity can
	// produce, so lying flat gives Pitch and Roll of 0 and Flip at full scale,
	// and tilting through 90 degrees takes Pitch or Roll to full scale and Flip
	// to 0.  Faster movement than gravity does not read any higher.

	/// Tilt, positive with the back of the 8mu lifted
	int32_t Pitch() const {return (ccValues[8] - ccValues[9]) >> 1;}

	/// Tilt, positive with the right of the 8mu lifted
	int32_t Roll() const {return (ccValues[10] - ccValues[11]) >> 1;}

	/** \brief Rotation, positive turning clockwise.

	    Unlike Pitch and Roll, this comes from the gyroscope, so it is a rate
	    of rotation rather than a position: it returns to 0 whenever the 8mu is
	    not actually being turned, however far it has been turned in total.
	*/
	int32_t Yaw() const {return (ccValues[12] - ccValues[13]) >> 1;}

	/** \brief Which way up the 8mu is: -2032 lying flat, 2032 upside down.

	    This is the vertical accelerometer axis.  Its magnitude says little that
	    Pitch and Roll do not (see above), but its sign distinguishes right way
	    up from upside down, which they cannot: both read 0 either way up.
	*/
	int32_t Flip() const {return (ccValues[14] - ccValues[15]) >> 1;}

	/// True while button (0-3) is held down
	bool Button(int i) const
	{
		return (i >= 0 && i < numButtons) ? buttons[i] : false;
	}

	/** \brief Set brightness of 8mu LED (0-7), from 0 to 4095.

	    The first call to any of the LED functions takes the LEDs over from
	    the 8mu, which otherwise flashes them on MIDI activity.  An 8mu used
	    only as a controller, that never has its LEDs set, is left alone.
	*/
	void SetLed(int i, int32_t brightness)
	{
		SetLedRaw(i, int(brightness >> 5));
	}

	/// Turn 8mu LED (0-7) fully on
	void LedOn(int i) {SetLedRaw(i, 127);}

	/// Turn 8mu LED (0-7) off
	void LedOff(int i) {SetLedRaw(i, 0);}

	/// Hand the LEDs back to the 8mu, restoring its MIDI activity flashing
	void ReleaseLeds() {ledsWanted = false;}

	static EightMU *Instance() {return instance;}

	// Called from the usb_midi_host callbacks, on core1.
	// Not intended to be called by cards.
	void OnMount(uint8_t addr);
	void OnUnmount(uint8_t addr);
	void OnMIDIBytes(uint8_t addr, const uint8_t *msg, int size);

private:
	/// Set brightness of 8mu LED (0-7) as a raw 7-bit MIDI velocity, 0 to 127
	void SetLedRaw(int i, int v)
	{
		if (i < 0 || i >= numLeds) return;
		if (v < 0) v = 0;
		if (v > 127) v = 127;
		ledDesired[i] = uint8_t(v);
		ledsWanted = true;
	}

	// SysEx messages understood by the 8mu.  0x7D is the 'non-commercial'
	// MIDI manufacturer ID, and 00 00 the 8mu's device bytes.
	static constexpr uint8_t sysExIdentify[6] = {0xF0, 0x7D, 0x00, 0x00, 0x21, 0xF7};
	static constexpr uint8_t sysExFaderQuery[6] = {0xF0, 0x7D, 0x00, 0x00, 0x12, 0xF7};
	static constexpr uint8_t sysExBlinkOff[6] = {0xF0, 0x7D, 0x00, 0x00, 0x30, 0xF7};
	static constexpr uint8_t sysExBlinkOn[6] = {0xF0, 0x7D, 0x00, 0x00, 0x31, 0xF7};

	// The 8mu's index in the Music Thing editor, sent in its identify reply
	static constexpr uint8_t deviceIndex = 6;

	// Faders are CC 34-41, accelerometer axes CC 42-49
	static constexpr uint8_t firstCC = 34;
	static constexpr int numCCs = 16;

	static constexpr uint8_t buttonNotes[numButtons] = {36, 48, 60, 72};

	static constexpr uint32_t identifyRetryUs = 250000;
	static constexpr uint8_t identifyMaxTries = 8;
	static constexpr uint32_t faderQueryRetryUs = 250000;
	static constexpr uint8_t faderQueryMaxTries = 8;

	// The 8mu only spots SysEx whose four header bytes land contiguously
	// within one 64-byte read, and it discards the rest of that read once
	// it has handled one message.  So SysEx must go out on its own, spaced
	// out from anything else we send.
	static constexpr uint32_t sysExGapUs = 50000;

	// Setting the 8mu's blink flag is idempotent, and resending it means a
	// single dropped message doesn't leave the LEDs flashing indefinitely.
	static constexpr uint32_t blinkOffResendUs = 2000000;

	static constexpr uint32_t ledUpdateUs = 20000; // 50Hz

	// If a device disconnects part-way through enumeration, TinyUSB can be
	// left unable to attach anything else.  Reinitialising the host stack
	// clears this, but is rate-limited in case the bus is genuinely dead.
	static constexpr uint32_t recoveryDebounceUs = 3000000;
	static constexpr uint32_t minRecoveryIntervalUs = 5000000;

	// Written on core1, read on core0
	volatile int32_t ccValues[numCCs] = {};
	volatile bool buttons[numButtons] = {};
	volatile bool connected = false;
	volatile uint8_t fwMajor = 0, fwMinor = 0, fwPoint = 0;

	// Written on core0, read on core1
	volatile uint8_t ledDesired[numLeds] = {};
	volatile bool ledsWanted = false;

	// Core1 only
	volatile uint8_t devAddr = 0; // volatile: written by the umount callback
	uint8_t ledSent[numLeds] = {};
	bool ledPrimed = false;
	bool blinkDisabled = false;
	bool faderReplyGot = false;
	bool faderQueryDone = false;
	uint8_t identifyTries = 0, faderQueryTries = 0;
	uint32_t lastIdentifyUs = 0, lastFaderQueryUs = 0;
	uint32_t lastBlinkOffUs = 0, lastLedUpdateUs = 0;
	uint32_t lastSysExUs = 0;
	bool haveSentSysEx = false;
	bool sysExSent = false;
	bool recoveryArmed = false, haveRecovered = false;
	uint32_t recoveryCheckUs = 0, lastRecoveryUs = 0;

	static EightMU *instance;

	static void Core1Entry();

	void Service();
	void ServiceLeds(uint32_t now);
	void RecoveryWatchdog(uint32_t now);
	void ResetConnection();

	bool MIDIQueue(const uint8_t *data, uint32_t size);
	void MIDIFlush();
	bool SendSysEx(const uint8_t *data, uint32_t size, uint32_t now);
};


#ifndef EIGHTMU_NOIMPL

#ifndef EIGHTMU_NOUSBDRIVER

// Includes hoisted out of the two vendored .c files below, so that they
// are not pulled in from inside an extern "C" block.
#include "tusb_option.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"
#include <stdlib.h>

extern "C" {

//============================================================================
// BEGIN vendored rppicomidi usb_midi_host.c
//
// Taken from the eightmu/ directory of this repository, which was deleted in
// favour of this copy (see commit 67fec98 for the originals).  Verbatim apart
// from these changes, needed because it is compiled as C++ here rather than
// as C:
//   - get_midi_host(): TU_VERIFY expands to "return false", which C++ will
//     not convert to a pointer, so it is written out as an explicit test.
//   - the three malloc() results in midih_init() are cast.
//   - the zero-length usbh_edpt_xfer() in midih_xfer_cb() passed
//     XFER_RESULT_SUCCESS where a uint8_t* buffer is expected; now NULL.
//   - its #includes are hoisted above the extern "C" block.
//============================================================================
/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 rppicomidi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */


#if (TUSB_OPT_HOST_ENABLED)


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+
#ifndef CFG_TUH_MAX_CABLES
  #define CFG_TUH_MAX_CABLES 16
#endif
#ifndef CFG_TUH_MIDI_RX_BUFSIZE
  #define CFG_TUH_MIDI_RX_BUFSIZE USBH_EPSIZE_BULK_MAX
#endif
#ifndef CFG_TUH_MIDI_TX_BUFSIZE
  #define CFG_TUH_MIDI_TX_BUFSIZE USBH_EPSIZE_BULK_MAX
#endif
#ifndef CFG_TUH_MIDI_EP_BUFSIZE
  #define CFG_TUH_MIDI_EP_BUFSIZE USBH_EPSIZE_BULK_MAX
#endif

#define MIDI_MAX_DATA_VAL 0x7f
static struct midih_limits_s {
    size_t midi_rx_buf;
    size_t midi_tx_buf;
    uint8_t max_cables;
} midih_limits = {CFG_TUH_MIDI_RX_BUFSIZE, CFG_TUH_MIDI_TX_BUFSIZE, CFG_TUH_MAX_CABLES};

// This descriptor follows the standard bulk data endpoint descriptor
typedef struct
{
  uint8_t bLength            ; ///< Size of this descriptor in bytes (4+bNumEmbMIDIJack)
  uint8_t bDescriptorType    ; ///< Descriptor Type, must be CS_ENDPOINT
  uint8_t bDescriptorSubType ; ///< Descriptor SubType, must be MS_GENERAL
  uint8_t bNumEmbMIDIJack;   ; ///< Number of embedded MIDI jacks associated with this endpoint
  uint8_t baAssocJackID[];   ; ///< A list of associated jacks
} midi_cs_desc_endpoint_t;

typedef struct
{
  uint8_t buffer[4];
  uint8_t index;
  uint8_t total;
}midi_stream_t;

typedef struct
{
  uint8_t dev_addr;
  uint8_t itf_num;

  uint8_t ep_in;          // IN endpoint address
  uint8_t ep_out;         // OUT endpoint address
  uint16_t ep_in_max;     // min( midih_limits.midi_rx_buf, wMaxPacketSize of the IN endpoint)
  uint16_t ep_out_max;    //  min( midih_limits.midi_tx_buf, wMaxPacketSize of the OUT endpoint)

  uint8_t num_cables_rx;  // IN endpoint CS descriptor bNumEmbMIDIJack value
  uint8_t num_cables_tx;  // OUT endpoint CS descriptor bNumEmbMIDIJack value

  // For Stream read()/write() API
  // Messages are always 4 bytes long, queue them for reading and writing so the
  // callers can use the Stream interface with single-byte read/write calls.
  midi_stream_t *stream_write;
  midi_stream_t stream_read;

  /*------------- From this point, data is not cleared by bus reset -------------*/
  // Endpoint FIFOs
  tu_fifo_t rx_ff;
  tu_fifo_t tx_ff;
 

  uint8_t *rx_ff_buf;
  uint8_t *tx_ff_buf;

  #if CFG_FIFO_MUTEX
  osal_mutex_def_t rx_ff_mutex;
  osal_mutex_def_t tx_ff_mutex;
  #endif

  // Endpoint Transfer buffer
  CFG_TUSB_MEM_ALIGN uint8_t epout_buf[CFG_TUH_MIDI_EP_BUFSIZE];
  CFG_TUSB_MEM_ALIGN uint8_t epin_buf[CFG_TUH_MIDI_EP_BUFSIZE];

  bool configured;
  // Track the transfer result in the xfer_cb function
  // If the result is not XFER_RESULT_SUCCESS, block
  // midih_flush() and do not restart IN polling.
  // The user will need to unplug and re-plug the device
  xfer_result_t last_xfer_result;
#if CFG_MIDI_HOST_DEVSTRINGS
#define MAX_STRING_INDICES 32
  uint8_t all_string_indices[MAX_STRING_INDICES];
  uint8_t num_string_indices;
#define MAX_IN_JACKS 8
#define MAX_OUT_JACKS 8
  struct {
    uint8_t jack_id;
    uint8_t jack_type;
    uint8_t string_index;
  } in_jack_info[MAX_IN_JACKS];
  uint8_t next_in_jack;
  struct {
    uint8_t jack_id;
    uint8_t jack_type;
    uint8_t num_source_ids;
    uint8_t source_ids[MAX_IN_JACKS/4];
    uint8_t string_index;
  } out_jack_info[MAX_OUT_JACKS];
  uint8_t next_out_jack;
  uint8_t ep_in_associated_jacks[MAX_OUT_JACKS/2];
  uint8_t ep_out_associated_jacks[MAX_IN_JACKS/2];
#endif
}midih_interface_t;

static midih_interface_t _midi_host[CFG_TUH_DEVICE_MAX];

static midih_interface_t *get_midi_host(uint8_t dev_addr)
{
  if (!(dev_addr > 0 && dev_addr <= CFG_TUH_DEVICE_MAX)) return NULL;
  return (_midi_host + dev_addr - 1);
}

//------------- Internal prototypes -------------//
static uint32_t write_flush(uint8_t dev_addr, midih_interface_t* midi);

static void midih_freeall(void)
{
  // free memory allocated by midih_init()
  for (int inst = 0; inst < CFG_TUH_DEVICE_MAX; inst++)
  {
    midih_interface_t *p_midi_host = &_midi_host[inst];
    if (p_midi_host->rx_ff_buf != NULL)
    {
      free (p_midi_host->rx_ff_buf);
      p_midi_host->rx_ff_buf = NULL;
    }
    if (p_midi_host->tx_ff_buf != NULL)
    {
      free (p_midi_host->tx_ff_buf);
      p_midi_host->tx_ff_buf = NULL;
    }
    if (p_midi_host->stream_write != NULL)
    {
      free(p_midi_host->stream_write);
      p_midi_host->stream_write = NULL;
    }
  }
}

//--------------------------------------------------------------------+
// USBH API
//--------------------------------------------------------------------+
void tuh_midih_define_limits(size_t midi_rx_buffer_bytes, size_t midi_tx_buffer_bytes, uint8_t max_cables)
{
  // prevent memory leak in case midih_init() was called before this function
  midih_freeall();
  midih_limits.midi_rx_buf = midi_rx_buffer_bytes;
  midih_limits.midi_tx_buf = midi_tx_buffer_bytes;
  midih_limits.max_cables = max_cables;
}

bool midih_init(void)
{
  tu_memclr(&_midi_host, sizeof(_midi_host));
  // config fifos
  for (int inst = 0; inst < CFG_TUH_DEVICE_MAX; inst++)
  {
    midih_interface_t *p_midi_host = &_midi_host[inst];
    p_midi_host->rx_ff_buf = (uint8_t *)malloc(midih_limits.midi_rx_buf);
    p_midi_host->tx_ff_buf = (uint8_t *)malloc(midih_limits.midi_tx_buf);
    p_midi_host->stream_write = (midi_stream_t *)malloc(midih_limits.max_cables * sizeof(midi_stream_t));
    TU_ASSERT((p_midi_host->rx_ff_buf != NULL && p_midi_host->tx_ff_buf != NULL && p_midi_host->stream_write != NULL), 0);
    tu_memclr(p_midi_host->stream_write, sizeof(*(p_midi_host->stream_write))*midih_limits.max_cables);
    tu_fifo_config(&p_midi_host->rx_ff, p_midi_host->rx_ff_buf, midih_limits.midi_rx_buf, 1, false); // true, true
    tu_fifo_config(&p_midi_host->tx_ff, p_midi_host->tx_ff_buf, midih_limits.midi_tx_buf, 1, false); // OBVS.

  #if CFG_FIFO_MUTEX
    tu_fifo_config_mutex(&p_midi_host->rx_ff, NULL, osal_mutex_create(&p_midi_host->rx_ff_mutex));
    tu_fifo_config_mutex(&p_midi_host->tx_ff, osal_mutex_create(&p_midi_host->tx_ff_mutex), NULL);
  #endif
  }
  return true;
}

bool midih_deinit()
{
  midih_freeall();
  return true;
}
bool midih_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  p_midi_host->last_xfer_result = result;
  if (result == XFER_RESULT_FAILED) {
    TU_LOG2("MIDIH xfer result failed\r\n");
    return false;
  }
  TU_ASSERT(result == XFER_RESULT_SUCCESS);

  if ( ep_addr == p_midi_host->ep_in)
  {
    // receive new data if available
    uint32_t packets_queued = 0;
    if (xferred_bytes)
    {
      // put in the RX FIFO only non-zero MIDI IN 4-byte packets
      uint8_t* buf = p_midi_host->epin_buf;
      uint32_t npackets = xferred_bytes / 4;
      uint32_t packet_num;
      for (packet_num = 0; packet_num < npackets; packet_num++)
      {
        // some devices send back all zero packets even if there is no data ready
        uint32_t packet = (uint32_t)((*buf)<<24) | ((uint32_t)(*(buf+1))<<16) | ((uint32_t)(*(buf+2))<<8) | ((uint32_t)(*(buf+3)));
        if (packet != 0)
        {
          tu_fifo_write_n(&p_midi_host->rx_ff, buf, 4);
          ++packets_queued;
          TU_LOG3("MIDI RX=%08lx\r\n", packet);
        }
        buf += 4;
      }
      // invoke receive callback if available
      if (tuh_midi_rx_cb && packets_queued)
      {
        tuh_midi_rx_cb(dev_addr, packets_queued);
      }
    }

    TU_LOG2("Requesting poll IN endpoint %d\r\n", p_midi_host->ep_in);
    TU_ASSERT(usbh_edpt_xfer(p_midi_host->dev_addr, p_midi_host->ep_in, p_midi_host->epin_buf, p_midi_host->ep_in_max), 0);
  }
  else if ( ep_addr == p_midi_host->ep_out )
  {
    if (0 == write_flush(dev_addr, p_midi_host))
    {
      // If there is no data left, a ZLP should be sent if
      // xferred_bytes is multiple of EP size and not zero
      if ( !tu_fifo_count(&p_midi_host->tx_ff) && xferred_bytes && (0 == (xferred_bytes % p_midi_host->ep_out_max)) )
      {
        if ( usbh_edpt_claim(dev_addr, p_midi_host->ep_out) )
        {
          TU_ASSERT(usbh_edpt_xfer(dev_addr, p_midi_host->ep_out, NULL, 0));
        }
      }
    }
    if (tuh_midi_tx_cb)
    {
      tuh_midi_tx_cb(dev_addr);
    }
  }

  return true;
}

void midih_close(uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  if (p_midi_host == NULL)
    return;
  if (tuh_midi_umount_cb)
    tuh_midi_umount_cb(dev_addr, 0);
  tu_fifo_clear(&p_midi_host->rx_ff);
  tu_fifo_clear(&p_midi_host->tx_ff);
  p_midi_host->ep_in = 0;
  p_midi_host->ep_in_max = 0;
  p_midi_host->ep_out = 0;
  p_midi_host->ep_out_max = 0;
  p_midi_host->itf_num = 0;
  p_midi_host->num_cables_rx = 0;
  p_midi_host->num_cables_tx = 0;
  p_midi_host->dev_addr = 255; // invalid
  p_midi_host->configured = false;
  tu_memclr(&p_midi_host->stream_read, sizeof(p_midi_host->stream_read));
  tu_memclr(p_midi_host->stream_write, sizeof(p_midi_host->stream_write)*midih_limits.max_cables);
}

//--------------------------------------------------------------------+
// Enumeration
//--------------------------------------------------------------------+
bool midih_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
  (void) rhport;

  midih_interface_t *p_midi_host = get_midi_host(dev_addr);

  TU_VERIFY(p_midi_host != NULL);
  p_midi_host->last_xfer_result = XFER_RESULT_SUCCESS;
#if CFG_MIDI_HOST_DEVSTRINGS
  p_midi_host->num_string_indices = 0;
#endif
  TU_VERIFY(TUSB_CLASS_AUDIO == desc_itf->bInterfaceClass);
  // There can be just a MIDI interface or an audio and a MIDI interface. Only open the MIDI interface
  uint8_t const *p_desc = (uint8_t const *) desc_itf;
  uint16_t len_parsed = 0;
  if (AUDIO_SUBCLASS_CONTROL == desc_itf->bInterfaceSubClass)
  {
#if CFG_MIDI_HOST_DEVSTRINGS
    // Keep track of any string descriptor that might be here
    if (desc_itf->iInterface != 0)
        p_midi_host->all_string_indices[p_midi_host->num_string_indices++] = desc_itf->iInterface;
#endif
    // This driver does not support audio streaming. However, if this is the audio control interface
    // there might be a MIDI interface following it. Search through every descriptor until a MIDI
    // interface is found or the end of the descriptor is found
    while (len_parsed < max_len && (desc_itf->bInterfaceClass != TUSB_CLASS_AUDIO || desc_itf->bInterfaceSubClass != AUDIO_SUBCLASS_MIDI_STREAMING))
    {
      len_parsed += desc_itf->bLength;
      p_desc = tu_desc_next(p_desc);
      desc_itf = (tusb_desc_interface_t const *)p_desc;
    }

    TU_VERIFY(TUSB_CLASS_AUDIO == desc_itf->bInterfaceClass);
  }
  TU_VERIFY(AUDIO_SUBCLASS_MIDI_STREAMING == desc_itf->bInterfaceSubClass);
  len_parsed += desc_itf->bLength;

#if CFG_MIDI_HOST_DEVSTRINGS
  // Keep track of any string descriptor that might be here
  if (desc_itf->iInterface != 0)
      p_midi_host->all_string_indices[p_midi_host->num_string_indices++] = desc_itf->iInterface;
#endif
  p_desc = tu_desc_next(p_desc);
  TU_LOG1("MIDI opening Interface %u (addr = %u)\r\n", desc_itf->bInterfaceNumber, dev_addr);
  // Find out if getting the MIDI class specific interface header or an endpoint descriptor
  // or a class-specific endpoint descriptor
  // Jack descriptors or element descriptors must follow the cs interface header,
  // but this driver does not support devices that contain element descriptors

  // assume it is an interface header
  midi_desc_header_t const *p_mdh = (midi_desc_header_t const *)p_desc;
  TU_VERIFY((p_mdh->bDescriptorType == TUSB_DESC_CS_INTERFACE && p_mdh->bDescriptorSubType == MIDI_CS_INTERFACE_HEADER) || 
    (p_mdh->bDescriptorType == TUSB_DESC_CS_ENDPOINT && p_mdh->bDescriptorSubType == MIDI_CS_ENDPOINT_GENERAL) ||
    p_mdh->bDescriptorType == TUSB_DESC_ENDPOINT);

  uint8_t prev_ep_addr = 0; // the CS endpoint descriptor is associated with the previous endpoint descrptor
  p_midi_host->itf_num = desc_itf->bInterfaceNumber;
  tusb_desc_endpoint_t const* in_desc = NULL;
  tusb_desc_endpoint_t const* out_desc = NULL;
  while (len_parsed < max_len)
  {
    TU_VERIFY((p_mdh->bDescriptorType == TUSB_DESC_CS_INTERFACE) || 
      (p_mdh->bDescriptorType == TUSB_DESC_CS_ENDPOINT && p_mdh->bDescriptorSubType == MIDI_CS_ENDPOINT_GENERAL) ||
      p_mdh->bDescriptorType == TUSB_DESC_ENDPOINT);

    if (p_mdh->bDescriptorType == TUSB_DESC_CS_INTERFACE) {
      // The USB host doesn't really need this information unless it uses
      // the string descriptor for a jack or Element

      // assume it is an input jack
      midi_desc_in_jack_t const *p_mdij = (midi_desc_in_jack_t const *)p_desc;
      if (p_mdij->bDescriptorSubType == MIDI_CS_INTERFACE_HEADER)
      {
        TU_LOG2("Found MIDI Interface Header\r\b");
      }
      else if (p_mdij->bDescriptorSubType == MIDI_CS_INTERFACE_IN_JACK)
      {
        // Then it is an in jack. 
        TU_LOG2("Found in jack\r\n");
#if CFG_MIDI_HOST_DEVSTRINGS
        if (p_midi_host->next_in_jack < MAX_IN_JACKS)
        {
          p_midi_host->in_jack_info[p_midi_host->next_in_jack].jack_id = p_mdij->bJackID;
          p_midi_host->in_jack_info[p_midi_host->next_in_jack].jack_type = p_mdij->bJackType;
          p_midi_host->in_jack_info[p_midi_host->next_in_jack].string_index = p_mdij->iJack;
          ++p_midi_host->next_in_jack;
          // Keep track of any string descriptor that might be here
          if (p_mdij->iJack != 0)
            p_midi_host->all_string_indices[p_midi_host->num_string_indices++] = p_mdij->iJack;

        }
#endif
      }
      else if (p_mdij->bDescriptorSubType == MIDI_CS_INTERFACE_OUT_JACK)
      {
        // then it is an out jack
        TU_LOG2("Found out jack\r\n");
#if CFG_MIDI_HOST_DEVSTRINGS
        if (p_midi_host->next_out_jack < MAX_OUT_JACKS)
        {
          midi_desc_out_jack_t const *p_mdoj = (midi_desc_out_jack_t const *)p_desc;
          p_midi_host->out_jack_info[p_midi_host->next_out_jack].jack_id = p_mdoj->bJackID;
          p_midi_host->out_jack_info[p_midi_host->next_out_jack].jack_type = p_mdoj->bJackType;
          p_midi_host->out_jack_info[p_midi_host->next_out_jack].num_source_ids = p_mdoj->bNrInputPins;
          const struct associated_jack_s {
              uint8_t id;
              uint8_t pin;
          } *associated_jack = (const struct associated_jack_s *)(p_desc+6);
          int jack;
          for (jack = 0; jack < p_mdoj->bNrInputPins; jack++)
          {
            p_midi_host->out_jack_info[p_midi_host->next_out_jack].source_ids[jack] = associated_jack->id;
          }
          p_midi_host->out_jack_info[p_midi_host->next_out_jack].string_index = *(p_desc+6+p_mdoj->bNrInputPins*2);
          ++p_midi_host->next_out_jack;
          if (p_mdoj->iJack != 0)
            p_midi_host->all_string_indices[p_midi_host->num_string_indices++] = p_mdoj->iJack;
        }
#endif
      }
      else if (p_mdij->bDescriptorSubType == MIDI_CS_INTERFACE_ELEMENT)
      {
        // the it is an element;
    #if CFG_MIDI_HOST_DEVSTRINGS
        TU_LOG1("Found element; strings not supported\r\n");
    #else
        TU_LOG2("Found element\r\n");
    #endif
      }
      else
      {
        TU_LOG2("Unknown CS Interface sub-type %u\r\n", p_mdij->bDescriptorSubType);
        TU_VERIFY(false); // unknown CS Interface sub-type
      }
      len_parsed += p_mdij->bLength;
    }
    else if (p_mdh->bDescriptorType == TUSB_DESC_CS_ENDPOINT)
    {
      TU_LOG2("found CS_ENDPOINT Descriptor for %02x\r\n", prev_ep_addr);
      TU_VERIFY(prev_ep_addr != 0);
      // parse out the mapping between the device's embedded jacks and the endpoints
      // Each embedded IN jack is assocated with an OUT endpoint
      midi_cs_desc_endpoint_t const* p_csep = (midi_cs_desc_endpoint_t const*)p_mdh;
      if (tu_edpt_dir(prev_ep_addr) == TUSB_DIR_OUT)
      {
        TU_VERIFY(p_midi_host->ep_out == prev_ep_addr);
        TU_VERIFY(p_midi_host->num_cables_tx == 0);
        p_midi_host->num_cables_tx = p_csep->bNumEmbMIDIJack;
#if CFG_MIDI_HOST_DEVSTRINGS
        uint8_t jack;
        uint8_t max_jack = p_midi_host->num_cables_tx;
        if (max_jack > sizeof(p_midi_host->ep_out_associated_jacks))
        {
            max_jack = sizeof(p_midi_host->ep_out_associated_jacks);
        }
        for (jack = 0; jack < max_jack; jack++)
        {
          p_midi_host->ep_out_associated_jacks[jack] = p_csep->baAssocJackID[jack];
        }
#endif
      }
      else
      {
        TU_VERIFY(p_midi_host->ep_in == prev_ep_addr);
        TU_VERIFY(p_midi_host->num_cables_rx == 0);
        p_midi_host->num_cables_rx = p_csep->bNumEmbMIDIJack;
#if CFG_MIDI_HOST_DEVSTRINGS
        uint8_t jack;
        uint8_t max_jack = p_midi_host->num_cables_rx;
        if (max_jack > sizeof(p_midi_host->ep_in_associated_jacks))
        {
            max_jack = sizeof(p_midi_host->ep_in_associated_jacks);
        }
        for (jack = 0; jack < max_jack; jack++)
        {
          p_midi_host->ep_in_associated_jacks[jack] = p_csep->baAssocJackID[jack];
        }
#endif
      }
      len_parsed += p_csep->bLength;
      prev_ep_addr = 0;
    }
    else if (p_mdh->bDescriptorType == TUSB_DESC_ENDPOINT) {
      // parse out the bulk endpoint info
      tusb_desc_endpoint_t *p_ep = (tusb_desc_endpoint_t *)p_mdh;
      TU_LOG2("found ENDPOINT Descriptor %02x\r\n", p_ep->bEndpointAddress);
      if (p_ep->wMaxPacketSize > USBH_EPSIZE_BULK_MAX) {
        TU_LOG2("ENDPOINT %02x wMaxPacketSize shorted from %u to %u\r\n", p_ep->bEndpointAddress, p_ep->wMaxPacketSize, USBH_EPSIZE_BULK_MAX);
        p_ep->wMaxPacketSize = USBH_EPSIZE_BULK_MAX;
      }
      if (tu_edpt_dir(p_ep->bEndpointAddress) == TUSB_DIR_OUT)
      {
        TU_VERIFY(p_midi_host->ep_out == 0);
        TU_VERIFY(p_midi_host->num_cables_tx == 0);
        p_midi_host->ep_out = p_ep->bEndpointAddress;
        p_midi_host->ep_out_max = p_ep->wMaxPacketSize;
        if (p_midi_host->ep_out_max > midih_limits.midi_tx_buf)
          p_midi_host->ep_out_max = midih_limits.midi_tx_buf;
        prev_ep_addr = p_midi_host->ep_out;
        out_desc = p_ep;
      }
      else
      {
        TU_VERIFY(p_midi_host->ep_in == 0);
        TU_VERIFY(p_midi_host->num_cables_rx == 0);
        p_midi_host->ep_in = p_ep->bEndpointAddress;
        p_midi_host->ep_in_max = p_ep->wMaxPacketSize;
        if (p_midi_host->ep_in_max > midih_limits.midi_rx_buf)
          p_midi_host->ep_in_max = midih_limits.midi_rx_buf;
        prev_ep_addr = p_midi_host->ep_in;
        in_desc = p_ep;
      }
      len_parsed += p_mdh->bLength;
    }
    p_desc = tu_desc_next(p_desc);
    p_mdh = (midi_desc_header_t const *)p_desc;
  }
  TU_VERIFY((p_midi_host->ep_out != 0 && p_midi_host->num_cables_tx != 0) ||
            (p_midi_host->ep_in != 0 && p_midi_host->num_cables_rx != 0));
  TU_LOG1("MIDI descriptor parsed successfully\r\n");
#if CFG_MIDI_HOST_DEVSTRINGS
  // remove duplicate string indices
  for (int idx=0; idx < p_midi_host->num_string_indices; idx++) {
      for (int jdx = idx+1; jdx < p_midi_host->num_string_indices; jdx++) {
          while (jdx < p_midi_host->num_string_indices &&  p_midi_host->all_string_indices[idx] == p_midi_host->all_string_indices[jdx]) {
              // delete the duplicate by overwriting it with the last entry and reducing the number of entries by 1
              p_midi_host->all_string_indices[jdx] = p_midi_host->all_string_indices[p_midi_host->num_string_indices-1];
              --p_midi_host->num_string_indices;
          }
      }
  }
#endif
  if (in_desc)
  {
    TU_ASSERT(tuh_edpt_open(dev_addr, in_desc));
    // Some devices always return exactly the request length so transfers won't complete
    // unless you assume every transfer is the last one.
    // TODO usbh_edpt_force_last_buffer(dev_addr, p_midi_host->ep_in, true);
  }
  if (out_desc)
  {
    TU_ASSERT(tuh_edpt_open(dev_addr, out_desc));
  }
  p_midi_host->dev_addr = dev_addr;

  return true;
}

bool tuh_midi_configured(uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  return p_midi_host->configured;
}

bool midih_set_config(uint8_t dev_addr, uint8_t itf_num)
{
  TU_LOG2("Set config dev_addr=%u\r\n", dev_addr);
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);

  // Compound devices (e.g. a CDC+Audio+MIDI device) cause the host stack to
  // call this driver's set_config once per associated interface.  Without
  // this guard, the second call would re-queue the IN endpoint and assert in
  // usbh_edpt_xfer (because the EP is already busy), which leaves the host
  // stack with _dev0.enumerating stuck and breaks all later attaches.
  // Only do the one-time setup the first time we're called for this device.
  if (p_midi_host->configured)
  {
    TU_LOG2("midih_set_config: already configured for dev_addr=%u itf=%u (compound device)\r\n",
            dev_addr, itf_num);
    usbh_driver_set_config_complete(dev_addr, itf_num);
    return true;
  }
  p_midi_host->configured = true;

  TU_LOG2("Requesting poll IN endpoint %d\r\n", p_midi_host->ep_in);
  TU_ASSERT(usbh_edpt_xfer(p_midi_host->dev_addr, p_midi_host->ep_in, p_midi_host->epin_buf, p_midi_host->ep_in_max), 0);
  if (tuh_midi_mount_cb)
  {
    tuh_midi_mount_cb(dev_addr, p_midi_host->ep_in, p_midi_host->ep_out, p_midi_host->num_cables_rx, p_midi_host->num_cables_tx);
  }
  usbh_driver_set_config_complete(dev_addr, itf_num);
  return true;
}

//--------------------------------------------------------------------+
// Stream API
//--------------------------------------------------------------------+
static uint32_t write_flush(uint8_t dev_addr, midih_interface_t* midi)
{
  // No data to send
  if ( !tu_fifo_count(&midi->tx_ff) ) return 0;
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  if (p_midi_host->last_xfer_result != XFER_RESULT_SUCCESS) return 0;

  // skip if previous transfer not complete
  TU_VERIFY( usbh_edpt_claim(dev_addr, midi->ep_out) );

  uint16_t count = tu_fifo_read_n(&midi->tx_ff, midi->epout_buf, midi->ep_out_max);

  if (count)
  {
    TU_ASSERT( usbh_edpt_xfer(dev_addr, midi->ep_out, midi->epout_buf, count), 0 );
    return count;
  }else
  {
    // Release endpoint since we don't make any transfer
    usbh_edpt_release(dev_addr, midi->ep_out);
    return 0;
  }
}

bool tuh_midi_can_write_stream (uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  return (tu_fifo_remaining(&p_midi_host->tx_ff) >= 4);
}

uint32_t tuh_midi_stream_write (uint8_t dev_addr, uint8_t cable_num, uint8_t const* buffer, uint32_t bufsize)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  TU_VERIFY(cable_num < p_midi_host->num_cables_tx);
  TU_VERIFY(cable_num < midih_limits.max_cables);
  midi_stream_t *stream = &p_midi_host->stream_write[cable_num];

  uint32_t i = 0;
  uint8_t const CN_ = cable_num << 4;

  while ( (i < bufsize) && (tu_fifo_remaining(&p_midi_host->tx_ff) >= 4) )
  {
    uint8_t const data = buffer[i];
    i++;

    if (data >= MIDI_STATUS_SYSREAL_TIMING_CLOCK)
    {
        // real-time messages need to be sent right away
        midi_stream_t streamrt;
        streamrt.buffer[0] = CN_ + MIDI_CIN_1BYTE_DATA;
        streamrt.buffer[1] = data;
        streamrt.buffer[2] = 0;
        streamrt.buffer[3] = 0;

        uint16_t const count = tu_fifo_write_n(&p_midi_host->tx_ff, streamrt.buffer, 4);
        // FIFO overflown, since we already check fifo remaining. It is probably race condition
        TU_ASSERT(count == 4, i);
    }
    else if ( stream->index == 0 )
    {
      //------------- New event packet -------------//

      uint8_t const msg = data >> 4;
      uint8_t const _msg = (stream->buffer[0]) & 0x0F;
      stream->index = 2;
      //stream->buffer[1] = data; //first check if its a running status byte, then update
      stream->total = 4;

      // Check to see if we're still in a SysEx transmit.
      if ( _msg == MIDI_CIN_SYSEX_START)
      {
        stream->buffer[1] = data;

        if ( data == MIDI_STATUS_SYSEX_END )
        {
          stream->buffer[0] = CN_ + MIDI_CIN_SYSEX_END_1BYTE;
          stream->total = 2;
        }
      }
      else if (msg < 0x8 && _msg >= 0x8 && _msg < 0xF)   //Running Status ?
      {
        //stream->buffer[0] leave;
        //stream->buffer[1] leave;
        stream->buffer[2] = data;

        if (_msg < 0xC || _msg == 0xE)
        {
            stream->index = 3;
        }
        else    //if (_msg < 0xF)
        {
            stream->index = 3;
            stream->total = 3;
        }
      }
      else if ( (msg >= 0x8 && msg <= 0xB) || msg == 0xE )
      {
        // Channel Voice Messages
        stream->buffer[1] = data;
        stream->buffer[0] = CN_ + msg;
      }
      else if ( msg == 0xC || msg == 0xD)
      {
        // Channel Voice Messages, two-byte variants (Program Change and Channel Pressure)
        stream->buffer[1] = data;
        stream->buffer[0] = CN_ + msg;
        stream->total = 3;
      }
      else if ( msg == 0xf )
      {
        // System message
        stream->buffer[1] = data;

        if ( data == MIDI_STATUS_SYSEX_START )
        {
          stream->buffer[0] = CN_ + MIDI_CIN_SYSEX_START;
        }
        else if ( data == MIDI_STATUS_SYSCOM_TIME_CODE_QUARTER_FRAME || data == MIDI_STATUS_SYSCOM_SONG_SELECT )
        {
          stream->buffer[0] = CN_ + MIDI_CIN_SYSCOM_2BYTE;
          stream->total = 3;
        }
        else if ( data == MIDI_STATUS_SYSCOM_SONG_POSITION_POINTER )
        {
          stream->buffer[0] = CN_ + MIDI_CIN_SYSCOM_3BYTE;
        }
        else        //for example, MIDI_STATUS_SYSCOM_TUNE_REQUEST
        {
          stream->buffer[0] = CN_ + MIDI_CIN_1BYTE_DATA;
          stream->total = 2;
        }
      }
      else
      {
        // Pack individual bytes if we don't support packing them into words.
        stream->buffer[1] = data;
        stream->buffer[0] = CN_ + 0xF;
        stream->index = 2;
        stream->total = 2;
      }
    }   //End of: if (stream->index == 0)
    else
    {
      //------------- On-going (buffering) packet -------------//

      TU_ASSERT(stream->index < 4, i);
      stream->buffer[stream->index] = data;
      stream->index++;
      // See if this byte ends a SysEx.
      if ( stream->buffer[0] == CN_ + MIDI_CIN_SYSEX_START && data == MIDI_STATUS_SYSEX_END )
      {
        stream->buffer[0] = CN_ + MIDI_CIN_SYSEX_START + (stream->index - 1);   //END +1/+2/+3 Bytes
        stream->total = stream->index;
      }
    }

    // Send out packet
    if ( stream->index >= 2 && stream->index >= stream->total )
    {
      //zeroes unused bytes
      for(uint8_t idx = stream->total; idx < 4; idx++) stream->buffer[idx] = 0;
      TU_LOG3_MEM(stream->buffer, 4, 2);

      uint16_t const count = tu_fifo_write_n(&p_midi_host->tx_ff, stream->buffer, 4);

      stream->index = 0;

      // FIFO overflown, since we already check fifo remaining. It is probably race condition
      TU_ASSERT(count == 4, i);
    }
  }

  return i;
}


bool tuh_midi_packet_write (uint8_t dev_addr, uint8_t const packet[4])
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);

  if (tu_fifo_remaining(&p_midi_host->tx_ff) < 4)
  {
    return false;
  }

  tu_fifo_write_n(&p_midi_host->tx_ff, packet, 4);

  return true;
}

uint32_t tuh_midi_stream_flush( uint8_t dev_addr )
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);

  uint32_t bytes_flushed = 0;
  if (!usbh_edpt_busy(p_midi_host->dev_addr, p_midi_host->ep_out))
  {
    bytes_flushed = write_flush(dev_addr, p_midi_host);
  }
  return bytes_flushed;
}
//--------------------------------------------------------------------+
// Helper
//--------------------------------------------------------------------+
uint8_t tuh_midih_get_num_tx_cables (uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  TU_VERIFY(p_midi_host->ep_out != 0); // returns 0 if fails
  return p_midi_host->num_cables_tx;
}

uint8_t tuh_midih_get_num_rx_cables (uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  TU_VERIFY(p_midi_host->ep_in != 0); // returns 0 if fails
  return p_midi_host->num_cables_rx;
}

bool tuh_midi_packet_read (uint8_t dev_addr, uint8_t packet[4])
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  TU_VERIFY(tu_fifo_count(&p_midi_host->rx_ff) >= 4);
  return tu_fifo_read_n(&p_midi_host->rx_ff, packet, 4) == 4;
}

uint32_t tuh_midi_stream_read (uint8_t dev_addr, uint8_t *p_cable_num, uint8_t *p_buffer, uint16_t bufsize)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  uint32_t bytes_buffered = 0;
  TU_ASSERT(p_cable_num);
  TU_ASSERT(p_buffer);
  TU_ASSERT(bufsize);
  uint8_t one_byte;
  if (!tu_fifo_peek(&p_midi_host->rx_ff, &one_byte))
  {
    return 0;
  }
  *p_cable_num = (one_byte >> 4) & 0xf;
  uint32_t nread = tu_fifo_read_n(&p_midi_host->rx_ff, p_midi_host->stream_read.buffer, 4);
  static uint16_t cable_sysex_in_progress; // bit i is set if received MIDI_STATUS_SYSEX_START but not MIDI_STATUS_SYSEX_END
  while (nread == 4 && bytes_buffered < bufsize)
  {
    *p_cable_num=(p_midi_host->stream_read.buffer[0] >> 4) & 0x0f;
    uint8_t bytes_to_add_to_stream = 0;
    if (*p_cable_num < p_midi_host->num_cables_rx)
    {
      // ignore the CIN field; too many devices out there encode this wrong
      uint8_t status = p_midi_host->stream_read.buffer[1];
      uint16_t cable_mask = (uint16_t) (1 << *p_cable_num);
      if (status <= MIDI_MAX_DATA_VAL || status == MIDI_STATUS_SYSEX_START)
      {
        if (status == MIDI_STATUS_SYSEX_START)
        {
          cable_sysex_in_progress |= cable_mask;
        }
        // only add the packet if a sysex message is in progress
        if (cable_sysex_in_progress & cable_mask)
        {
          ++bytes_to_add_to_stream;
          uint8_t idx;
          for (idx = 2; idx < 4; idx++)
          {
            if (p_midi_host->stream_read.buffer[idx] <= MIDI_MAX_DATA_VAL)
            {
              ++bytes_to_add_to_stream;
            }
            else if (p_midi_host->stream_read.buffer[idx] == MIDI_STATUS_SYSEX_END)
            {
              ++bytes_to_add_to_stream;
              cable_sysex_in_progress &= (uint16_t) ~cable_mask;
              idx = 4; // force the loop to exit; I hate break statements in loops
            }
          }
        }
      }
      else if (status < MIDI_STATUS_SYSEX_START)
      {
        // then it is a channel message either three bytes or two
        uint8_t fake_cin = (status & 0xf0) >> 4;
        switch (fake_cin)
        {
          case MIDI_CIN_NOTE_OFF:
          case MIDI_CIN_NOTE_ON:
          case MIDI_CIN_POLY_KEYPRESS:
          case MIDI_CIN_CONTROL_CHANGE:
          case MIDI_CIN_PITCH_BEND_CHANGE:
            bytes_to_add_to_stream = 3;
            break;
          case MIDI_CIN_PROGRAM_CHANGE:
          case MIDI_CIN_CHANNEL_PRESSURE:
            bytes_to_add_to_stream = 2;
            break;
          default:
            break; // Should not get this
        }
        cable_sysex_in_progress &= (uint16_t)~cable_mask;
      }
      else if (status < MIDI_STATUS_SYSREAL_TIMING_CLOCK)
      {
        switch (status)
        {
          case MIDI_STATUS_SYSCOM_TIME_CODE_QUARTER_FRAME:
          case MIDI_STATUS_SYSCOM_SONG_SELECT:
            bytes_to_add_to_stream = 2;
            break;
          case MIDI_STATUS_SYSCOM_SONG_POSITION_POINTER:
            bytes_to_add_to_stream = 3;
            break;
          case MIDI_STATUS_SYSCOM_TUNE_REQUEST:
          case MIDI_STATUS_SYSEX_END:
            bytes_to_add_to_stream = 1;
            break;
          default:
            break;
          cable_sysex_in_progress &= (uint16_t)~cable_mask;
        }
      }
      else
      {
        // Real-time message: can be inserted into a sysex message,
        // so do don't clear cable_sysex_in_progress bit
        bytes_to_add_to_stream = 1;
      }
    }
    uint8_t idx;
    for (idx = 1; idx <= bytes_to_add_to_stream; idx++)
    {
      *p_buffer++ = p_midi_host->stream_read.buffer[idx];
    }
    bytes_buffered += bytes_to_add_to_stream;
    nread = 0;
    if (tu_fifo_peek(&p_midi_host->rx_ff, &one_byte))
    {
      uint8_t new_cable = (one_byte >> 4) & 0xf;
      if (new_cable == *p_cable_num)
      {
        // still on the same cable. Continue reading the stream
        nread = tu_fifo_read_n(&p_midi_host->rx_ff, p_midi_host->stream_read.buffer, 4);
      }
    }
  }

  return bytes_buffered;
}

uint8_t tuh_midi_get_num_rx_cables(uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  uint8_t num_cables = 0;
  if (p_midi_host)
  {
    num_cables = p_midi_host->num_cables_rx;
  }
  return num_cables;
}

uint8_t tuh_midi_get_num_tx_cables(uint8_t dev_addr)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  uint8_t num_cables = 0;
  if (p_midi_host)
  {
    num_cables = p_midi_host->num_cables_tx;
  }
  return num_cables;
}

#if CFG_MIDI_HOST_DEVSTRINGS
static uint8_t find_string_index(midih_interface_t *ptr, uint8_t jack_id)
{
  uint8_t index = 0;
  uint8_t assoc;
  for (assoc = 0; index == 0 && assoc < ptr->next_in_jack; assoc++)
  {
    if (jack_id == ptr->in_jack_info[assoc].jack_id)
    {
      index = ptr->in_jack_info[assoc].string_index;
    }
  }
  for (assoc = 0; index == 0 && assoc < ptr->next_out_jack; assoc++)
  {
    if (jack_id == ptr->out_jack_info[assoc].jack_id)
    {
      index = ptr->out_jack_info[assoc].string_index;
    }
  }
  return index;
}
#endif

#if CFG_MIDI_HOST_DEVSTRINGS
uint8_t tuh_midi_get_rx_cable_istrings(uint8_t dev_addr, uint8_t* istrings, uint8_t max_istrings)
{
  uint8_t nstrings = 0;
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  nstrings = p_midi_host->num_cables_rx;
  if (nstrings > max_istrings)
  {
      nstrings = max_istrings;
  }
  uint8_t jack;
  for (jack=0; jack<nstrings; jack++)
  {
    uint8_t jack_id = p_midi_host->ep_in_associated_jacks[jack];
    istrings[jack] = find_string_index(p_midi_host, jack_id);
  }
  return nstrings;
}

uint8_t tuh_midi_get_tx_cable_istrings(uint8_t dev_addr, uint8_t* istrings, uint8_t max_istrings)
{
  uint8_t nstrings = 0;
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  nstrings = p_midi_host->num_cables_tx;
  if (nstrings > max_istrings)
  {
      nstrings = max_istrings;
  }
  uint8_t jack;
  for (jack=0; jack<nstrings; jack++)
  {
    uint8_t jack_id = p_midi_host->ep_out_associated_jacks[jack];
    istrings[jack] = find_string_index(p_midi_host, jack_id);
  }
  return nstrings;
}

uint8_t tuh_midi_get_all_istrings(uint8_t dev_addr, const uint8_t** istrings)
{
  midih_interface_t *p_midi_host = get_midi_host(dev_addr);
  TU_VERIFY(p_midi_host != NULL);
  uint8_t nstrings = p_midi_host->num_string_indices;
  if (nstrings)
    *istrings = p_midi_host->all_string_indices;
  return nstrings;
}
#endif
#endif
//============================================================================
// END vendored rppicomidi usb_midi_host.c
//============================================================================

//============================================================================
// BEGIN vendored rppicomidi usb_midi_host_app_driver.c
//
// Verbatim apart from: .name is initialised unconditionally, as
// usbh_class_driver_t::name is not debug-only in the Pico SDK's TinyUSB;
// and its #includes are hoisted above the extern "C" block.
//============================================================================
/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 rppicomidi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#if (TUSB_OPT_HOST_ENABLED)



// Install the USB MIDI Host class driver
usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count)
{
  static usbh_class_driver_t host_driver = {
    .name = "MIDIH",
    .init=midih_init,
    .deinit=midih_deinit,
    .open=midih_open,
    .set_config=midih_set_config,
    .xfer_cb = midih_xfer_cb,
    .close = midih_close };
  *driver_count = 1;
  return &host_driver;
}
#endif
//============================================================================
// END vendored rppicomidi usb_midi_host_app_driver.c
//============================================================================

} // extern "C"

#endif // EIGHTMU_NOUSBDRIVER

EightMU *EightMU::instance = nullptr;

void EightMU::Core1Entry()
{
	board_init();
	tusb_init();

	while (1)
	{
		instance->Poll();
	}
}

// Forget everything we know about the attached device, so that the whole
// identify/query sequence runs again when one appears.
void EightMU::ResetConnection()
{
	connected = false;
	fwMajor = fwMinor = fwPoint = 0;
	for (int i = 0; i < numButtons; i++)
	{
		buttons[i] = false;
	}
	// Fader values are deliberately kept, so that outputs derived from them
	// hold their last position rather than jumping to zero on a disconnect.

	identifyTries = 0;
	faderQueryTries = 0;
	faderQueryDone = false;
	faderReplyGot = false;
	blinkDisabled = false;
	ledPrimed = false;
}

void EightMU::OnMount(uint8_t addr)
{
	if (devAddr != 0) return; // already have a device; ignore any others

	devAddr = addr;
	ResetConnection();
	recoveryArmed = false;
}

void EightMU::OnUnmount(uint8_t addr)
{
	if (addr != devAddr) return;

	devAddr = 0;
	ResetConnection();
	recoveryCheckUs = time_us_32() + recoveryDebounceUs;
	recoveryArmed = true;
}

void EightMU::OnMIDIBytes(uint8_t addr, const uint8_t *msg, int size)
{
	if (addr != devAddr || size < 1) return;

	if (msg[0] == 0xF0) // SysEx
	{
		// The rx parser splits on status bytes, so the trailing 0xF7 arrives
		// as a message of its own and is not counted in size here.
		if (size < 5) return;
		if (msg[1] != 0x7D || msg[2] != 0x00 || msg[3] != 0x00) return;

		if (msg[4] == 0x20 && size >= 10 && msg[5] == deviceIndex)
		{
			// Identify reply: device index, then firmware version
			fwMajor = msg[6];
			fwMinor = msg[7];
			fwPoint = msg[8];
			connected = true;
		}
		else if (msg[4] == 0x12 && size >= 13)
		{
			// Reply to a fader position query
			for (int i = 0; i < numFaders; i++)
			{
				ccValues[i] = int32_t(msg[5 + i] & 0x7F) << 5;
			}
			faderReplyGot = true;
		}
		return;
	}

	// Only interpret channel messages once we know this really is an 8mu
	if (!connected || size < 3) return;

	uint8_t command = msg[0] & 0xF0;

	if (command == 0xB0)
	{
		uint8_t cc = msg[1];
		if (cc >= firstCC && cc < firstCC + numCCs)
		{
			ccValues[cc - firstCC] = int32_t(msg[2] & 0x7F) << 5;
		}
	}
	else if (command == 0x90 || command == 0x80)
	{
		for (int i = 0; i < numButtons; i++)
		{
			if (msg[1] == buttonNotes[i])
			{
				buttons[i] = (command == 0x90) && (msg[2] > 0);
				break;
			}
		}
	}
}

// Write to the outgoing MIDI stream without flushing it.
// Returns false if the device went away part-way through.
bool EightMU::MIDIQueue(const uint8_t *data, uint32_t size)
{
	if (devAddr == 0 || !tuh_midi_configured(devAddr)) return false;

	uint32_t sent = 0;
	int stalls = 0;
	while (sent < size)
	{
		sent += tuh_midi_stream_write(devAddr, 0, data + sent, size - sent);

		if (sent < size)
		{
			// Buffer full: push what we have and let the stack run.
			// Messages here are only a few bytes, so this is rare.
			tuh_midi_stream_flush(devAddr);
			tuh_task();
			if (devAddr == 0 || ++stalls > 100) return false;
		}
	}
	return true;
}

void EightMU::MIDIFlush()
{
	if (devAddr != 0) tuh_midi_stream_flush(devAddr);
}

// Send one SysEx message, if enough time has passed since the last one.
bool EightMU::SendSysEx(const uint8_t *data, uint32_t size, uint32_t now)
{
	if (haveSentSysEx && now - lastSysExUs < sysExGapUs) return false;
	if (!MIDIQueue(data, size)) return false;

	MIDIFlush();
	haveSentSysEx = true;
	lastSysExUs = now;
	sysExSent = true;
	return true;
}

void EightMU::Service()
{
	uint32_t now = time_us_32();
	sysExSent = false;

	if (devAddr == 0)
	{
		RecoveryWatchdog(now);
		return;
	}

	if (!tuh_midi_configured(devAddr)) return;

	// Identify the device, and in doing so make it load its default
	// configuration, so that the CC and note numbers below are the ones
	// it actually uses.
	if (!connected)
	{
		if (identifyTries < identifyMaxTries &&
		    (identifyTries == 0 || now - lastIdentifyUs >= identifyRetryUs))
		{
			if (SendSysEx(sysExIdentify, sizeof(sysExIdentify), now))
			{
				identifyTries++;
				lastIdentifyUs = now;
			}
		}
		return; // nothing else worth doing until we know what this is
	}

	// Ask a newly connected 8mu where its faders are.  The 8mu does send
	// these at power-up, but that is before we have enumerated it, so
	// without this the fader values are wrong until each one is moved.
	if (!faderQueryDone)
	{
		if (faderReplyGot || faderQueryTries >= faderQueryMaxTries)
		{
			faderQueryDone = true; // got them, or gave up
		}
		else if (faderQueryTries == 0 || now - lastFaderQueryUs >= faderQueryRetryUs)
		{
			if (SendSysEx(sysExFaderQuery, sizeof(sysExFaderQuery), now))
			{
				faderQueryTries++;
				lastFaderQueryUs = now;
			}
		}
	}

	ServiceLeds(now);
}

void EightMU::ServiceLeds(uint32_t now)
{
	if (ledsWanted && !blinkDisabled)
	{
		// Take the LEDs over.  The 8mu masks LEDs we set while it is
		// flashing them itself, so this has to land first.
		if (SendSysEx(sysExBlinkOff, sizeof(sysExBlinkOff), now))
		{
			blinkDisabled = true;
			lastBlinkOffUs = now;
			// A freshly connected 8mu has all its LEDs off, so send the
			// full state rather than only what has changed since.
			ledPrimed = false;
		}
	}
	else if (ledsWanted && now - lastBlinkOffUs >= blinkOffResendUs)
	{
		if (SendSysEx(sysExBlinkOff, sizeof(sysExBlinkOff), now))
		{
			lastBlinkOffUs = now;
		}
	}
	else if (!ledsWanted && blinkDisabled)
	{
		// Hand the LEDs back, turning off any we had lit
		if (SendSysEx(sysExBlinkOn, sizeof(sysExBlinkOn), now))
		{
			for (int i = 0; i < numLeds; i++)
			{
				uint8_t off[3] = {0x80, uint8_t(i), 0};
				if (!MIDIQueue(off, 3)) break;
			}
			MIDIFlush();
			blinkDisabled = false;
		}
	}

	// Keep LED updates out of any transfer carrying SysEx
	if (!blinkDisabled || sysExSent) return;
	if (now - lastLedUpdateUs < ledUpdateUs) return;
	lastLedUpdateUs = now;

	bool any = false;
	for (int i = 0; i < numLeds; i++)
	{
		uint8_t v = ledDesired[i];
		if (ledPrimed && v == ledSent[i]) continue;

		// Note number is the LED index, and must stay in 0-7: the 8mu does
		// not range-check it.  Velocity is brightness, which the 8mu squares
		// for PWM, giving the same curve as ComputerCard::LedBrightness.
		uint8_t note[3] = {0x90, uint8_t(i), v};
		if (!MIDIQueue(note, 3)) return;
		ledSent[i] = v;
		any = true;
	}
	if (any) MIDIFlush();
	ledPrimed = true;
}

void EightMU::RecoveryWatchdog(uint32_t now)
{
	if (!recoveryArmed) return;
	if (int32_t(now - recoveryCheckUs) < 0) return;

	recoveryArmed = false;
	if (devAddr != 0) return; // something attached in the meantime

	if (haveRecovered && now - lastRecoveryUs < minRecoveryIntervalUs) return;

	haveRecovered = true;
	lastRecoveryUs = now;

	tuh_deinit(0);
	devAddr = 0;
	ResetConnection();
	tusb_init();
}


// The four callbacks used by the rppicomidi usb_midi_host driver

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx)
{
	(void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;

	EightMU *mu = EightMU::Instance();
	if (mu) mu->OnMount(dev_addr);
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)instance;

	EightMU *mu = EightMU::Instance();
	if (mu) mu->OnUnmount(dev_addr);
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
	EightMU *mu = EightMU::Instance();
	if (!mu || num_packets == 0) return;

	uint8_t cableNum;
	uint8_t buffer[64];

	while (1)
	{
		uint32_t bytesRead = tuh_midi_stream_read(dev_addr, &cableNum, buffer, sizeof(buffer));
		if (bytesRead == 0) return;

		// Split the byte stream into messages, each starting at a status
		// byte (top bit set) and running to the byte before the next one.
		// A SysEx message therefore arrives in one piece, with its closing
		// 0xF7 following as a one-byte message that is ignored.
		uint8_t *bufPtr = buffer;
		int32_t remaining = int32_t(bytesRead);
		while (remaining > 0)
		{
			uint8_t *next = bufPtr;
			do
			{
				next++;
				remaining--;
			} while (remaining > 0 && !(*next & 0x80));

			mu->OnMIDIBytes(dev_addr, bufPtr, int(next - bufPtr));
			bufPtr = next;
		}
	}
}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
	(void)dev_addr;
}

#endif // EIGHTMU_NOIMPL

#endif // EIGHTMU_H

#endif // __cplusplus
