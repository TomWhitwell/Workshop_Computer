# Dawn Chorus

An ambient soundscape that follows the real sun and the local weather. A browser page works out where the sun is for your location, fetches the weather, and streams both to the card over USB. As the sun rises, a drone swells and brightens, chimes ring out and birds wake. Through dusk, everything settles back to silence. Because the weather is the score, no two dawns sound the same.

The birds are local and seasonal. The page finds the four birds most likely near you at this time of year, and the card gives each its own voice and songs: in a British June, screaming swifts, a blackbird, a robin and a blackcap, with a tawny owl at night. X picks which of them leads.

Inspired by the tides of Matt Allison's Equanimity card. A repo-ready release folder for the Workshop Computer repository is kept separately.

## What you need

- A Workshop System or Workshop Computer with this card flashed.
- A laptop or desktop running **Chrome or Edge**. Web Serial isn't available in Safari, Firefox or mobile browsers.
- A USB cable from the Computer's USB-C port to the laptop.
- An internet connection for the weather and bird records. Without one, the sun still works, mild default weather is used, and the card plays a general mix of birds.

## Getting started

1. Flash `firmware/dawn_chorus_v0.15.uf2` to a program card.
2. Power the system and connect the Computer to the laptop over USB.
3. Open `host/dawn-chorus-host.html` in Chrome or Edge (double-click it; nothing to install).
4. Set your location by searching for a town, choosing **Use my location**, or typing coordinates.
5. Click **Connect card** and choose the Raspberry Pi / Pico serial device.
6. The top-right LED goes solid and the page shows **Card is receiving**.

To hear a whole sunrise straight away, choose **Fast** on the page.

## The host page

The page has three time modes:

| Mode | What it does |
|------|--------------|
| **Live** | Real time and real weather. The card follows today's actual sun. |
| **Fast** | The real sun and forecast weather, played at 10× to 1440× from an hour before the next sunrise or sunset. |
| **Manual** | Sliders for sun height, rising or setting, and each weather value, for auditioning. |

A band across the top shows the day's sun path in sky colours, with sunrise and sunset times. The page remembers your location and mode.

The **Birds** panel shows your four birds and how much each is singing at this hour. Its **Season** menu follows the current month, or you can pick any month to hear another season.

## Local, seasonal birds

