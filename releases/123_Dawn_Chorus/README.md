# Dawn Chorus

An ambient soundscape that follows the real sun and the local weather. A browser page works out where the sun is for your location, fetches the weather, and streams both to the card over USB. As the sun rises, a drone swells and brightens, chimes ring out and birds wake. Through dusk, everything settles back to (almost) silence. Because the weather is the score, no two sun rises or sun sets sound the same.

Inspired by the tides of Matt Allison's [Equanimity](../112_Equanimity) card.

## What you need

- A Workshop System or Workshop Computer with this card flashed.  
- A laptop or desktop running **Chrome or Edge**. Web Serial isn't available in Safari, Firefox or mobile browsers.  
- A USB cable from the Computer's USB-C port to the laptop.  
- An internet connection for the weather. Without one, the sun still works and mild default weather is used.

## Getting started

1. Flash `UF2/dawn_chorus_v0.6.uf2` to a program card.  
2. Power the system and connect the Computer to the laptop over USB.  
3. Open the card's **Web Editor** page. You can also open `web/index.html` locally; it's a single file with nothing to install.  
4. Set your location by searching for a town, choosing **Use my location**, or typing coordinates.  
5. Click **Connect card** and choose the Raspberry Pi / Pico serial device.  
6. The top-right LED goes solid and the page shows **Card is receiving**.

To hear a whole sunrise straight away, choose **Fast** on the page.

## The host page

The page has three time modes:

| Mode | What it does |
| :---- | :---- |
| **Live** | Real time and real weather. The card follows today's actual sun. |
| **Fast** | The real sun and forecast weather, played at 10× to 1440× from an hour before the next sunrise or sunset. |
| **Manual** | Sliders for sun height, rising or setting, and each weather value, for auditioning. |

A band across the top shows the day's sun path in sky colours, with sunrise and sunset times. The page remembers your location and mode.

## How the sky becomes sound

| Data | What you hear |
| :---- | :---- |
| **Daylight** | Rises smoothly from nautical twilight (sun 12° below the horizon) to the sun 12° up. It raises the drone's level and brightness, makes chimes louder, brighter, busier and higher, and drives CV Out 1\. |
| **Birdsong** | Builds slowly from first light, peaks just after sunrise, settles to 80% through the morning, eases to 60% when the sun is high, lifts again for an evening chorus, and fades through dusk. In winter the sun stays low, so there's little midday lull. |
| **Cloud** | Dulls the drone. |
| **Wind** | Adds vibrato, beating and slow gusting swells to the drone, makes the chime echoes waver like tape, and makes chimes busier. Above about 14 km/h the chime notes bend too, by up to about 9 cents in strong gusts. |
| **Warmth** | Brightens the chime strike. |
| **Humidity** | Lengthens the echoes. |
| **Rain** | Adds a soft vinyl-style crackle with a faint hiss, independently on each output. |

Values from the page are smoothed on the card: over about a second for the sun and about four seconds for the weather, so changes arrive gradually rather than in steps.

## Outputs

| Jack | What it does |
| :---- | :---- |
| **Audio Out 1** | Drone: root, fifth and octave. Rain crackle. |
| **Audio Out 2** | Chimes through a feedback delay, plus birds. The birds skip the delay and have a light reverb of their own. Rain crackle. |
| **CV Out 1** | Daylight, 0 V at night to \+5 V in full daylight. |
| **CV Out 2** | Pitch of the latest chime, 1 V/oct. |
| **Pulse Out 1** | 10 ms trigger on each chime. |
| **Pulse Out 2** | 10 ms trigger at the start of each bird phrase. |

## Inputs

| Jack | What it does |
| :---- | :---- |
| **CV In 1** | Added to X (bird type). A slope here changes the species from phrase to phrase. |
| **CV In 2** | Added to Y (chime instrument). |
| **Pulse In 1** | Strikes a chime immediately. |
| **Pulse In 2** | Starts a bird phrase immediately. |

About 0 to \+5 V covers most of a knob's range, so with a slope patched in, start with the knob well anticlockwise. Unplugged CV inputs have no effect.

## Controls

| Control | What it does |
| :---- | :---- |
| **Main** | Key. 12 o'clock is D, and each step is a semitone. The drone, chimes and CV Out 2 follow, with a short glide. |
| **X** | Bird type. Anticlockwise gives low, slow coos, 12 o'clock warbling songbirds, and clockwise small, fast tweets. |
| **Y** | Chime instrument, anticlockwise to clockwise: marimba, harp, xylophone, bell, vibraphone, glockenspiel. Marimba, harp and xylophone use Casio CZ-style phase distortion; the others use two-operator FM. |
| **Switch middle** | Normal. |
| **Switch up** | Sparse: chimes and birds at 40% of normal. |
| **Tap down** | Preview full daylight for 10 seconds, then return to the real sky. |

