# Punk Confusion Vocal Samples

The WAVs in this folder are the editable source for `../VocalSamples.h`:

- `marquee_oi.wav` -> `Marquee`
- `cbgb_hey_ho.wav` -> `CBGB`
- `club100_no_future.wav` -> `100 Club`
- `whisky_lets_go.wav` -> `Whisky a Go Go`

Expected format:

- mono WAV
- signed 16-bit PCM
- 24 kHz sample rate
- short enough that all four samples plus firmware fit comfortably on a 2 MB card
- conservative peak level, around `-6 dBFS`, to avoid extra digital clipping after overdriven source processing

To replace the calls, overwrite these WAVs, then run from the card folder:

```sh
python3 tools/generate_vocal_samples.py
```

That regenerates `VocalSamples.h`, which is compiled directly into the UF2.

The included vocal calls are original recordings by Adrian Vos, processed for
Punk Confusion. They are released with this card under the same MIT license.