The page asks [GBIF](https://www.gbif.org), the Global Biodiversity Information Facility, which bird species have been recorded within 30 km of you in this month over the last ten years. In quiet areas it widens the search to 60 or 120 km. GBIF pools records from eBird, iNaturalist, national surveys and museums, and needs no account. Results are remembered in the browser for 30 days.

Birds in the same family tend to sound alike, so each species is sung as one of ten bird types, chosen by its family:

| Type | Examples | How it's made |
|------|----------|---------------|
| **Screamer** | Swifts | High, buzzy screams that dip in pitch as they pass, often in groups |
| **Flute** | Blackbird, song thrush, curlew | Mellow two-voiced phrases ending in a twitter, or short motifs repeated two to four times |
| **Warbler** | Robin, warblers, swallows | Thin, high notes with wide leaps, never quite repeating |
| **Trill** | Wren, chaffinch, finches | Very fast repeated notes; the chaffinch version speeds up and falls, then ends with a flourish |
| **Two-note** | Great tit, chiffchaff, sparrows | "Tea-cher, tea-cher" or "chiff-chaff" alternations |
| **Coo** | Pigeons and doves | Soft, low coos in the wood pigeon's or collared dove's rhythm |
| **Cuckoo** | Cuckoo | Two notes falling a minor third |
| **Harsh** | Crows, gulls, geese | Rough, noisy caws and falling gull calls |
| **Owl** | Tawny and barn owls | Hoots with a trembling last note, or a sharp "ke-wick" |
| **Skylark** | Larks | A long, high, endlessly varied warble |

A species' share of this month's records sets how likely it is, boosted for seasonal visitors and dropped when it's far below its usual level, since that means out of season or a stray record. Records over-represent big, conspicuous birds and under-represent songbirds compared with what you'd hear, so each type is rebalanced (`TYPE_BALANCE` in the page). Owls and cuckoos are rare in the records but unmistakable, so they're kept in contention (`CHARACTER_FLOOR`).

### Your four birds

The four most likely species become a fixed cast, in order of likelihood, with at most two of any one type. Each has a **personality** derived from its species: its own pitch, and a small repertoire of up to three songs that it repeats, as real birds do. Some types have two song styles, and the species decides which: blackbird or song thrush, wren or chaffinch, great tit or chiffchaff, wood pigeon or collared dove, crow or gull.

Each bird sings only at its time of day. Before dawn, robins and blackbirds start first, and the others join as the sun comes up. At night an owl, if one is recorded nearby, takes the fourth place and calls sparsely.

**X picks which bird leads**, in five positions. Fully anticlockwise, all four share the singing. Each step clockwise picks one to lead, from the most likely to the fourth. The lead gets about 85% of the turns. When it's mid-song (a bird never sings over itself), its turn goes to another bird, which sings a shorter phrase, a call rather than a full song, so there are fewer gaps but the lead still sings about twice as much as any other. A background bird only starts if it leaves a voice free for the lead. If the lead isn't awake at this hour, the others carry on until it is. Turning X to a new position plays that bird straight away, and the page highlights it.

The birds are always local. If the page has no bird data (no internet, or too few records nearby), the card plays a general mix of common garden birds instead. These are impressions of birds, not imitations.

## How the sky becomes sound

| Data | What you hear |
|------|---------------|
| **Daylight** | Rises smoothly from nautical twilight (sun 12° below the horizon) to the sun 12° up. It raises the drone's level and brightness, makes chimes louder, brighter, busier and higher, and drives CV Out 1. |
| **Birdsong** | Builds slowly from first light, peaks just after sunrise, settles to 80% through the morning, eases to 60% when the sun is high, lifts again for an evening chorus, and fades through dusk. In winter the sun stays low, so there's little midday lull. |
| **Cloud** | Dulls the drone. |
| **Wind** | Adds vibrato, beating and slow gusting swells to the drone, makes the chime echoes waver like tape, and makes chimes busier. Above about 14 km/h the chime notes bend too, by up to about 9 cents in strong gusts. |
| **Warmth** | Brightens the chime strike. |
| **Humidity** | Lengthens the echoes. |
| **Rain** | Adds a soft vinyl-style crackle with a faint hiss, independently on each output. |
| **Season and place** | Choose which kinds of bird sing (see above). |

Values from the page are smoothed on the card: over about a second for the sun and about four seconds for the weather, so changes arrive gradually rather than in steps.

## Outputs

| Jack | What it does |
|------|--------------|
| **Audio Out 1** | Drone: root, fifth and octave. Rain crackle. |
| **Audio Out 2** | Chimes through a feedback delay, plus birds. The birds skip the delay and have a light reverb of their own, so they sit in front like birds in nearby trees. Rain crackle. |
| **CV Out 1** | Daylight, 0 V at night to +5 V in full daylight. |
| **CV Out 2** | Pitch of the latest chime, 1 V/oct. |
| **Pulse Out 1** | 10 ms trigger on each chime. |
| **Pulse Out 2** | 10 ms trigger at the start of each bird phrase. |

## Inputs

| Jack | What it does |
|------|--------------|
| **CV In 1** | Moves X, choosing which bird leads. A slope here passes the lead from bird to bird. |
| **CV In 2** | Moves Y, stepping through the chime instruments. |
| **Pulse In 1** | Strikes a chime immediately. |
| **Pulse In 2** | Starts a bird phrase immediately, usually from the lead bird. |

With a cable in, the knob becomes an offset and 0 to +5 V covers its whole range. So with X or Y at 12 o'clock, a Slopes output looping up and down sweeps through every bird or instrument; turning the knob shifts where the sweep starts. Unplugged, the knobs work on their own.

## Controls

| Control | What it does |
|---------|--------------|
| **Main** | Key. 12 o'clock is D, and each step is a semitone. The drone, chimes and CV Out 2 follow, with a short glide. Turning it to a new key plays a chime. |
| **X** | Lead bird. Fully anticlockwise, all four share the singing; each step clockwise picks one of your four birds to lead. |
| **Y** | Chime instrument, anticlockwise to clockwise: marimba, harp, xylophone, bell, vibraphone, glockenspiel. Marimba, harp and xylophone use Casio CZ-style phase distortion; the others use two-operator FM. Turning it to a new instrument plays a chime. |
| **Switch middle** | Normal. |
| **Switch up** | Sparse: chimes and birds at 40% of normal. |
| **Tap down** | Preview full daylight for 10 seconds, then return to the real sky. |

Turning Main, X or Y to a new position plays the result straight away, so you can hear a change even at night, when the drone is silent and chimes are rare. Changes from CV In 1 and 2 don't add these extra notes, so a slope patched in doesn't make the card busier.

There's no volume control on the card, so set levels on the mixer.

## LEDs

| LED | Meaning |
|-----|---------|
| Top left | Daylight |
| Top right | USB link: solid while receiving, slow blink while waiting or after the link drops |
| Middle left | Birdsong activity |
| Middle right | Cloud cover |
| Bottom left | Flashes on each chime |
| Bottom right | Flashes on each bird phrase |

## Suggested patch

Send Audio Out 2 through one of the Workshop System's filters, with **CV Out 1** into the filter's FM input (attenuator fully up) and the cutoff at around 9 o'clock. The chimes and birds then open up warmly as daylight comes in. CV Out 1 works the same way on anything else in the system.

## Without the page

The card needs the page to know where the sun is. With nothing connected it sits at night: almost silent, apart from an occasional chime. Tap the switch down to preview daylight. If the USB link drops, the card holds its last values until the page reconnects.

## Protocol

USB CDC serial at 115200, newline-terminated ASCII. Ten times a second the page sends:

```
F el=-4.21 li=0.1830 ch=0.4200 ri=1 cl=0.400 wi=0.250 te=0.550 ra=0.000 hu=0.700 n=4 t0=0 s0=31052 w0=1.000 ... t3=2 s3=56804 w3=1.000
```

| Key | Meaning | Range |
|-----|---------|-------|
| `el` | Solar elevation | degrees |
| `li` | Daylight (smoothstep from -12° to +12°) | 0–1 |
| `ch` | Birdsong (see above) | 0–1 |
| `ri` | Sun rising | 0 or 1 |
| `cl` | Cloud cover ÷ 100 | 0–1 |
| `wi` | Wind km/h ÷ 40 | 0–1 |
| `te` | (Temperature °C + 5) ÷ 35 | 0–1 |
| `ra` | Precipitation mm ÷ 4 | 0–1 |
| `hu` | Relative humidity ÷ 100 | 0–1 |
| `n` | How many birds are in the cast. 0 means no local data, and the card plays a general mix. | 0–4 |
| `t0`–`t3` | Each bird's type, numbered in the order of the table above (0 screamer to 9 skylark) | 0–9 |
| `s0`–`s3` | Each bird's personality seed. The lowest bit chooses the song style within the type. | 0–65535 |
| `w0`–`w3` | How much each bird is singing at this hour | 0–1 |

The page also sends `HELLO`, and the card replies with its name and version. Twice a second the card sends `S fr=<frames> ch=<chimes> bi=<birds> li=<0-1000> x=<0-4> ld=<%> pk=<%> lt=<n>`. The page shows the counts, uses `x` (X's position: 0 all four, 1–4 the lead) to highlight the lead bird, and shows `ld` and `pk`, the average and peak audio load. `lt` counts audio interrupts that started late (ignoring the first second after power-on, while the card starts up); it should always be 0.

The card ignores keys it doesn't recognise, and the mapping from raw data to 0–1 lives in the page (`weatherAt`, `buildFrame`, `birdsong`, `fetchBirds` and `birdsNow` in `host/dawn-chorus-host.html`). That means new data feeds can be added, or the mappings retuned, without reflashing. Anything that can write to a serial port can drive the card, for example a Python script or a Raspberry Pi in an installation.

Never open the port at 1200 baud: the Pico SDK treats that as a request to reboot into BOOTSEL.

## Building

Needs the Pico SDK (tested with 2.1.1) and `arm-none-eabi-gcc`:

```
cd firmware
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico
make
```

The card runs at 200 MHz, the RP2040's fastest officially supported clock (`PICO_USE_FASTEST_SUPPORTED_CLOCK` in `CMakeLists.txt`; the SDK raises the core voltage to suit). The sample rate comes from ComputerCard's separate 48 MHz ADC clock, so it's unaffected.

Core 0 runs ComputerCard's 48 kHz audio interrupt, in integer maths, at the highest interrupt priority. Its control work (knobs, wind, drone, envelopes, triggers) runs at 3 kHz, spread over separate samples so no single sample carries much of it. USB also starts on core 0, because the Pico SDK only allows that, but its interrupts run at a lower priority, so they can't hold up the audio. Core 1 reads incoming frames, smooths the values, and plans bird phrases ahead, so starting a phrase in the interrupt is just a copy.

If USB ever fails to start, all six LEDs flash together twice a second, and the page won't be able to connect. The whole program runs from RAM (`copy_to_ram`) so the audio never waits on flash.

This matters because ComputerCard reads the knobs, switch and CV inputs through a multiplexer that it steps once per sample. If the audio interrupt runs late, by overrunning its time or being held up by another interrupt, the ADC readings can slip channels, and the knobs and switch then read the wrong inputs. Versions up to 0.9 did their control work in a single sample and planned bird phrases inside the interrupt, and that is what scrambled the controls. (Version 0.10 started USB on core 1, which the SDK silently refuses, so it couldn't connect at all.)

`sim/` compiles the card's `main.cpp` on a desktop with a stand-in for ComputerCard, so you can check sounds, levels and controls without flashing. `day8.cpp` plays a compressed June day with a four-bird cast and writes `june8.wav`:

```
cd sim
cp ../firmware/main.cpp card.cpp
g++ -O2 -std=c++17 -I. -Dprotected=public -Dprivate=public -include cmath -include cstring -include algorithm day8.cpp -o day8
./day8 300           # the number is X, from 0 to 4095
```

`pick_preview.cpp` steps X through its five positions (writes `pick.wav`), `check10.cpp` tests the knobs, CV sweeps, switch and bird picking, `knobturn.cpp` checks that turning a knob plays once and doesn't repeat, and `leadfill_detail.cpp` measures each bird's share of the singing at each X position. Build them the same way.

## Credits

Dawn Chorus by Joe Hedinger. Weather data from [Open-Meteo](https://open-meteo.com/). Bird records from [GBIF](https://www.gbif.org/), the Global Biodiversity Information Facility, and its contributing datasets. Sun position from the NOAA solar calculator equations.
