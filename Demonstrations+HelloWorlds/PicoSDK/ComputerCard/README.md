# ComputerCard
ComputerCard is a  [MIT licensed](https://opensource.org/license/mit) header-only C++ library, that
manages the hardware aspects of the [Music Thing Modular Workshop
System Computer](https://www.musicthing.co.uk/workshopsystem/).

It aims to present a very simple C++ framework for card programmers to use all the hardware features of the Computer, within a callback at a fixed 48kHz audio sample rate.

ComputerCard was designed to work with the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk) but also works with the Arduino environment using the earlephilhower RP2040 board [as described below](#arduino-ide)


Behind the scenes, the ComputerCard class:
- Manages the ADC and external multiplexer to collect analogue samples from audio inputs, CV inputs, knobs and switch.
- Applies smoothing and some simple hysteresis to knobs and CV, to provide relatively low-noise values updated every audio frame.
- Signals the external DAC to drive audio outputs.
- Manages PWM signals for CV output and brightness control of the six LEDs.
- If enabled, determines whether jacks are connected to the six input sockets by driving the 'normalisation probe' pin and detecting its signal on the inputs.
- Reads EEPROM calibration, if set, for calibrated MIDI note CV outputs.

## Usage:
Basic usage is intended to be extremely simple. For example, here is complete code for a Sample and Hold card:

```c++
#include "ComputerCard.h"

// Derive a new class from ComputerCard
class SampleAndHold : public ComputerCard
{
public:
	virtual void ProcessSample() // executed every sample, at 48kHz
	{
		// Update audio out 1 with value from audio in 1,
		// only if rising edge seen on pulse 1 input
		if (PulseIn1RisingEdge()) AudioOut1(AudioIn1());
	}
};

int main()
{
	// Run chip at multiple of 48MHz
	set_sys_clock_khz(144000, true);

	// Create and run our new Sample and Hold
	SampleAndHold sh;
	sh.Run();
}
```
  
More generally, the process is:
1. Add `#include "ComputerCard.h"` to each source file that uses it. If linking more than one source file, use `#define COMPUTERCARD_NOIMPL` before this include in all but one file, so that the implementation is only included once.

2. Derive a new class (such as the `SampleAndHold` class above) from the abstract `ComputerCard` class, representing the particular card being written.

3. Do any card-specific setup in the constructor of the derived class.

4. Override the pure virtual `ComputerCard::ProcessSample()` method to implement the per-sample functionality required for the particular card. `ComputerCard::ProcessSample()` is called at a fixed 48kHz sample rate.

5. To minimise ADC input noise, at the start of `main()` use `set_sys_clock_khz(144000, true);` to run the RP2040 at 144MHz, or another multiple of 48MHz.

6. Now, create an instance of the new derived class (for example, in `main()`)

7. If the normalisation probe is used, call the `ComputerCard::EnableNormalisationProbe()` method on this instance.

8. Call the `ComputerCard::Run()` method on this instance to start audio processing. `ComputerCard::Run()` is blocking (never returns).

### Examples
ComputerCard contains several examples in the `examples/` directory.
For beginners just starting with ComputerCard, the first example to look at is `passthrough` to introduce the basic functions, followed by `sample_and_hold` for typical usage of these in a 'real' card.

- `calibrated_cv_out` — example of using calibration data to output precise voltages/pitches 
- `calibrated_input` — 1V/oct sine VCO, using calibration data to read precise voltages on the audio inputs. Audio In 1 sets pitch (0V = A4), and the measured voltage is copied to both CV outputs. LED 5 lights if the inputs are uncalibrated.
- `card_info` — displays properties of the program card itself on the LEDs: eight bits of the unique card ID as four 2-bit brightness levels on the top four LEDs, and whether the flash chip is larger than 2MB on the bottom two.
- `eightmu` — example of using a Music Thing Modular 8mu USB MIDI controller as an input to a card, through the `EightMU` class ([see below](#eightmu)). Requires Computer 1.1.0 Hardware.
- `hid_keyboard_mouse` — example of connecting a mouse or keyboard to the Computer, using USB HID (Human Interface Device) host mode
- `midi_device` — example of USB MIDI being used alongside ComputerCard. The MTM Computer acts as a USB device, to allow it to be connected to a (laptop/desktop) computer or a phone/tablet. Sends Computer knob values to the USB host as CC messages.
- `midi_host` — example of USB MIDI being used alongside ComputerCard. The MTM Computer acts as a USB host, to allow it to be connected to USB MIDI devices such as keyboards/controllers/etc.
- `midi_device_host` — example of USB MIDI being used alongside ComputerCard. At startup, the MTM computer determines the type of USB port it is connected to, and becomes either a host or device as appropriate. Requires Computer 1.1.0 Hardware for host mode.
- `normalisation_probe` — minimal example of patch cable detection. LEDs are lit when corresponding sockets have a jack plugged in.
- `passthrough` — simple demonstration of using the all the jacks and knobs, switch and LEDs.
- `sample_and_hold` — dual sample and hold, demonstrating jacks, normalisation probe and pseudo-random numbers
- `sample_upload` — an interface for users to upload audio samples (in WAV file format) to a Computer card, and play these back
- `second_core` — demonstration of using the second RP2040 core for more CPU-intensive processing than is possible at the 48kHz sample rate
- `sine_wave_float` — 440Hz sine wave generator, using floating-point numbers
- `sine_wave_lookup` — 440Hz sine wave generator, demonstrating scanning and linear interpolation of a lookup table using integer arithmetic 
- `usb_detect` — Displays on the LEDs whether the USB port on the MTM Computer is acting as a 'downstream facing port' (MTM Computer is USB Host), or 'upstream facing port' (MTM Computer is USB device). Requires Computer 1.1.0 Hardware. 
- `usb_serial` — Outputs debugging information from a ComputerCard through the USB serial connection
- `usb_msc_host` — Plays and records WAV files on a USB stick. Needs TinyUSB 0.21 or later for adequate speed.
- `web_interface` — Demonstrates bidirectional SysEx communication with an HTML web interface

### Notes
- Make sure execution of `ComputerCard::ProcessSample` always runs quickly enough that it has returned before the next execution begins (1/48kHz = ~20μs). (See the [guidance below](#programming) on achieving this)
- While multiple ComputerCard objects can be created and used sequentially, only one instance of a ComputerCard can be active (using `Run()`) at any one time.

<a id="eightmu"></a>
### 8mu support
`EightMU.h` provides an `EightMU` class, for cards that want to use a [Music Thing Modular 8mu](https://www.musicthing.co.uk/8mu/) USB MIDI controller as an input. It is independent of ComputerCard, and is intended to be added as a member of a card:

```c++
#include "ComputerCard.h"
#include "EightMU.h"

class MyCard : public ComputerCard
{
	EightMU mu;
public:
	MyCard()
	{
		mu.Start(); // claims the second core, and runs USB MIDI host there
	}

	virtual void ProcessSample()
	{
		CVOut1(mu.Fader(0) - 2048);  // faders, 0 to 4064
		AudioOut1(mu.Pitch());       // accelerometer axes, -2032 to 2032
		mu.SetLed(0, mu.Fader(0));   // 8mu LED brightness, 0 to 4095
	}
};
```

`EightMU` gives access to the 8mu's eight faders (`Fader`, outputs 0 to 4064, similar to range of `KnobVal`), its motion sensing (`Pitch`, `Roll`, `Yaw`, `Flip`, -2032 to 2032, similar to range of the audio and CV jacks) and its four buttons (`Button`), and sets the brightness of the 8mu's eight LEDs (`SetLed`, `LedOn`, `LedOff`, `ReleaseLeds`). `Connected()` reports whether an 8mu is attached. All of these are listed in the [EightMU reference](#eightmu-reference) below.

All of the USB MIDI host handling is done inside the class, on the second RP2040 core. This means:
- The 8mu is a USB device, so the Computer must be acting as a USB host. That requires Computer 1.1.0 Hardware, with nothing plugged into the Computer's own USB socket.
- Cards using `EightMU::Start()` cannot use the second core for anything else. Cards that need it can instead call `EightMU::Poll()` repeatedly from their own second-core loop.
- `EightMU.h` is a single header: it carries the USB MIDI host driver ([rppicomidi/usb_midi_host](https://github.com/rppicomidi/usb_midi_host)) and the host-mode TinyUSB configuration itself, so a card using it needs no other source files. Its build needs only the TinyUSB host libraries, and `CFG_TUSB_CONFIG_FILE` pointing at the header, which is what lets it serve as `tusb_config.h`:
```cmake
target_link_libraries(mycard pico_multicore tinyusb_host tinyusb_board)
target_compile_definitions(mycard PRIVATE CFG_TUSB_CONFIG_FILE="EightMU.h")
```
  Instead of that definition, a one-line `tusb_config.h` containing `#include "EightMU.h"` next to the card's `main.cpp` also works. `#define EIGHTMU_NOUSBDRIVER` before the include leaves the driver and configuration out, for cards that supply their own.

Note that when an 8mu connects, `EightMU` sends it a SysEx message that makes it load its default configuration, so that the fader, accelerometer and button assignments are the ones the class expects. As a side effect, the 8mu switches to bank 1, and its bank buttons stop working until it is power-cycled.

### Limitations / potential future improvements
- There is no way to configure CV/knob smoothing filters.
- There is no way to change the sample rate
- There is no built-in way to process blocks of samples, rather than individual samples.

<a id="pico-sdk"></a>
## Using the RPi Pico SDK (Linux command line)
- Clone and install the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- Set the `PICO_SDK_PATH` environment variable to the location at with the Pico SDK is installed
- Clone this repository
- Create a subdirectory `build` within this repository, and `cd` into it
- Run `cmake ..`
- Run `make`
- The example cards in `examples/` will be built, and all the `uf2` files all placed in the `build/` directory
- Flash one of these `uf2` files to the Workshop System Computer in the usual way

You can create your own projects using ComputerCard by
- creating a new directory and source file in `examples/` and adding the appropriate `add_example` line to `CMakeLists.txt`.
- or, this being a single-header library, by just copying `ComputerCard.h` into your own Pico SDK project.

<a id="vscode"></a>
## Using Visual Studio Code (with RPi Pico plugin)
Disclaimer: the instructions below appear to work but are likely far from optimal (I am not a VSCode user myself)
- Install [Visual Studio Code](https://code.visualstudio.com/) 
- Install the [RPi Pico VSCode plugin](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
- In VSCode, click the Raspberry Pi Pico icon on the Activity Bar (by default, the bottom icon on the bar of icons going down the left side of the screen)
- Click 'Import Project' in the 'Raspberry Pi Pico Project: Quick Access' bar that appears. Select this ComputerCard directory and continue to import the project
- Go to the CMake icon in the Activity Bar.
- Mouseover 'PROJECT OUTLINE' and click the 'Configure all' button (looks like a piece of paper with arrow pointing right out of it). The example projects should appear within the 'Project Outline' area.
- Build all of these by hovering over 'PROJECT OUTLINE' again and clicking the Build All button (looks like an arrow pointing into a container of dots)


<a id="arduino-ide"></a>
## Using the Arduino IDE

### Installation

- Install Arduino IDE: (https://www.arduino.cc/en/software)[https://www.arduino.cc/en/software]
- Install Arduino Pico by adding it as a new board: (https://github.com/earlephilhower/arduino-pico?tab=readme-ov-file#installation)[https://github.com/earlephilhower/arduino-pico?tab=readme-ov-file#installation]

### Create a new sketch

- Open Arduino IDE and create a new sketch and save it
- In the Arduino IDE, select Raspberry Pi Pico as your board
- Download ComputerCard.h file from this ComputerCard repository
- Store this file in the directory of your sketch (there should be an .ino file with your sketch name in this directory).

### Code
- As a start you can just copy the Sample&Hold example from the following file into your sketch (the .ino file): https://raw.githubusercontent.com/TomWhitwell/Workshop_Computer/refs/heads/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard/examples/sample_and_hold/main.cpp
- Replace the `main` function with the following code:
```c++
SampleAndHold sh;

void setup() {
  sh.EnableNormalisationProbe();
}

void loop() {
  sh.Run();
}
```
- Note: The `Run()` method is blocking (never returns), and therefore takes over control from the Arduino `loop()` function. Code to be executed every sample goes in the ComputerCard `ProcessSample` function.

### Build
- Note: Make sure Tools -> USB Stack is set to "No USB", as this interferes with the normalization probing.
- Sketch -> Upload to build and upload the sketch. 
- You can also create a .uf2 file with Sketch -> Export Compiled Binary. Your sketch directory should contain a build/rp2040.rp2040.rpipico directory (or similar) with a .uf2 file. Copy this file to your Computer.


## Changelog

Early versions do not include a version number in the source code, but can be identified by the MD5 checksum of the `ComputerCard.h` file.

| Version | Date |`ComputerCard.h` MD5 |
|---------|------|----------------------|
| 0.1.4   |  2024/11/30 | 37e9eba18047a4bc5999914c03256275 |
| 0.2.0   | 2024/12/22 | 79c2dddec3cb576ea102ef064996acb2 |
| 0.2.1   | 2024/12/29 | b5639163decb8d980935aa677d820231 |
| 0.2.2   | 2025/02/04 | 2602203ab56f56d9f6222eea839b8f5b |
| 0.2.3   | 2025/02/08 | 6dee0f6690ea3e6b9cb09c2814fd9cc5 |
| 0.2.4   | 2025/02/28 | 2247e04b8719cdc6df8c625057e8cad1 |
| 0.2.5   | 2025/03/02 | b76132bc5126e2cb2ee14617f72b7f64 |
| 0.2.6   | 2025/07/31 | Version number in ComputerCard.h |
| 0.2.7   | 2025/08/03 |                                  |
| 0.2.8   | 2026/02/09 |                                  |
| 0.3.0   | 2026/05/12 |                                  |
| 0.4.0   | 2026/06/14 |                                  |

#### 0.1.4
Transfer of code to public Workshop_Computer repository.

#### 0.2.0
Lots of fixes found during Utility Pair development:

- Fixed incorrect sign of both audio input and output.
- Fixed incorrect order of audio input jacks
- Reduced crosstalk of knobs by using different ADC sample
- Added (not well tested) compensation of ADC DNL errors.
- Removed `_calibrated` form of CV output functions
- Added input/output functions with jack number as parameter

#### 0.2.1
- Removed some residual double-precision calculations in calibration routine

#### 0.2.2
- Added `Abort` function
- removed unnecessary 50ms startup delay

#### 0.2.3
- Added `SwitchChanged` function

#### 0.2.4
- Added clipping of signed 16-bit audio outputs to -2048-2047 range
- Detection of Computer v1.1 hardware revision

#### 0.2.5
- Renamed `GetHardwareVersion()` method to `HardwareVersion()`
- Renamed `HardwareVersion` enum to `HardwareVersion_t`
- Added `USBPowerState()` function and `USBPowerState_t` enum

#### 0.2.6
- Moved to precise 19-bit sigma-delta PWM for CV outputs
  - New `CVOutPrecise` functions
  - Precise values used by `CVOutMIDINote` functions
  - May have _very_ minor differences in CV output latency, and on precise code timings, due to new interrupt that is not synchronised with audio sample.
- Bug fixes to USB MIDI example to handle multiple MIDI messages sent in a short time frame
- Bug fix in `CVOutMIDINote` where calibration was always that of CV out 1
- New `usb_serial` example


#### 0.2.7
- Bug fix in ADC DNL nonlinearity correction (thanks Andrew Cooke)
- Applying DNL nonlinearity correction to audio inputs, as well as CV
- Added `CVOutMillivolts` functions for calibrated CV output voltages
- Added `CVOutsCalibrated` function to detect if CV outputs have been calibrated
- New `calibrated_cv_out` example

#### 0.2.8
- Bug fix in calibration routine, where up to v0.2.7 `CVOutMillivolts` and `CVOutMIDINote` outputted constant voltages if the EEPROM did not contain calibration data
- This also fixes an issue where ComputerCard did not run on Computer development boards without an EEPROM.

#### 0.3.0
- Alterations to CV out to minimise high-frequency tones that have audible in the audio inputs since the introduction of 19-bit PWM in v0.2.6. To benefit from this, the CPU clock must be set to a multiple of 48MHz.
- Updated examples to use 144MHz (= 3 × 48MHz) clock
- New `hid_keyboard_mouse` example

#### 0.4.0
- Calibrated audio inputs, using calibration data held in the EEPROM
  - New `AudioIn1Millivolts`, `AudioIn2Millivolts` and `AudioInMillivolts` methods
  - New `InputsCalibrated` method to detect whether input calibration data is present
  - New `calibrated_input` example
- New `EightMU.h`, a standalone single-header class for using a Music Thing Modular 8mu as an input to a card. It carries the USB MIDI host driver and the host-mode TinyUSB configuration itself, so a card using it needs no other source files.
  - New `eightmu` example
- New `FlashSizeBytes` method, reporting the size of the flash chip on the program card as read from the chip at startup.
- New `card_info` example, displaying the unique card ID and flash size on the LEDs
- Bug fix where audio and CV inputs could return +2048, one count outside their documented -2048 to 2047 range
- Bug fix where the CV input smoothing filter settled up from zero over the first few milliseconds after startup, rather than starting at the value present on the input
- Bug fix in the mixing of the bits of `UniqueCardID`, which was previously very incomplete. This changes the value returned by `UniqueCardID` for every card.
- The flash chip size and unique ID are now read before `main()` runs, rather than in the `ComputerCard` constructor. This means that there are no longer any restrictions on where a `ComputerCard` object is constructed.
- Multicore examples no longer launch core1 from the `ComputerCard` constructor, and no longer call `ThisPtr()` from core1, since `ThisPtr()` is only set once `Run()` has been called
- Bug fix in the `midi_device` example, where a MIDI message cut short by the end of the USB read buffer was parsed from data bytes that had never been received
- `KnobVal` now scales the knob readings so that the ends of the knob's travel give 0 and 4095. Defining `COMPUTERCARD_UNSCALED_KNOBS` before including `ComputerCard.h` restores the unscaled behaviour of v0.3.0 and earlier.
- MIT licence text added to `ComputerCard.h`

<a id="reference"></a>
# Reference


The following is a list of public and protected methods of the `ComputerCard` class. 

## Public methods

- `void Run()`

   Starts processing of user interface and jacks. Calls `ProcessSample` callback at 48kHz. This method blocks, and in most cases will never return, though calling `Abort()` within ProcessSample will cause it to return.
   
- `void EnableNormalisationProbe()`
 
   Call before `Run` to enable detection of connected input jacks.
   

## Protected methods

- `void ProcessSample()`
 
   Pure virtual processing callback, overridden by all user-written classes that inherit from `ComputerCard`. Called at 48kHz once the `Run` method has been called to start processing.
   
   
The following protected methods are designed to be run within the overridden `ProcessSample` callback method, to access the hardware of the Computer. These functions are quick to run, and most are designated `__not_in_flash_func` to ensure that they run with low latency from RAM.

### Knobs and switches
- `int32_t KnobVal(Knob ind)`

   Returns value of the `Knob` specified. Output value is 12-bit integer in the range 0–4095, increasing clockwise. As of v0.4.0 this is scaled so that knobs should reach 0 and 4095 at the ends of their travel.

   Defining `COMPUTERCARD_UNSCALED_KNOBS` before including `ComputerCard.h` turns this scaling off, so that `KnobVal` returns the unscaled ADC reading (typical range 14-4095), as it did in v0.3.0 and earlier. 
   Knob inputs have some smoothing applied, but a knob left untouched may well jitter between two (or perhaps more) adjacent values.
 
    | `Knob` | Knob |
    |---------------------|---------|
    | `Main`            | The big knob|
    | `X`       | Knob X |
    | `Y`            | Knob Y | 

- `Switch SwitchVal()`

   Reads the current value of switch Z.
    | `Switch` | Position |
    |---------------------|---------|
    | `Up`            | Up|
    | `Middle`       | Middle |
    | `Down`            | Down (momentary) | 
   
- `bool SwitchChanged()`

  Returns `true` if the switch value has changed since the last sample. Useful for taking action only when a switch changes, rather than every sample (e.g. `if (SwitchChanged() && SwitchVal() == Down) {...}`). 

### Jack outputs
In all jack input and output methods with a parameter `int i`, jack 1 (on the left) is set when `i` has the value `0`, and jack 2 (on the right) is set when `i` has the value `1`.

- `void AudioOut(int i, int16_t val)`
  
  `void AudioOut1(int16_t val)`
  
  `void AudioOut2(int16_t val)`

  Set the value of an audio output jack. Accepts signed 12-bit values, −2048 to 2047, corresponding to roughly −6V (value −2048) to +6V (value 2047). Values outside this will be clipped.
  
  The latest `value` specified by the `AudioOut` methods is sent to the audio output jacks *after* the `ProcessSample` function has finished executing.
 
- `void CVOut(int i, int16_t val)`
 
  `void CVOut1(int16_t val)`
  
  `void CVOut2(int16_t val)`

  Set the value of an CV output jack. Accepts signed 12-bit values, −2048 to 2047. Values outside this range will be clipped. The range of voltages output is approximately −6V (value −2048) to +6V (value 2047), and is uncalibrated. 
  
- `void CVOutPrecise(int i, int32_t val)`
 
  `void CVOut1Precise(int32_t val)`
  
  `void CVOut2Precise(int32_t val)`

  Set the value of an CV output jack. Accepts signed 19-bit values, −262144 to 262143. Values outside this range will be clipped. The range of voltages output is approximately −6V (value −262144) to +6V (value 262143), and is uncalibrated. Sigma-delta modulation is used to get 19-bit precision from 11-bit PWM.
  

  The `CVOut` functions change the CV output at the start of the next CV PWM cycle, which is not synchronised with the audio samples, so it is recommended that the value of each CV output is set only once per `ProcessSample` call.
  
- `void CVOutMIDINote(int i, uint8_t noteNum)`

  `void CVOut1MIDINote(uint8_t noteNum)`
  
  `void CVOut2MIDINote(uint8_t noteNum)`
  
  Set the value of an CV output jack. Accepts a 12-bit MIDI note number 0–127. If the calibration data has been saved, this will be used to produce calibrated output voltages. The precision of the voltage output is roughly 5.9mV (7 cents at 1 volt per octave).
  
- `bool CVOutMillivolts(int i, uint32_t millivolts)`

  `bool CVOut1Millivolts(uint32_t millivolts)`
  
  `bool CVOut2Millivolts(uint32_t millivolts)`
  
  Set the value of an CV output jack to a voltage in millivolts. If the calibration data has been saved, this will be used to produce calibrated output voltages. Return true if the requested value is outside the range possible and the output has been limited; achievable range will vary depending on calibration values.
  
- `void PulseOut(int i, bool val)`

  `void PulseOut1(bool val)`

  `void PulseOut2(bool val)`
  
  
  Set the value of a pulse output jack. Accepts a boolean, producing roughly 5V output for `true` and 0V for `false`.
  
  The `PulseOut` functions change the pulse output immediately, so to avoid the possibility of very short pulses, it is recommended that the value of each Pulse output is set only once per `ProcessSample` call.
  
### Jack inputs
- `int16_t AudioIn(int i)`

   `int16_t AudioIn1()`
   
   `int16_t AudioIn2()`
   
   Return a signed 12-bit value (−2048 to 2047) corresponding to the `i`th audio input voltage.
   Audio inputs are sampled at 96kHz and two adjacent samples averaged to produce the value returned by these methods.

- `int16_t CVIn(int i)`
 
   `int16_t CVIn1()`
   
   `int16_t CVIn2()`
   
   Return a signed 12-bit value (−2048 to 2047) corresponding to the `i`th CV input voltage.
   CV inputs are sampled at 24kHz and a digital low pass filter is applied.

- `bool PulseIn(int i)`
  
  `bool PulseIn1()`
  
  `bool PulseIn2()`
  
  Return the state (`true` is high, `false` is low) of the pulse input jacks.
  
  
- `bool PulseInRisingEdge(int i)`
  
  `bool PulseIn1RisingEdge()`
  
  `bool PulseIn2RisingEdge()`
  
  Return `true` if the the state of the input jack is high this sample, but was low in the previous sample.
   
  
  
- `bool PulseInFallingEdge(int i)`
  
  `bool PulseIn1FallingEdge()`
  
  `bool PulseIn2FallingEdge()`
  
  Return `true` if the the state of the input jack is high this sample, but was low in the previous sample.
  

- `bool Connected(Input i)`

  `bool Disconnected(Input i)`
  
  Return `true` if a jack is (`Connected`) or is not (`Disconnected`) plugged into the input jack identified by `i`.
  This function requires `EnableNormalisationProbe()` to be called on the `ComputerCard` class, prior to `Run()`, otherwise jacks are always regarded as disconnected. Values of the `Input` enum are as follows:
  
  | `Input` |
  |---------|
  | `Audio1`  |
  | `Audio2`  |
  | `CV1`  |
  | `CV2`  |
  | `Pulse1`  |
  | `Pulse2`  |

### LEDs
For all LED functions, the `index` parameter takes a value 0–5 and identifies the LED, as in the following diagram of the bottom-left corner of the Workshop System:
```
|
| 0 1
| 2 3
| 4 5
|______
```

- `void LedBrightness(uint32_t index, uint16_t value)`
 
  Set the brightness of the LED identified by `index`. The brightness `value` ranges from 0 (off) to 4095 (full brightness).
  
- `void LedOn(uint32_t index, bool value = true)`
  
  Turn the LED identified by `index` on or (with the `value` parameter set to `false`) off.
  
- `void LedOff(uint32_t index)`

  Turn the LED identified by `index` off.
  
  
### Misc

- `HardwareVersion_t HardwareVersion()`
   
   Returns the hardware revision of the Workshop System Computer that the code is running on.
    | `HardwareVersion_t` | Version |
    |---------------------|---------|
    | `Proto1`            | 2024 Dyski prototypes and Proto 1.2 (May 2024 devkits)|
    | `Proto2_Rev1`       | Proto 2.0.0/2.0.1/2.0.2 and December 2024 production |
    | `Rev1_1`            | Rev 1.1, January 2025 | 

   ComputerCard currently does not appear to run on systems without an EEPROM (i.e. any prior to Proto 2.0.2).

- `USBPowerState_t USBPowerState()`
   
   Returns the mode of the USB port on the Computer.
    | `USBPowerState_t` | State |
    |---------------------|---------|
    | `DFP`            | Downstream facing port (MTM Computer acting as USB host)|
    | `UFP`       | Upstream facing port (MTM Computer acting as USB device), or, USB not connected |
    | `Unsupported`            | Returned on hardware prior to `Rev1_1`| 

- `uint64_t UniqueCardID()`

   Returns a 64-bit integer unique to the program card.
   This is the unique ID returned by the flash chip, with a scrambling function applied to ensure that all bits in the returned integer are unpredictable, even if many bits in the original unique ID are common between flash chips.

- `uint32_t FlashSizeBytes()`

   Returns the size, in bytes, of the flash chip on the program card, as read from the chip itself at startup.

   This is the size of the physical flash chip, which is not necessarily the size that the firmware was built for. The latter is the compile-time constant `PICO_FLASH_SIZE_BYTES` (2MB by default), and it is `PICO_FLASH_SIZE_BYTES` that bounds what the Pico SDK `flash_range_erase`/`flash_range_program` functions will allow you to write. To make use of flash beyond that point on a larger card, `PICO_FLASH_SIZE_BYTES` must also be increased at build time.

   The size is read using the JEDEC Read Identification command (0x9F), interpreting the capacity byte as log2 of the chip size. On the rare chips that do not follow this convention, `PICO_FLASH_SIZE_BYTES` is returned instead.

   Reading this from the flash chip requires turning off execute-in-place (XIP) for a few microseconds, which is only safe when no other code is executing from flash. It is therefore read once, along with the unique ID used by `UniqueCardID`, in a function that runs before `main()`, where no interrupts are enabled and core1 has not been started. (This is the same approach that the Pico SDK's own `pico_unique_id` library takes.) There are consequently no restrictions on where a `ComputerCard` object is constructed.

- `bool CVOutsCalibrated()`

Returns `true` if CV Output calibration data has been loaded, or false if no calibration data detected. 
	
- `void Abort()`

   When called from `ProcessSample`, stops the processing started when `Run()` was called, and returns from the (otherwise blocking) `Run` method. This allows `Run` to be called again, potentially on a different `ComputerCard` class.
   

- `static ComputerCard* ThisPtr()`

   Static member function that returns the `this` pointer of whichever `ComputerCard` instance last started audio processing. This is useful to allow C-style functions (in particular, callbacks) to access the active ComputerCard.
   


<a id="eightmu-reference"></a>
# EightMU reference

The following is a list of the public methods of the `EightMU` class, provided by `EightMU.h` for cards using a [Music Thing Modular 8mu](https://www.musicthing.co.uk/8mu/) as an input. See the [8mu support](#eightmu) section above for how to add one to a card, and the `eightmu` example.

All of these may be called at any time, including from `ProcessSample`. Values read from the 8mu arrive on core1 and are read on core0, so a card sees the most recent value received, and never blocks waiting for one.

## Starting the USB host

- `void Start()`

   Launches core1, which then handles USB MIDI host communication with the 8mu forever. Call this before `ComputerCard::Run()`, from the card's constructor or from `main()`.

   A card that calls `Start()` cannot use core1 for anything else. Cards that need core1 themselves should use `Poll()` instead.

- `void Poll()`

   Services USB MIDI, for cards that run their own core1 loop. Call repeatedly from core1, after `board_init()` and `tusb_init()`. Not needed if `Start()` is used.

- `bool Connected()`

   Returns `true` once an attached USB device has identified itself as an 8mu. An 8mu may be plugged and unplugged while the card is running.

## Faders and buttons

- `int32_t Fader(int i)`

   Returns the position of fader `i` (0–7), from 0 to 4064. The 8mu sends fader positions as 7-bit MIDI CCs, so this has 128 distinct values, spaced 32 apart.

   Returns 0 for an `i` outside 0–7, and before an 8mu has first connected. Fader positions are deliberately kept when an 8mu is unplugged, so that outputs derived from them hold their last position rather than jumping to zero.

- `bool Button(int i)`

   Returns `true` while button `i` (0–3) is held down. Unlike the fader positions, buttons are all released when an 8mu is unplugged.

## Motion sensing

All four of these return values from −2032 to 2032. The 8mu sends each as a pair of one-sided CCs, which are subtracted to give a signed value. All are heavily smoothed by the 8mu itself, so they respond over tens of milliseconds rather than instantly.

`Pitch`, `Roll` and `Flip` are the three axes of the accelerometer, so for a stationary 8mu they are the three components of a single gravity vector, and are not independent of each other: knowing two fixes the magnitude of the third. The 8mu clips them at 1g, which is the whole range that gravity can produce, so an 8mu lying flat gives `Pitch` and `Roll` of 0 and `Flip` at full scale, and tilting it through 90 degrees takes `Pitch` or `Roll` to full scale and `Flip` to 0. Moving the 8mu faster than gravity does not read any higher.

- `int32_t Pitch()`

   Tilt, positive with the back of the 8mu lifted.

- `int32_t Roll()`

   Tilt, positive with the right-hand side of the 8mu lifted.

- `int32_t Yaw()`

   Rotation about the vertical axis. Unlike `Pitch` and `Roll`, this comes from the gyroscope, so it is a rate of rotation rather than a position: it returns to 0 whenever the 8mu is not actually being turned, however far it has been turned in total.

- `int32_t Flip()`

   This is the vertical accelerometer axis, indicating which way up the 8mu is when at rest: −2032 lying flat, 2032 upside down.

## LEDs

The 8mu flashes its own LEDs on MIDI activity. The first call to any of the functions below except `ReleaseLeds` takes the LEDs over from the 8mu, so an 8mu used only as a controller, that never has its LEDs set, is left alone.

- `void SetLed(int i, int32_t brightness)`

   Sets the brightness of 8mu LED `i` (0–7), from 0 to 4095, matching the range of `ComputerCard::LedBrightness`. The 8mu takes 7-bit MIDI velocities, so this has 128 distinct levels.

- `void LedOn(int i)`

   Turns 8mu LED `i` (0–7) fully on.

- `void LedOff(int i)`

   Turns 8mu LED `i` (0–7) off.

- `void ReleaseLeds()`

   Hands the LEDs back to the 8mu, restoring its own MIDI activity flashing.

## Misc

- `uint8_t FirmwareMajor()`, `uint8_t FirmwareMinor()`, `uint8_t FirmwarePoint()`

   The three parts of the version number of the firmware running on the attached 8mu, as reported when it identifies itself. Valid while `Connected()`; all three read 0 otherwise.

- `static EightMU* Instance()`

   Static member function that returns the `this` pointer of the most recently constructed `EightMU`. This is used internally to reach the class from the USB MIDI host driver's C callbacks, in the same way that `ComputerCard::ThisPtr()` is used.

- `static constexpr int numFaders`, `numButtons`, `numLeds`

   The number of faders (8), buttons (4) and LEDs (8) on an 8mu.

The `OnMount`, `OnUnmount` and `OnMIDIBytes` methods are also public, but are called by the USB MIDI host driver's callbacks and are not intended to be called by cards.

<a id="programming"></a>
# Programming for ComputerCard

This section introduces some ways in which ComputerCard programming differs from desktop audio programming. Many more details are provided in a longer [programming tips](./NOTES.md) document.

ComputerCard is designed to allow audio signals (with bandwidths up to ~20kHz) to be processed at low latency. To do this, the computations for each sample must be calculated individually, by calling the users `ProcessSample()` function at 48kHz. The `ProcessSample()` function for one sample must finish before the one for the next sample starts, meaning that the user's code for each sample must execute in 1/48000th of a second, or ~20μs. This is perfectly feasible on the RP2040, but requires some attention to code performance. Specifically;

1. increasing clock speed 
2. calculations on audio signals usually need to be done with integers rather than floating point.
3. relatively lengthy calculations (more than ~20μs) must either be done on a different RP2040 core to the audio, or split between `ProcessSample` calls to ensure than no one call goes above the maximum duration. In particular, USB handling must be on a different core. 
4. Particularly for larger programs, it may be necessary to force the code called by `ProcessSample` into RAM, so that delays in fetching of code from the flash card do not cause `ProcessSample()` to exceed its allowed time.

We'll discuss each of these below

## 1. Increasing clock speed

The default clock rate of the RP2040 is 125MHz, and until February 2025 the fastest supported clock speed was 133MHz.

As of Pico SDK v2.1.1, clock speeds up to 200MHz are supported (potentially with associated core voltage increase). This is officially done with a [preprocessor define](https://github.com/raspberrypi/pico-sdk/releases/tag/2.1.1), though I have had no issues with increasing the clock speed only, using `set_sys_clock_khz(200000, true);` at the start of the `main()` entry point.

As of ComputerCard 0.3.0, clock speeds that are are multiples of 48MHz minimise noise on the audio inputs, and so a rate of 144MHz, 192MHz or 240MHz is recommended.

```
	set_sys_clock_khz(144000, true); // officially supported at default voltage
```
```
	vreg_set_voltage(VREG_VOLTAGE_1_15); // officially supported at 1.15V
	set_sys_clock_khz(192000, true);
```
```
	vreg_set_voltage(VREG_VOLTAGE_1_20); // overclock, not officially supported
	set_sys_clock_khz(240000, true);     // but appears to work on multiple devices at 1.2V
```
## 2. Integer calculations

Floating-point operations on the RP2040 are software emulated, and are [much slower](https://forums.raspberrypi.com/viewtopic.php?t=308794#p1848188) than the native 32-bit integer addition, subtraction and multiplication.

While simple floating-point calculations are possible within the `ProcessSample()` function (see, for example, the `sine_wave_float` example), for reasons of speed the ComputerCard class itself does not use floating-point variables in the sample callback.

The approach taken instead is to use a fixed-point number representation, with operations on samples performed with signed 32-bit integers (`int32_t`, as defined in the `cstdint` header). The hardware 32-bit integer multiply on the RP2040 makes many such operations very efficient. 

### Example: crossfading audio signals
Let's look at an example of code which averages the two audio inputs and puts this mixed signal onto both audio outputs:

$$\mbox{out} = \frac{\mbox{in}_1 + \mbox{in}_2}{2}$$
```c++
int32_t mix = (AudioIn1() + AudioIn2()) >> 1;
AudioOut1(mix);
AudioOut2(mix);
```
Because the RP2040 has hardware multiply and shift instructions, but not a division instruction, where possible I replace divisions with multiply (`*`) and bit-shift-right (`>>`), which on the RP2040 acts as a divide-by-power-of-two on both signed and unsigned integers[^1].


[^1]: albeit with different rounding behaviour: for signed types `>>` is implementation-defined in C++, but GCC compiles to the ARM `ASR` (arithmetic shift right) instruction, which divides rounding to negative infinity.  C++ divide (`/`) rounds towards zero. 


An ever-present concern with integer operations such as these is the possibility of integer overflow (exceeding the representable range of integers), and the wrapping of values that occurs in this case. 
- C++ integer promotion rules mean that the `int16_t` return value of `AudioIn` functions (in fact only containing a 12-bit range -2048 to 2047) are promoted to the (32-bit signed) `int` before operations. The relevant wrapping values are therefore $\pm 2^{31}$, far larger than any integers used here.
- Saving `mix` to a 32-bit integer introduces wrapping at $\pm 2^{31}$, again not a problem here, given the 12-bit outputs of the `AudioIn` functions. (In fact, we could use an `int16_t` for the `mix` variable, but there is no need to do so.)

Let's now extend our program so that it uses the main knob to crossfade between the two audio inputs.
Denoting the knob value as $k$, varying from 0 to 1, our crossfade would be [^2]

$$ \mbox{out} = (1-k)\\, \mbox{in}_1 + k\\, \mbox{in}_2.$$ 

[^2]: Ideally, for audio signals, we'd use an energy-preserving crossfade rather than this linear one.

In ComputerCard, the knobs return positive 12-bit values 0–4095 (though the end of knob travel may not reach either 0 or 4095), and we instead calculate: 
```c++
int32_t k = KnobVal(Knob::Main);
int32_t mix = (AudioIn1()*(4095-k) + AudioIn2()*(k)) >> 12;

AudioOut1(mix);
AudioOut2(mix);
```

Even though `k` is non-negative, we use an signed `int32_t` type (not `uint32_t`) because operations involving both signed and unsigned integers will often result in the signed type being converted to unsigned, which is not desired here. It's easiest just to keep everything as `int32_t`.

The choice of `4095 - k` not `4096 - k` means that this crossfade perfectly isolates each of the two inputs at the ends of the range 0–4095, at the expense of a very slight decline ($4095/4096$) in amplitude. 

Let's again examine the possibility of overflow. The multiply operations here calculate the product of signed 12-bit and unsigned 12-bit numbers, resulting in a signed 24-bit number. We then add two of these, which would in general produce up to a signed 25-bit number, but here because of the `k` and `4095-k` multiplicands, the sum is in fact signed 24-bit. This is well within the signed 32-bit range of the integer type, so there is no risk of overflow during the evaluation of the expression. Shifting right by 12 bits produces a signed 12-bit result (-2048 to 2047) which will not clip in the `AudioOut` functions.


### Example: first-order IIR LPF
One of the most common DSP operations is a first-order filter, such as the IIR lowpass filter

$$ y[n] = a y[n-1] + b x[n], $$

with filter coefficients $a$ ($0 < a < 1$) and $b$. For unity gain at DC, $b = 1-a$, and so our filter equation,

$$ y[n] = a y[n-1] + (1-a) x[n], $$

looks almost identical to the crossfade example above. Choosing the input data $x$ to be the first audio input, and sending the filter output $y$ to the first audio output, we might write

```c++
int32_t a = 4089;
y = (a*y + (4096-a)*AudioIn1()) >> 12;

int32_t out = y;
if (out<-2048) out=-2048;
if (out>2047) out=2047;
AudioOut1(out);
```
where 
- the filter coefficient value $a = 0.9983 \approx 4089/2^{12}$ corresponds to a cutoff of about 13Hz – very low for audio but typical of an AC-coupling filter or a filter for CV,
- `y` is assumed to be an `int32_t` that persists between calls to `ProcessSample()` (likely a class member), and
- clipping has been applied to the filter output, as it is not obvious that this will be in the 12-bit range -2048 to 2047 for all possible input signals

However, as well as integer overflow, another consideration with integer/fixed-point arithmetic is roundoff error. Suppose that in the code above, previous audio input has resulted in `y` having the value 500, and the audio input then falls silent (`AudioIn1` returns zero). For this low-pass filter, we would expect `y` to drop to zero exponentially. The relevant expression becomes:
```c++
y = (4089*y) >> 12;
```
Repeated evaluation of this shows that the decay of `y` from an initial value of 500 is far from exponential – in fact it drops linearly to zero! In this problem the amount by which `y` decreases per sample is between 0 and 1 if evaluated exactly, but in integer arithmetic this decay must be an integer and is quantised to a decay of 1 per sample.

This problem can be mitigated by amplifying the signal going into the filter by a large factor, the attenuating the filter output by the same amount. Here we amplify and attenuate by $2^7 = 128$:
```c++
int32_t a = 4089;
y = (a*y + (4096-a)*(AudioIn1()<<7)) >> 12;

int32_t out = y>>7;
if (out<-2048) out=-2048;
if (out>2047) out=2047;

AudioOut1(out);
```
This gives a much closer approximation to the floating-point behaviour.

As before, we must check for overflow, and for the first time in these examples, we are here getting close to overflow with 32-bit integers.  The expression `(4096-a)*(AudioIn1()<<7)` is `(unsigned 12-bit) * (signed 12-bit) << 7`, giving a signed 31-bit result, to which `a*y`, 24-bit value, is added, potentially using all 32 bits. There is a tradeoff here between:
- precision with which `a` can be specified (here, 12-bit)
- bit-depth of the audio signal (here, 12-bit)
- amount of amplification/attenuation to reduce roundoff (here, 7-bit)
which together must not exceed the 32 bits available. Different operations may favour a different allocation of bits. 

Three further comments:
- Sometimes, depending on the filter, the increased precision offered by the `int64_t` type is needed, though this is slower than `int32_t`.
- On the RP2040, the `>>` operation on signed types rounds to negative infinity. Sometimes it may be worth adding a constant into the filter expression to alter this behaviour to round-to-nearest.  
- The roundoff error on a low-pass filter such as this produces a very primative hysteresis-like effect, which may occasionally be useful. This is used in ComputerCard to reduce noise/jitter in knob values.

## 3. Lengthy calculations
Options for dealing with calculations that exceed the available ~20μs per sample are:
- optimise these calculations, for example using lookup tables [^3]
- offload long calculations onto the second RP2040 core.
- split the calculations that do not have to be done every sample up in to parts small enough to do in successive `ProcessSample` functions,

The `second_core` example shows one way to execute longer/slower computations for CV signals (that is, not at audio-rate) on the second core.

For USB processing, the TinyUSB function `tud_task` may take longer than one sample time, and so this needs to be done on a different core from the audio. See the `midi_device` example for how this can be done. I'm planning to add some multicore stuff into ComputerCard itself, in due course, including an option to run the audio callback on core1, not the default core0.


[^3]: While anything more than very simple floating point calculations are typically too slow to perform every sample, it's convenient to have them available for calculating lookup tables when the card first starts. Lookup tables can of course be calculated on a much more powerful computer and hard-coded as constant arrays.

## 4. Putting code in RAM
To force a function into RAM, rather than flash, surround the name in function definition by  [__not_in_flash_func()](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#group_pico_platform_1gad9ab05c9a8f0ab455a5e11773d610787). The `ComputerCard.h` file has various examples of this. 

For cards of modest code size and RAM use, but very tight timing requirements, the entire code can be copied into RAM at startup using the `set(PICO_COPY_TO_RAM,1)` command in `CMakeLists.txt`.

