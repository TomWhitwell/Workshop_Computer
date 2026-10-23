# Workshop Scope

A six-input scope for the Workshop Computer. It reads Audio In 1 and 2, CV In 1 and 2, and Pulse In 1 and 2, and streams them over USB to a web page. The page draws them as a triggered scope, an XY plot, a spectrum, or a slow rolling chart for long envelopes. Every input is also passed through to its matching output, so the scope can sit in a patch.

Beta. Tested only on one Workshop System; the board revision was not recorded.

## Using it

1. Flash `UF2/workshop_scope.uf2` to the program card.
2. Connect the Computer's USB socket to a computer and power-cycle it.
3. Open `web/index.html` in desktop Chrome or Edge, press **Connect** and choose the Computer's serial port.

The page needs Web Serial. Firefox and Safari (including every iPhone and iPad browser) do not support it. Android browser support varies and has not been verified; desktop Chrome or Edge is recommended. Serve the page over `http://localhost` or from the GitHub Pages site; Web Serial does not work from a `file://` address. The Connect button reports whether the port opened and whether scope data arrived.

## Inputs

All four jacks are DC-coupled and read roughly -6 V to +6 V. Anything above about +6 V or below about -6 V is clipped flat, so attenuate hot signals (for example 10 V envelopes) before they reach the card. Values are not calibrated, so voltages shown are approximate.

| Jack | Read |
|------|------|
| Audio In 1, 2 | 48 kS/s each |
| CV In 1, 2 | Share one ADC input through the Computer's multiplexer, so they alternate every 1.8 ms. The page holds the other jack's last value in between. This is fine for CV, not for fast signals. |
| Pulse In 1, 2 | Digital state is sampled once per 1.8 ms block for display; very short pulses may be missed by the plotted trace. The live pass-through reads the inputs at the audio frame rate. |

The four analog jacks can take either audio or CV. The page uses fixed physical jack labels; it does not try to infer whether an analog signal is audio or CV. In the page, Audio In 1 is the Computer's left input and Audio In 2 is its right input. The Computer documentation says the left input is normalled to the right, so with only one audio jack plugged in both audio channels may show it.

## Pass-through

| Input | Output |
|-------|--------|
| Audio In 1 | Audio Out 1 |
| Audio In 2 | Audio Out 2 |
| CV In 1 | CV Out 1 |
| CV In 2 | CV Out 2 |
| Pulse In 1 | Pulse Out 1 |
| Pulse In 2 | Pulse Out 2 |

Pass-through is on when the card starts and runs whether or not the web page is open. The **Pass-through** checkbox in the page switches it off, which parks the analog outputs at about 0 V and makes the pulse outputs inactive.

- The raw ADC codes go straight to the audio DAC and the CV PWM outputs, with no calibration, so output levels are approximate.
- The outputs are designed to follow the inputs by about one 48 kHz frame, on the second processor core. The delay was not measured.
- CV In 1 and 2 are read alternately, so the CV outputs are stepped and not suitable for fast signals.
- The CV outputs use 62.5 kHz PWM. Whether this affects the readings was not checked.
- Pulse outputs use the Computer's inverted digital output convention. Pulse pass-through timing was not measured.

## Views

- **Scope**: trigger on any channel (rising or falling, with a level), and a time span of 64 to 2048 samples (about 1.3 to 43 ms). Time and voltage gridlines are labelled.
- **XY**: plots any channel against any other.
- **Spectrum**: a 4096-point FFT on a log axis from 20 Hz to 24 kHz, with 0 dB as a full-scale sine. The Smoothing setting (Off, Low, Medium, High) averages over time and over fractions of an octave. High smoothing reads peaks about 2 dB low.
- **Roll**: a scrolling chart of the last 5, 10, 30, 60 or 120 seconds, drawn from the minimum and maximum of every 5 ms. Meant for slow envelopes and LFOs.

The Pulse rate readouts use rising-edge intervals measured by GPIO interrupts in the firmware, rather than USB arrival timing. The card sends the interval in milliseconds; the web page uses the median of recent periods and shows `-- BPM` until it has a period or after the pulses stop. The trace still samples pulse state once per 1.8 ms block, so very short pulses may not appear in the plot.

Pause freezes the display. Invert is on by default because the Computer's inputs read inverted (+6 V reads low).

## Known issues

- The page shows a count of dropped USB blocks. Each drop loses about 1.8 ms of signal, and the spectrum skips frames that contain a drop. The count grows during long sessions on the author's setup. The cause was not found.
- A narrow spur near 16.5 kHz at about -76 dB shows up in the spectrum on the author's unit even when the signal changes. Its cause was not found; it could be interference or an alias of something above 24 kHz.
- There are no calibration or offset corrections. The ADC offset is removed only for the spectrum.

## How it works

The RP2040 ADC runs in round-robin over ADC0, ADC1 and ADC3 (GPIO 26, 27 and 29) at 48 kS/s per channel, filled by DMA into two ping-pong buffers. Mux select A (GPIO 24) flips after every block to choose CV In 1 or 2. Pulse inputs use GPIO 2 and 3 with pull-ups; pulse outputs use GPIO 8 and 9. Each block is sent over USB CDC as a frame:

| Bytes | Content |
|-------|---------|
| 2 | `0xA5 0x5A` |
| 1 | Sequence number |
| 1 | Flags: bit 0 CV In 2 selected; bits 1 and 2 pulse inputs active; bits 3 and 4 pulse rising edges |
| 4 | Pulse 1 period in milliseconds, unsigned little-endian 16-bit |
| 6 | Pulse 2 period in milliseconds, unsigned little-endian 16-bit |
| 510 | 255 little-endian 12-bit samples, interleaved ADC0, ADC1, ADC3 |

The second core follows the DMA frame by frame and writes the audio DAC (SPI, GPIO 18, 19 and 21), CV PWM outputs (GPIO 23 and 22), and pulse outputs. The web page sends one byte to the card, `P` or `p`, to switch pass-through on or off. The page is plain HTML and JavaScript with no build step.

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
