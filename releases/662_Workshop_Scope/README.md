# Workshop Scope

A four-channel scope for the Workshop Computer. It reads Audio In 1 and 2 and CV In 1 and 2 and streams them over USB to a web page, which draws them as a triggered scope, an XY plot, a spectrum, or a slow rolling chart for long envelopes. Every input is also passed through to its matching output, so the scope can sit in a patch.

Beta. Tested only on one Workshop System; the board revision was not recorded.

## Using it

1. Flash `UF2/workshop_scope.uf2` to the program card.
2. Connect the Computer's USB socket to a computer and power-cycle it.
3. Open `web/index.html` in desktop Chrome or Edge, press **Connect** and choose the Computer's serial port.

The page needs Web Serial, which Firefox and Safari (including every iPhone and iPad browser) do not support. Android was not tried. Serve the page over `http://localhost` or from the GitHub Pages site; Web Serial does not work from a `file://` address.

## Inputs

All four jacks are DC-coupled and read roughly -6 V to +6 V. Anything above about +6 V or below about -6 V is clipped flat, so attenuate hot signals (for example 10 V envelopes) before they reach the card. Values are not calibrated, so voltages shown are approximate.

| Jack | Read |
|------|------|
| Audio In 1, 2 | 48 kS/s each |
| CV In 1, 2 | Share one ADC input through the Computer's multiplexer, so they alternate every 1.8 ms. The page holds the other jack's last value in between. This is fine for CV, not for fast signals. |

Any of the four jacks can take audio or CV. The page labels each one **audio**, **CV** or **idle** (flat signal) from what it sees on the input. In the page, Audio L is Audio In 1 and Audio R is Audio In 2. The Computer documentation says the left input is normalled to the right, so with only one audio jack plugged in both audio channels may show it.

## Pass-through

| Input | Output |
|-------|--------|
| Audio In 1 | Audio Out 1 |
| Audio In 2 | Audio Out 2 |
| CV In 1 | CV Out 1 |
| CV In 2 | CV Out 2 |

Pass-through is on when the card starts and runs whether or not the web page is open. The **Pass-through** checkbox in the page switches it off, which parks all four outputs at about 0 V.

- The raw ADC codes go straight to the audio DAC and the CV PWM outputs, with no calibration, so output levels are approximate.
- The outputs are designed to follow the inputs by about one 48 kHz frame, on the second processor core. The delay was not measured.
- CV In 1 and 2 are read alternately, so the CV outputs are stepped and not suitable for fast signals.
- The CV outputs use 62.5 kHz PWM. Whether this affects the readings was not checked.

## Views

- **Scope**: trigger on any channel (rising or falling, with a level), and a time span of 64 to 2048 samples (about 1.3 to 43 ms). Time and voltage gridlines are labelled.
- **XY**: plots any channel against any other.
- **Spectrum**: a 4096-point FFT on a log axis from 20 Hz to 24 kHz, with 0 dB as a full-scale sine. The Smoothing setting (Off, Low, Medium, High) averages over time and over fractions of an octave. High smoothing reads peaks about 2 dB low.
- **Roll**: a scrolling chart of the last 5, 10, 30, 60 or 120 seconds, drawn from the minimum and maximum of every 5 ms. Meant for slow envelopes and LFOs.

Pause freezes the display. Invert is on by default because the Computer's inputs read inverted (+6 V reads low).

## Known issues

- The page shows a count of dropped USB blocks. Each drop loses about 1.8 ms of signal, and the spectrum skips frames that contain a drop. The count grows during long sessions on the author's setup. The cause was not found.
- A narrow spur near 16.5 kHz at about -76 dB shows up in the spectrum on the author's unit even when the signal changes. Its cause was not found; it could be interference or an alias of something above 24 kHz.
- There are no calibration or offset corrections. The ADC offset is removed only for the spectrum.

## How it works

The RP2040 ADC runs in round-robin over ADC0, ADC1 and ADC3 (GPIO 26, 27 and 29) at 48 kS/s per channel, filled by DMA into two ping-pong buffers. Mux select A (GPIO 24) flips after every block to choose CV In 1 or 2. Each block is sent over USB CDC as a frame:

| Bytes | Content |
|-------|---------|
| 2 | `0xA5 0x5A` |
| 1 | Sequence number |
| 1 | Flags: bit 0 set when the CV samples are CV In 2 |
| 510 | 255 little-endian 12-bit samples, interleaved ADC0, ADC1, ADC3 |

The second core follows the DMA frame by frame and writes the audio DAC (SPI, GPIO 18, 19 and 21) and the CV PWM outputs (GPIO 23 and 22). The web page sends one byte to the card, `P` or `p`, to switch pass-through on or off. The page is plain HTML and JavaScript with no build step.

## Building

Needs the Pico SDK.

```
mkdir build && cd build
cmake .. && make
```

This card does not use ComputerCard, calibration, the knobs, the switch or the LEDs.

## License

MIT, see `LICENSE`.

## AI assistance

Written with AI help (GitHub Copilot).
