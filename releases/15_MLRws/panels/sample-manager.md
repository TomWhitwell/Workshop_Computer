# Sample manager

The MLRws sample manager is a tiny web app that runs in your browser and talks
to the Workshop Computer over USB-serial. You use it to upload audio files
from your (non-Workshop) computer onto the card, pull recordings *off* the
card as WAVs, audition or clear individual tracks, and trim/select regions
before upload. It is the simplest way to put curated source material on the
card without having to play it into the audio input.

- Web app: <https://dessertplanet.github.io/MLRws-web/>
- Source: [`dessertplanet/MLRws-web`](https://github.com/dessertplanet/MLRws-web)

![MLRws sample manager](https://raw.githubusercontent.com/TomWhitwell/Workshop_Computer/main/releases/15_MLRws/docs/images/web.png)

## Getting connected

1. Power-cycle the Workshop Computer into a **USB-device** mode. The easiest
   path is to power up with nothing else plugged into the Computer's USB-C
   port. MLRws comes up in [Gridless mode](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/15_MLRws/panels/gridless.md) and will transition
   to sample-manager mode automatically once the web app connects. (If you
   power up *while already plugged into your host computer*, MLRws can also
   boot straight into sample-manager mode.)
2. Plug the Computer into your laptop/desktop with USB-C.
3. Open the web app, click **Connect**, and pick the Computer's serial port
   from the browser prompt. The status line will report `Connected`, and then read
   each existing track from the card so you can see and audition them.

> [!NOTE]
> This web app requires the Web Serial API to communicate with the WS computer, so a Chromium-based browser is required. [Details below](#browser-and-web-serial-caveats)

## What you see per track

For each of the six tracks the app shows:

- A **Speed** dropdown. See [Record speed and length](#record-speed-and-length).
- With firmware 1.2.1 or later, a **Pan** slider (L6 through C to R6) and a
  **Filter** dropdown (250 Hz, 600 Hz, 1.5 kHz, 4 kHz, open). These set the
  same per-track pan and low-pass filter as the grid's REC page, and **Preview**
  plays through them. Older firmware shows a **Channel** selector instead (see
  [Audio format](#audio-format)).
- A **CV output** toggle for tracks that already contain audio. Empty tracks
  always send CV1 pitch, gate CV2, and drive pulse outputs from CUT-page grid interactions on their row;
  populated tracks default to off until enabled here. The toggle is disabled
  while the web app is disconnected.
- A **waveform** view with the current crop selection drawn in blue.
- The action buttons described below.
- A small info line showing the current selection length, the maximum length
  at the chosen speed, and (for tracks that were originally recorded on the
  Computer panel) the speed they were recorded at (e.g. `rec 0.5×`).

You can load source audio by clicking **Load Audio** to pick a file, or by
**dragging an audio file straight onto the track row**. Both work the same
way and accept whatever the browser can decode (WAV/AIFF/MP3/FLAC/etc.).

## Waveform interaction

The app runs a transient detector on every loaded file and draws each
transient as a small tick on the waveform. The transients become **sector
boundaries** for selection:

- **Click** a sector to select just that sector as the crop region.
- **Click-and-drag** across the waveform to expand the selection to all
  sectors between the start and end of the drag.
- **Right-click** anywhere on the waveform to add a manual sector boundary at
  that position (useful if the auto-detected transients miss a cut point).

When you change the Speed dropdown while a long file is loaded, the app will
snap the end of the crop to the nearest transient before the per-speed length
limit (or hard-clamp if no good transient is available).

## The action buttons

- **Load Audio**: open a file picker. (Drag-and-drop onto the row works too.)
- **Upload**: ADPCM-encode the current selection at the chosen record speed
  and write it to the card for this track. Replaces any existing audio. Loaded
  source files are peak-normalized before upload and preview.
- **Download**: read whatever is currently on the card for this track,
  decode it, and save it as `trackN.wav` via your browser's download flow.
- **Crop**: commit the current crop selection to the audio buffer in the
  browser (a destructive trim of the *working* file, not the card). Useful
  for refining the selection before re-cropping or uploading.
- **Clear**: erase this track on the card. The browser will ask you to
  confirm first.
- **Preview**: play the current track buffer through your laptop's audio
  output using Web Audio, encoded exactly the way it would be uploaded, with
  a playhead drawn on the waveform. With firmware 1.2.1 or later it's panned
  and filtered the way the card will play it (the filter's response matches
  the card's; only its soft saturation on very loud input is left out). The button becomes **Stop** while
  playing; preview loops until you stop it. Changing the speed while playing
  automatically re-starts the preview at the new setting.

## Card settings

With firmware 1.2.1 or later, a settings box above the tracks sets how the
card's CV inputs behave in grid mode (see [CV inputs](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/15_MLRws/panels/grid.md#cv-inputs)):

- **CV In 1**: pan offset. Tick **last touched** to follow the last track row
  you pressed on the grid, or tick any set of tracks 1–6 to offset them all.
- **CV In 2**: choose **Filter cutoff** or **Rotation**, then which tracks it
  acts on, the same way as CV In 1. In rotation mode, **range** sets how far
  +5 V turns the recording (1–100% of the track).

Changes reach the card straight away and are saved to flash when the card
leaves sample-manager mode.

## Record speed and length

The Speed dropdown controls the **record speed** the audio is encoded at, in
the same sense as recording on the grid mode REC page. Slower record speeds
fit more seconds onto the card in exchange for fidelity (ADPCM at a lower
sample rate). The options are **0.25×**, **0.5×**, **0.67×** and **1×**
(default).

Maximum length per track at each speed (firmware 1.2.1 and later):

| Record speed | Max per track |
|--------------|---------------|
| 1x           | 26.3 s        |
| 0.67x        | 39.3 s        |
| 0.5x         | 52.6 s        |
| 0.25x        | 105.1 s       |

When recording
on the Computer panel itself (grid mode REC page) you can pick faster-than-1×
speeds too. Those tracks will still be displayed and downloadable here, but
the dropdown for uploads is capped at 1× because going faster doesn't give
you anything useful for a pre-prepared sample.

## Audio format

MLRws stores one mono ADPCM stream per track, at 24 kHz for 1× (firmware 1.2.1 and later).

With firmware 1.2.1 or later, stereo source files are mixed down to mono (left + right) before encoding, and each track's **Pan** places it between Audio Out 1 and Audio Out 2. A track's pan and filter are sent to the card along with each upload, so a fresh upload keeps the pan you set rather than jumping hard left.

With older firmware, each track has a Channel 1/2 selector instead; that selector chooses the stored output routing and, for multi-channel source files, which source channel is encoded. Channel 1 uses source channel 1, Channel 2 uses source channel 2 when present, and single-channel files can be assigned to either output channel.

Each populated track also has a **CV output** toggle. When it is enabled, touching CUT keys on that track row updates the CV1 pitch sample-and-hold, gates the CV2 ASR envelope, fires Pulse Out 1, and can hold Pulse Out 2 high as a gate. When it is disabled, the track still cuts and can be played normally, but it leaves the CV and pulse outputs unchanged. Empty tracks always update CV1 pitch, gate CV2, fire Pulse Out 1, gate Pulse Out 2 while held, and show the E-major keyboard guide; the pitch map runs left-to-right by semitone with guitar-like row tuning, and repeats on the bottom half of a 16-row grid. Once a track has audio, CV output defaults to off until enabled in the web app.

Uploading always writes only the selected track. With older firmware, use the Channel selector on stereo source files to choose whether source channel 1 or source channel 2 is encoded and which output channel the stored track uses.

## Serial transfer notes

The sample-manager protocol is framed so Web Serial packet boundaries do not matter:

- `I` returns a 32-bit little-endian metadata length followed by that many bytes of text metadata.
- `R <track>` returns a 32-bit little-endian track length, then streams the track blob in 1024-byte chunks. The browser acknowledges each chunk with `A`; the firmware sends `DONE\n` after the final acknowledged chunk.
- `P <track> <enabled>` updates the populated-track CV output setting.
- Firmware 1.2.1 adds settings commands. Each value byte has its top bit set
  (`0x80 | value`) so it can never be mistaken for the `X` resync, and each
  replies `OK\n` or `ERR\n`:
  - `G <0x80|percent>`: CV In 2 rotation range, 1–100%.
  - `M <0x80|mode>`: what CV In 2 drives: 0 = rotation, 1 = filter cutoff.
  - `C <0x80|input<<6|mask>`: the tracks CV In 1 (input 0) or CV In 2
    (input 1) act on; mask bit *t* = track *t*+1, 0 = last touched.
  - `N <track> <0x80|pan+6>`: a track's pan, −6 (hard Out 1) to +6 (hard Out 2).
  - `L <track> <0x80|slot>`: a track's filter, 0–4 (4 = open).

  The `I` metadata header reports these as `ROT <pct> CV2 LPF|ROT CV1M <mask> CV2M <mask>`,
  and each track line ends with its pan and filter slot.

The web app remembers the selected serial port during the current browser session and attempts to reconnect automatically after a USB disconnect. During reconnect it clears the displayed track buffers first, then repopulates them from the card once sync succeeds.


## Browser and Web Serial caveats

The app talks to the Computer via the [Web Serial API], which is not as
universally supported as the rest of the web. Things to know:

- Use a **Chromium-based browser**: Chrome, Edge, Brave, Opera, Arc, etc.
  **Firefox and Safari do not support Web Serial** as of this writing and
  the Connect button will do nothing useful there.
- Only **one tab** can hold the serial port at a time. If a previous tab is
  still connected (or a different app like the Arduino IDE is), you'll need
  to close it before connecting.
- On Linux, your user typically needs to be in the `dialout` (or `tty`) group
  to access serial devices. macOS and Windows generally work out of the box.

[Web Serial API]: https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API