There's no volume control on the card, so set levels on the mixer.

## LEDs

| LED | Meaning |
| :---- | :---- |
| Top left | Daylight |
| Top right | USB link: solid while receiving, slow blink while waiting or after the link drops |
| Middle left | Birdsong activity |
| Middle right | Cloud cover |
| Bottom left | Flashes on each chime |
| Bottom right | Flashes on each bird phrase |

## (Strongly) suggested patch

* **Audio Out 1** (drone) straight to **Mix**, volume knob about 12 o'clock.  
* **Audio Out 2** (birds and chimes) to top **Humpback Filter**. Filter FM at max, cut-off knob at 9 o'clock.   
* **Humpback Filter low-pass output** into **Mix**, volume knob at 1-2 o'clock.  
* **CV Out 1** to top **Humpback Filter FM In** (to open/close the filter slowly on birds and chimes as the sun rises).  
* **Bottom Slope** set to Loop mode, Up/Down shape, knob at 3 o'clock, to **CV In 1** (this varies the bird type).  
* **Top Slope** set to Loop mode, Up/Down shape, knob at 3 o'clock, to **CV In 2** (this varies the chime sound).  
* **X knob** at 12 o'clock  
* **Y knob** at 12 o'clock  
* **Main knob** (pitch) to taste

## Without the page

The card needs the page to know where the sun is. With nothing connected it sits at night: almost silent, apart from an occasional chime. Tap the switch down to preview daylight. If the USB link drops, the card holds its last values until the page reconnects.

## Protocol

USB CDC serial at 115200, newline-terminated ASCII. Ten times a second the page sends:

F el=-4.21 li=0.1830 ch=0.4200 ri=1 cl=0.400 wi=0.250 te=0.550 ra=0.000 hu=0.700

| Key | Meaning | Range |
| :---- | :---- | :---- |
| `el` | Solar elevation | degrees |
| `li` | Daylight (smoothstep from \-12° to \+12°) | 0–1 |
| `ch` | Birdsong (see above) | 0–1 |
| `ri` | Sun rising | 0 or 1 |
| `cl` | Cloud cover ÷ 100 | 0–1 |
| `wi` | Wind km/h ÷ 40 | 0–1 |
| `te` | (Temperature °C \+ 5\) ÷ 35 | 0–1 |
| `ra` | Precipitation mm ÷ 4 | 0–1 |
| `hu` | Relative humidity ÷ 100 | 0–1 |

The page also sends `HELLO`, and the card replies with its name and version. Twice a second the card sends `S fr=<frames> ch=<chimes> bi=<birds> li=<0-1000>`, which the page shows as counts.

The card ignores keys it doesn't recognise, and the mapping from raw weather to 0–1 lives in the page (`weatherAt`, `buildFrame` and `birdsong` in `web/index.html`). That means new data feeds can be added, or the mappings retuned, without reflashing. Anything that can write to a serial port can drive the card, for example a Python script or a Raspberry Pi in an installation.

Never open the port at 1200 baud: the Pico SDK treats that as a request to reboot into BOOTSEL.

## Building

Needs the Pico SDK (tested with 2.1.1) and `arm-none-eabi-gcc`:

cd src

mkdir build && cd build

cmake .. \-DPICO\_SDK\_PATH=/path/to/pico-sdk \-DPICO\_BOARD=pico

make

Core 0 runs ComputerCard's 48 kHz audio loop in integer maths. Core 1 reads USB serial, parses frames and smooths the values. The whole program runs from RAM (`copy_to_ram`) so the audio never waits on flash.

`sim/` compiles the card's `main.cpp` on a desktop with a stand-in for ComputerCard, plays a compressed day, and writes `day.wav`. It's useful for checking sounds and levels without flashing:

cd sim

cp ../src/main.cpp card.cpp

g++ \-O2 \-std=c++17 \-I. \-Dprotected=public \-Dprivate=public \-include cmath \-include cstring \-include algorithm sim.cpp \-o sim

./sim 0.6 0.3        \# wind and rain, 0 to 1

## Credits

Dawn Chorus by Joe Hedinger, designed and built with (significant) support from James Saunders. Vibe-coded with Claude. Weather data from [Open-Meteo](https://open-meteo.com/). Sun position from the NOAA solar calculator equations. With thanks to Matt Allison for the inspiration, and to Tom Whitwell for creating the Workshop System.