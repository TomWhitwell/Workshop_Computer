# VODER — hardware checklist

An ordered session with the card. The order matters: each step depends only on
things already confirmed, so when something fails you know the fault is in what
you just added rather than anywhere in the card.

**This is not a first bring-up.** The card has been played across many sessions
and every section below names a fault that was actually heard on hardware and
fixed. It is therefore a *regression* checklist — if one of these symptoms
returns, the named cause is where to look first rather than starting again.

**Flash `UF2/tract8.uf2`** (hold BOOTSEL, drag, it reboots on its own).

---

## 0. Before you plug anything in

- Headphones or a monitor on Audio Out 1 or 2.
- Nothing patched into the inputs. The card makes sound on its own and a bare
  start removes a whole class of confusion.
- The 8mu stays unplugged until section 6. Every control below is reachable
  from the panel alone, which is deliberate.

---

## 1. It boots, and it says which mode

Power on and watch the LEDs.

- A **chase** across all six while the switch is still settling.
- Then **even LEDs (0, 2, 4)** — BABBLE, the default.
- Hold the switch **Down** at power-on instead and you get **odd LEDs
  (1, 3, 5)** — the deliberate mode.

The mode is read *once*, after the full boot window. If the splash never
resolves to a steady pattern the switch is not being read.

## 2. It makes a sound at all

In BABBLE with nothing patched, the card should be talking to itself quietly.
Turn Main slowly: the vowel and pitch sweep together.

**If it is silent**, this is the fault this card has had more than any other,
and the panel will tell you which one. Hold the switch **Down** and read
LEDs 0–3:

| LED | Meaning if lit |
|---|---|
| 0 | Excitation gated shut — no buzz, no noise |
| 1 | External input has taken over from the internal sources |
| 2 | Formant freeze is latched |
| 3 | All eight band gains are near zero |

If LED 1 is lit with nothing patched, `kExtGateLevel` is below this unit's ADC
noise floor. If LED 0 is lit, noise on Pulse In 2 is tripping the gate latch.

If none of the four is lit and it is still silent, suspect the filter bank
itself — a v1.0.1 build was mute because the coefficient tables were still
zero when the bank copied them. There is now a guard that falls back to
pass-through rather than silence, so a mute bank should sound *wrong* rather
than *absent*.

## 3. The knobs each do something

**BABBLE, page 1** (switch middle or down):

- **Main** — vowel and pitch sweep together
- **X** — syllable rate and brightness, 2 to 20 Hz
- **Y** — rounded hum at one end, breathy whisper at the other

**Page 2** (switch up): Main is voice size, X is animation, Y is consonants.

Every knob must move the sound on every page. A knob that does nothing was a
real fault twice — once from a latch that stopped the morph writing after any
fader moved, once from a control the 8mu had silently seized.

## 4. The momentary switch as a gate

Hold the switch **Down** with nothing patched. **The card should sound for the
whole hold** and stop when you let go.

If it makes one click on the press and then goes quiet, the switch is still
wired as a plosive key rather than into the gate — the pre-1.0.1 behaviour.

If it stutters instead of sustaining, it has been put into `chatter_gate`
rather than overriding it. A gate from a sequencer is a stream of events and
should chatter; a finger on a button is one continuous intent and should not.

If it sounds while held but the card is **permanently mute after you release
it** with nothing patched, the switch is latching `gate_seen_`. It must not:
that latch is what keeps a misdetected Pulse In 2 from muting the card, and a
switch that sets it reintroduces the first hardware run's silence bug by the
back door. `silence_check.py` check 5 covers exactly this.

## 5. Auto-chatter

Hold the momentary switch for **two seconds** in BABBLE. The LEDs fill left to
right as a progress bar, **and the card sounds throughout the hold**, then it
starts talking by itself in phrases with pauses long enough to read as
breaths. Tap the switch to stop.

If the hold is silent, the switch is not reaching the gate — see section 4.

If the phrases sound staccato and evenly spaced rather than like speech, the
syllable duty is wrong.

## 6. The 8mu

Plug it in. **LED 4 lights** when it mounts.

