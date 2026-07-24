# Ouroboros

A tape loop, eating its own tail.

The buffer is the tape: a ring of cells that one head assembly travels around.
As the head passes each cell it plays back what came round from the last
rotation (that's the echo), then records input + feedback over it. There is no
separate "delay time" — the delay *is* one rotation of the loop, so tape length
and tape speed both set it, exactly like a real spliced reel.

Built as a playground for a physical 1/4" reel-to-reel tape loop rig (extra
playback heads, Frippertronics-style sound-on-sound) — a way to rehearse the
performance moves before the razor blades come out.

## Panel

| Control | Function |
| --- | --- |
| **Main knob** | Feedback, 0 → ~112%. Unity is around 3 o'clock; the last stretch of the knob makes the loop *grow* each rotation until tape saturation catches it. |
| **X knob** | Tape length, ~100ms → ~4s (square law — the bottom half is slapback territory). Length changes glide, so the splice moves rather than teleports, and lengthening splices in a *copy* of the loop — the bed keeps playing and just takes longer to come round. |
| **Y knob** | Tape speed: varispeed, ±1 octave, centre = normal. Turning it Doppler-shifts everything already on the tape, then new material settles back at unity — like grabbing the reel. Slow tape is longer *and* darker. |
| **Switch up** | **Freeze** — the record head lifts; the loop just spins, forever. Input is ignored. |
| **Switch middle** | Echo — sound-on-sound with regeneration. |
| **Switch down** (momentary) | **Reverse** — the tape runs backwards while held. Playback only, like real tape. |

## Patching

| Jack | Function |
| --- | --- |
| Audio In 1 | Input |
| Audio Out 1 | Wet — the playback head |
| Audio Out 2 | Dry thru (mix them yourself, or hard-pan) |
| CV In 1 | Tape speed, 1V/oct, adds to Y (total range ±2 octaves) |
| CV In 2 | Feedback, adds to the main knob |
| Pulse In 1 | Freeze gate — high = frozen |
| Pulse In 2 | Reverse gate — high = reversed |
| Pulse Out 1 | **Splice tick** — a 10ms pulse each time the splice passes the head. Clock a drum voice from it and the kit locks to the tape rotation, whatever the length and speed. |
| Pulse Out 2 | High while frozen |
| CV Out 1 | Envelope follower of the wet output, 0..+6V |
| CV Out 2 | Tape position: a 0..+6V ramp, once per rotation — a loop-synced LFO |
| LEDs | A dot chases round the panel once per rotation, brightness following the wet level |

## Times

The tape is 96K cells ticking at 24kHz when Y is centred, so the length knob
alone reaches ~4.1s. Speed multiplies that — and bandwidth falls with it, the
way real tape does:

| Tape speed | Max loop | Character |
| --- | --- | --- |
| +1 octave (Y full up) | ~2s | bright, tight |
| centre | ~4.1s | ~10kHz bandwidth, cassette-ish |
| −1 octave (Y full down) | ~8.2s | dark |
| −2 octaves (with CV) | ~16.4s | ~3kHz, murk |

A fixed, gentle wow (a couple of cents at ~0.7Hz) wobbles the playback head.
The feedback path has a low-pass that runs at cell rate, so repeats darken
faster on slow tape, and a soft saturation knee so runaway feedback compresses
into tape grit instead of hard square-wave clipping.

## Performance moves

- **Sound-on-sound:** long tape, feedback around 2–3 o'clock, keep playing.
  Each pass sinks darker into the bed.
- **Catch and hold:** flick the switch up the moment something good comes
  round. Then play over it, or drag Y down and it drops an octave, slow and
  huge.
- **Rocking the reel:** hold the switch down (it's momentary) in rhythm —
  the tape rocks backwards and forwards. Works best frozen.
- **Runaway:** main knob full, feed it one note, walk away. Pull the length
  knob around while it screams — the loop re-splices itself.
- **Tape as clock:** Pulse Out 1 into a drum trigger, CV Out 2 into anything
  slewed. The whole patch breathes at the loop rate.

## Simulator

No hardware needed to hear it:

```
cd sim
./build.sh
```

Compiles the real card source against a mock hardware layer (g++ only, no Pico
SDK), runs self-checks over the tape mechanics — echo period, splice-pulse
timing, freeze retention, reverse, feedback runaway and decay — then renders
demo WAVs (wet left, dry right): `out_echo.wav`, `out_runaway.wav`,
`out_freeze_varispeed.wav`, `out_reverse.wav`.

## Building the firmware

```
cd src
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Needs the Pico SDK (`PICO_SDK_PATH`). Flash `build/ouroboros.uf2` in the usual
way. The audio interrupt is fully fixed-point — no float library calls — and
the 192KB tape lives in .bss with ~65KB of RAM to spare.

## Status

Passes all simulator self-checks. Not yet tested on hardware.
