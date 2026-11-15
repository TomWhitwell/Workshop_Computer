## TelexIO

**TelexIO** is a port of the open-source **TelexI** and **TelexO** expander modules (for Teletype) to the **Music Thing Modular Workshop Computer**. 

The TELEX are a family of eurorack expanders that were designed by **bpcmusic** to interface with the Monome **Teletype** to provide additional inputs and outputs. These modules can also be used with other i2c devices that can send the appropriate i2c commands over the i2c bus, e.g. Crow. The original Telex Expanders (hardware design files and source code) can be found here: https://github.com/bpcmusic/telex

This card emulates both TelexI and TelexO modules together on a single workshop computer and responds to the same TI and TO commands from an i2c-connected Teletype as the original modules do. 

The Workshop Computer’s knobs, inputs, and outputs are mapped to corresponding TelexI and TelexO functionality, with a few differences due to different hardware.

For the compiled binary card (v0.2) see **telexio.uf2** in this folder.

# Input and output mapping
Mapping between Workshop Computer and TelexI/TelexO functionality:
* The three knobs (main, x, y) correspond to the first three TelexI knob inputs.
* The switch is mapped to the fourth TelexI input, generating values 0,1,2.
* Audio and CV inputs are mapped to the corresponding TelexI inputs. This makes use of the calibration data in the Workshop Computer's EEPROM, if available.
* Audio, CV and trigger outputs are mapped to the corresponding TelexO outputs. The Workshop Computer has two trigger outputs instead of TelexO’s four, so two are unavailable. The CV outputs make use of the EEPROM calibration data.
* There are two additional trigger inputs. These are internally normalised to trigger envelopes configured on the first two output channels of TelexIO.
* The maximum output range for the Workshop Computer's outputs is +/- 6V.
* Callibration can be done using the **01 Midi card**.

# Electrical notes
**Please read**

TelexIO needs an i2c connection. The Workshop Computer contains two (typically unpopulated) pins labeled UART DEBUG that can be used for i2c. Solder a header to the UART pads TX and RX:
* RX (bottom) -> **SCL**
* TX (top) -> **SDA**

Make sure Workshop Computer and Teletype share **GND**. A dedicated **GND** wire between the Workshop Computer and the Teletype i2c bus is recommended. Without it, I sometimes experienced data corruption during i2c communication.

By default the **computercard.h** framework seems to configure the UART pins as outputs during startup. Many other cards use this framework, though not many other cards actually make use of the UART pins. This default setup will affect the i2c bus when using those cards. Therefore, when using other cards based on computercard.h, disconnect the i2c connection, or recompile those cards with the appropriate UART configuration flag - ENABLE_UART_DEBUGGING - see my CMakeLists.txt file. While it may be electrically safe to leave the bus connected (I am not completely certain about this) it will definitively confuse the i2c devices on the bus. 

# Notes on implementation
On the RP2040, Core 1 handles the i2c interrupts and the TelexI code, while Core 0 runs most of the TelexO code with a sample rate of 48 kHz.

As the RP2040 has no FPU, some floating-point code from TelexO was rewritten to use integer arithmetic (particularly the polyblep code) and some float calculations are passed from Core 0 to Core 1 for less time critical processing.

# LLM disclosure
AI was used for code review, safety checks and optimisation suggestions. No vibe coding or otherwise unreviewed code is used in this project.

# License
**TelexIO** is **MIT licensed**, in line with the code it is derived from.

# History
V0.2 add callibrated inputs and outputs, change app type copy_to_ram for speed

V0.1 first version

Future work:
- Further optimisation, if needed.