- **Faders 1 and 8** are the vowel axes — the outermost pair, so a hand spans
  the device. These get played most, and they are the two that were reported
  as "not doing anything" when the mapping was wrong.
- **Fader 7** is volume, **fader 5** brightness, **fader 3** breath,
  **fader 4** pitch.
- **Buttons 2 and 4** add breath as well as their own function.
- **Tilt left/right** is rounding, the third vowel axis.

**The accelerometer sends LIFT magnitudes, not bipolar axes.** A level 8mu
sends 0 on all four; they do not add to 127 and there is no detent at 64. Six
versions got this wrong before the device was asked directly.

**If the 8mu goes quiet but its lights stay on**, it is not dying — the card is
discarding its messages. Three separate causes have been fixed (an unbounded
drain in the RX callback, a wedged endpoint from parsing inside the callback,
and a stale device address that made every later mount fail). It should now
recover on its own from any of them. Sustained fader plus accelerometer traffic
is what provoked it.

## 7. The jacks

Every CV input **adds** to its control rather than replacing it, so nothing
goes dead when a cable goes in.

| Jack | What to expect |
|---|---|
| Audio In 1 | Sums with the internal sources — the card speaks *over* what you patch, both shaped by the same vowel |
| Audio In 2 | Bipolar volume CV, swells and ducks around the fader |
| CV In 1 | 1V/oct pitch |
| CV In 2 | Sweeps the vowel cube diagonally — patch a sample-and-hold here |
| Pulse In 1 | Plosive burst on the rising edge |
| Pulse In 2 | Glottal gate |

Patch a bipolar LFO into CV In 2 and check the vowel sweeps **symmetrically**
at any fader position. A half-wave rectified sweep — the vowel moving for only
half the cycle, with the DC offset shifting as faders 1 and 8 move — was a real
fault, caused by clamping the CV at zero instead of scaling it into the
headroom the faders leave.

## 8. Level and cleanliness

On a scope, an ordinary vowel should span roughly **±2.8 V with momentary peaks
near ±4.2 V**, against outputs that reach ±6 V.

The output stage is a limiter with a hard knee at 4.4 V: below that it is
*exactly* linear, so ordinary playing is undistorted and only genuine
transients bend.

**Listen for a whine that tracks the volume and brightness faders.** That was a
cubic soft clipper, which sounds like the gentler choice and is not — a cubic
curves from the very first sample, so it has no linear region and distorts
everything rather than just the peaks. It measured 28 dB of THD on a normal
vowel; the hard knee measures 92 dB down.

Also listen for **high-frequency hash when moving a control**. Several distinct
causes were found and fixed: a control-rate zipper, a gain slew that stalled
because an arithmetic shift of a small delta is zero, ADC jitter on the knobs,
an unsmoothed CV multiplying the output, and the panel read beating against
ComputerCard's 4-state input mux. If it returns, the *period* identifies it —
250 µs is the mux beat, 8 ms is the control rate.

## 9. CV outs

- **CV Out 1** — formant energy envelope, ~5 ms smoothing.
- **CV Out 2** — the vowel's openness axis, 0–5 V closed to open. In BABBLE it
  wanders on its own, so it is a modulation source related to what you are
  hearing. Patch it at a filter and the patch moves with the voice.

## 10. What is still unproven

- **Deliberate reproduction of the USB lockup.** Three causes were fixed and
  absence of reports is not proof. Sustained fader plus accelerometer traffic
  is what used to provoke it.
- **Whether Q=4 is right.** It balances band separation against ringing on
  plosives, and only ears settle it.

---

## Measured, not modelled

| | |
|---|---|
| DSP load | **60% typical, 70% peak** of the 20.83 µs budget |
| Flash / RAM | 2.17% / 21.20% |
| Vowel level | 528 DAC counts pre-limiter, 1431 at the output |
| Build | zero warnings from this card's sources |

The DSP load figure matters because the *model* predicted 44.2% while the
hardware read 100% — the filter bank was using `int64` accumulators, and on an
M0+ every 64-bit multiply is a call to `__aeabi_lmul` rather than an
instruction. Rewriting the bank in 32-bit arithmetic took it to 60%. **Read CV
Out 2 rather than trusting `tools/budget_check.py`**, which says as much itself.
