# Cosmik M1N1

Cosmik M1N1 is a Minimoog-influenced monophonic voice for the Music Thing
Modular Workshop Computer. It combines two internal band-limited oscillators,
an external OSC3 pitch/CV loop, a four-pole resonant filter, noise, ADS
contours, a free-running LFO, USB MIDI, and Web MIDI preset management.

## Release 1.0.0

Flash `uf2/COSMIK_M1N1_1.0.0.uf2`. The release UF2 is built from the accepted
musical-glide firmware and is accompanied by `SHA256SUMS.txt`.

## Voice

- OSC1 and OSC2 offer triangle, sharktooth, saw, square, wide rectangle, and
  narrow rectangle waveforms. Nine pitch-selected harmonic bands reduce
  aliasing while preserving distinct waveform character.
- Each internal oscillator has a `32'`--`2'` pipe-length range, a seven
  semitone fine range around noon, its own level, and waveform selection.
- External OSC3 uses `CV Out 1` for calibrated pitch, `Audio In 2` as its
  mixer return, and `CV In 2` as an external modulation source. C3 is 0 V,
  matching the Workshop System and Keystep convention.
- `Audio Out 1` is the filtered voice. `Audio Out 2` is the pre-filter mixer.
- White and pink noise have independent level and colour selection.

## Front Panel

- **Middle:** Main is cutoff, X is resonance/emphasis, and Y is filter
  contour.
- **Up:** edits the selected OSC1, OSC2, or external OSC3 page. A short Down
  press cycles the selected oscillator page. Main controls pitch/offset, X
  controls level, and Y selects waveform for internal oscillators or range for
  external OSC3.
- **Down, short hold:** Main is external OSC3 offset, X is MOD DEPTH, and Y is
  pitch/filter modulation balance. MIDI CC1 adds temporary depth here without
  changing the stored setting.
- **Down, held longer than half a second:** Main controls noise level, X
  selects white or pink noise across a small noon dead zone, and Y is MOD MIX.
- **Preset selection:** hold Down at startup, or hold it for five seconds
  during operation. LED 4 warns after four seconds. Factory voices and only
  populated user slots are available; LED 5 identifies the user bank.
- Soft pickup prevents knob jumps whenever the switch position or oscillator
  page changes.

## Filter, Contours, And Modulation

`Audio Out 1` uses a four-pole resonant ladder filter with accepted low-pass
and high-pass modes. The Web UI supplies cutoff mode, emphasis, filter contour,
independent amplifier and filter ADS controls, independent decay-release
switches, and None/1/3/2/3/Full keyboard tracking.

MOD SRC A selects External OSC3 / CV In2 or Filter Contour. MOD SRC B selects
Noise, external modulation on CV In2, or the internal LFO. MOD MIX crossfades
those sources; MOD DEPTH and pitch/filter balance set their amount and
destination. The internal LFO is free-running, triangle or square, and covers
0.05--14 Hz. Oscillator and filter modulation can be independently enabled.

Glide is preset-backed and acts on the underlying MIDI or 1 V/oct pitch before
modulation. `CV Out 1` follows the same glide, keeping a patched external OSC3
in step with OSC1 and OSC2.

## Inputs And MIDI

- `Audio In 1`: calibrated 1 V/oct pitch CV for OSC1 and OSC2.
- `Audio In 2`: external OSC3 audio return.
- `CV In 1`: positive filter-cutoff modulation.
- `CV In 2`: external OSC3 or other external modulation source.
- `Pulse In 1` and `Pulse In 2`: gate the amplifier and filter contours.
- `Pulse Out 1`: mirrors `Pulse In 1`; `Pulse Out 2` is held low.
- USB MIDI: mono, last-note-wins note on/off, pitch bend at +/-2 semitones,
  and CC1 modulation depth. An active MIDI note takes pitch priority over
  `Audio In 1`; gate sources remain ORed, so a pulse gate can hold a contour
  after MIDI note-off.

## Web MIDI

Serve `web/` locally in Chrome or Edge, connect the card with the Web MIDI
button, then use the complete voice editor, eight factory voices, and eight
named card-backed user slots. The UI can save, recall, duplicate, and delete
user voices, provides a developer MIDI monitor, and polls live card changes
back into the interface. Earlier user slots migrate safely with Glide at zero.

## Release Verification

Release 1.0.0 passed the following Workshop System and Keystep tests:

- OSC1, OSC2, and external OSC3 range/pitch tracking, including glide.
- Harmonic-band transitions and distinct waveform character.
- Low/high-pass filter mode, contour, keyboard tracking, resonance, and ADS.
- White/pink noise, modulation-source routing, internal LFO, and MIDI CC1.
- USB MIDI, calibrated pitch CV, pulse gates, MIDI/CV priority, and pulse
  mirror output.
- Factory/user preset selection, Web MIDI sync, save/recall/delete, and
  persistence across power cycles.

## Build

Set `PICO_SDK_PATH` to a Raspberry Pi Pico SDK checkout, then run:

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=1
cmake --build build -j2
```

## Provenance

The initial platform, USB MIDI host support, and lookup-table framework derive
from `Workshop_Computer/releases/101_Gnarly_C1ZZL3` at commit
`bf4ecbbed2f2075a6d008f2cba18f70502593b86`. The inherited code and licence
notices remain under the MIT License.

Cosmik M1N1 uses the ComputerCard framework by Chris Johnson. The bundled
`computercard.h` is the canonical ComputerCard 0.4.0 release under the MIT
License.

The USB MIDI host driver files retain the MIT copyright notices from rppicomidi
(2023). Raspberry Pi Pico SDK and TinyUSB are external build dependencies and
are not vendored in this release directory.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it, or distribute the ZIP produced by
`license_tools/package_uf2.py` from the releases directory.
