# Bench test plan — 2026-09-04

> # RESOLVED — THE LINK WORKS, AND IS CHARACTERISED (2026-09-07)
> Multiboot 100 kHz · 2000 words/s sustained (~64 kbit/s) · round trip ≤0.5 ms ·
> 32 KB payload uploads in ~5 s. Full record and the measurement mistakes made along the
> way: [`POSTMORTEM.md`](POSTMORTEM.md).
>
> The GBA boots, runs the payload, shows *"MTM - Workshop Computer Link"*, and reports its
> buttons back. **For what actually went wrong and why, read
> [`POSTMORTEM.md`](POSTMORTEM.md).** This plan is kept as the re-test procedure for a rewire,
> a different cable, or a different console — work it top to bottom and it re-derives the
> whole configuration from scratch.

Written after a full audit against [GBATEK](https://problemkaputt.de/gbatek.htm). Two new
things came out of it: **a firmware timing bug** (we have probably been sampling on the wrong
clock phase all along) and **a cable ambiguity that firmware cannot fix but can now identify**.

Work top to bottom. Each step says what to do, what each outcome means, and where to go next.

---

## What the audit found

### 1. The sample edge was probably wrong — and the old notes pointed the wrong way

GBATEK, *SIO Normal Mode*, is unambiguous:

> "During inactive transfer, the shift clock (SC) is high."
>
> "When master sends SC=LOW, each master and slave must output the next outgoing data bit to
> SO. When master sends SC=HIGH, each master and slave must read out the opponents data bit
> from SI."

So the link is **SPI mode 3**: SC idles HIGH, both ends **drive on the falling edge** and
**read on the rising edge**.

Our scope session already confirmed the polarity half of this by accident: with the state
machine stalled on `side 0`, the SC jack sits **HIGH at 6 V** — because Pulse Out 1 inverts.
That is exactly GBATEK's idle state, so `GBA_SCK_OUTOVER = NORMAL` is right.

**But `BRINGUP.md` has been telling us to use `gba_spi_cpha0`**, which samples while the jack
is LOW — during the *drive* phase, the opposite of what GBATEK specifies. `linkcheck` defaulted
to it too. That guidance came from reasoning about what the reference uploaders do, never from
a measurement, and it looks wrong.

The applet itself (`gba_multiboot.cpp`, using `gba_spi`) has **always been on the correct
timing**. It is the diagnostics that have been testing the wrong thing.

### 2. The cable crossover is a real, unresolved fork

A GBA↔GBA link cable **crosses SO/SI between its two ends**; a GameCube→GBA cable does not.
Sources disagree in their wording about exactly how GBA cables are wired, and your cable has
already surprised us once. So this is settled **by measurement, not by reading**.

If your cable crosses and the socket is wired by the GBA pinout (pin 2 = read, pin 3 = drive):

* we **drive our data into the GBA's SO pin** — one of its *outputs* (contention), and
* we **listen on the GBA's SI pin** — an *input*, so we only ever read our own pull-up.

Everything looks wired, nothing works, and no amount of edge/polarity sweeping helps. That
matches this session's symptoms exactly.

### 3. GBATEK gives a readiness signal we were ignoring

The master init sequence says: **"Wait for SI to become LOW (slave ready). (Check timeout
here!)"** A GBA waiting for multiboot pulls its SO low. We never checked for it — and it can
be detected with **no transfer at all**, so it is independent of every timing question.
`cablecheck` MODE 2 watches for it.

### 4. Hardware compatibility (asked, and checked)

* **GBA SP (AGS-001 / AGS-101) is pin- and electrically compatible** with the original
  AGB-001 link port. Either console is fine.
* **Game Boy Micro is NOT** — different, smaller connector. Do not use one.
* Pinout is confirmed identical across sources, including GBATEK's Xboo PC→GBA multiboot
  cable, which is the closest published analogue to what we are building:
  **pin 2 = SO (we read), pin 3 = SI (we drive), pin 5 = SC (we drive), pin 6 = GND.**
* GBATEK notes the Xboo cable's PC side runs 5 V against the GBA's 3 V logic — the same
  mismatch as our 6 V pulse outputs, and the reason for the series resistors.

---

## ✅ RESULT (2026-09-06): THE CABLE IS **CROSSED**

STEP 1 has been run and answered. `cablecheck` MODE 0, GBA settled on the logo:

* **LED0 (Pulse In 1 = the SO-labelled wire): no activity.** That wire reaches the GBA's
  *input*, so it is high-Z and sits on our pull-up.
* **LED1 (Pulse In 2 = the SI-labelled wire): activity, in periodic bursts** — on for ~3
  blinks, off for ~6, repeating.

The burst pattern is what makes it conclusive. We clock SC *continuously* in MODE 0, so
crosstalk from our own clock would show as *continuous* activity. Bursts on the console's own
schedule are the BIOS multiboot-wait loop. That is a real GBA driving a real line.

**So the breakout labels are the wrong way round: the wire marked SI actually carries the
GBA's SO.** This explains every earlier symptom — the `0x00000000` word readout and nothing
ever latching in `linkcheck` tests 2 and 3. We were listening on the GBA's input.

**Go to STEP 2B.** STEP 1 below is kept for reference / re-testing after any rewire.

---

## ✅ HANDSHAKE CONFIRMED — next step is the real multiboot upload

`linkcheck` TEST 2 with the corrected polarity: **LED0 (ECHO), LED1 (SYNC) and LED2 all
latch.** Echo means the low-16 came back `0x6202`; sync means the high-16 came back `0x7202`.
That is the full multiboot recognition handshake, on real hardware. The transport works.

### Now run the real thing

Flash **`build/gba_link.uf2`** (the applet, not a diagnostic). It runs multiboot on core 1
automatically and retries forever, so just power-cycle the GBA with the Computer running.

Applet LED meanings (`main.cpp`) — note these are NOT the diagnostic conventions:

| LED | Meaning |
|---|---|
| **LED0** | **BOOTED** — multiboot completed, the payload is running on the GBA |
| **LED1** | connecting, or an error occurred |
| LED2–LED5 | GBA buttons **A, B, L, R** — press them on the console |

**The payoff:** once LED0 is lit, pressing A/B/L/R on the GBA lights LEDs 2–5. That is the
full round trip — code uploaded to the console, and the console reporting back.

The payload is only **624 bytes**, so the whole upload takes about **5 s even at 1 kHz**.

### Rate ladder (added 2026-09-06)

The applet used to hardcode 100 kHz for multiboot, which **has never been shown to work** —
the handshake was proven at ~1 kHz, with the Main knob fully CCW. It now walks a ladder,
fastest first: **100k → 50k → 16k → 5k → 1k**, trying multiboot at each until one succeeds,
then sticking with the winner (`gGba.linkHz` publishes it). Expect the first connection to
take a few seconds while it steps down.

### What can still go wrong, and what it means

The handshake is only the first of six stages. These have **never run on hardware**:
header transfer (0xC0 bytes), palette/handshake, length/seed, the encrypted main transfer
with its per-word offset check, and the final CRC exchange.

| Symptom | Likely stage | Meaning |
|---|---|---|
| LED0 never lights, LED1 steady | sync | never got `0x7202` — recheck wiring/polarity |
| LED1 flickers, cycles, retries | later stages | a stage broke partway; `gGba.lastError` holds the `MultibootResult` |
| LED0 lights, then drops back | post-boot poll | multiboot succeeded but the payload's serial slave is not answering with the `0x600D` tag |
| LED0 solid, buttons work | — | **done** |

If it cycles, the most informative thing is *which* rate it settles on and how far it gets —
`MultibootResult` distinguishes `BadHandshake`, `TransferError` and `CrcMismatch`, and those
point at very different causes.

---

## The breakthrough that got us here (2026-09-06, night): the data was inverted

`linkcheck` TEST 3 with the corrected wiring, word readout:

```
LED4 bright (high half):  0001 1011 1111 1011  ->  8 D F D
LED4 dim    (low half):   1001 1011 1111 1011  ->  9 D F D
                                    word = 0x8DFD9DFD
```

Invert every bit:

| Read back | Complement | Meaning |
|---|---|---|
| `0x8DFD` | **`0x7202`** | multiboot **RECOGNITION** |
| `0x9DFD` | **`0x6202`** | **ECHO** of exactly what we sent |

`0x8DFD9DFD` is the exact bit-complement of `0x72026202` — the textbook multiboot sync reply,
both halves. Noise does not produce the precise complement of the expected word.

**The GBA has been answering correctly all along. We were reading it inverted.**

### The fix

`GBA_MISO_INOVER` is now `GPIO_OVERRIDE_INVERT` in `../gba_spi.h`. Everything is rebuilt.

### Why the earlier "measurements" said NORMAL — both were flawed

1. **The scope check (2026-09-03) proved nothing.** It drove Pulse Out 2 into Pulse In 1
   through a patch cable and probed **both jacks**. They tracked because *a patch cable is a
   wire*. The probe never saw the pad, so it never observed the input stage at all. This was
   presented as decisive at the time; it was not.
2. **The loopback only ever proved MOSI-inversion XOR MISO-inversion == 0** — its documented
   blind spot from the very beginning. It could never distinguish a matched pair of errors,
   which is exactly what we had.

The console's own reply outranks both. It is the only test that exercises the real input
stage at the real drive level: our ~6 V output driving the transistor input is a different
operating point from the GBA driving it at 3.3 V, so the loopback and the live link can
genuinely disagree here.

### Retest now

1. Flash the rebuilt **`linkcheck.uf2`**.
2. Knobs: **X fully CCW** (variant 0, GBATEK canonical), **Y fully CCW** (normal SCK
   polarity), **Main fully CCW** (~1 kHz).
3. Click to **TEST 2** (LED4 on, LED5 off).
4. Power-cycle the **GBA** with the Computer already running; settle on the logo.

**Expect LED0 (echo) and LED1 (sync) to light.** If they do, the transport is finished and the
next step is the real multiboot upload with the rebuilt `gba_link.uf2`.

If TEST 2 is still dark, run TEST 3 and read the word again — if it now reads `0x72026202`
directly, the polarity is right and only the sync-loop logic needs attention.

### Expect the loopback test to FAIL now

`linkcheck` TEST 1 and `bringup` stage 2 will likely stop passing, because the MOSI/MISO
inversions no longer cancel for a jack-to-jack patch cable. **That is expected and is not a
regression** — the loopback was self-consistent and wrong. Judge the system by the console,
not by the loopback, from here on.

---

## STEP 1 — `cablecheck.uf2`, MODE 0. Which wire carries the GBA's data?

**Do this first.** It is the only step that resolves a hardware unknown, and every later step
depends on the answer. It drives no data, so it is safe whichever way the cable is wired.

### Rewire (one wire moves, temporarily)

Normally SO and SI go to different *kinds* of pin — SO to an input (we listen), SI to an
output (we drive). For this test **both go to inputs**. Nothing drives the GBA's data lines,
so it is safe even if the labels on your breakout are swapped — and that is exactly what it
is here to find out.

By the labels on the breakout:

| Your wire | Goes to | Why |
|---|---|---|
| **SC** | Pulse Out 1 (keep the 1 kΩ) | the clock — we drive it |
| **GND** | Computer GND | return path |
| **SO** | **Pulse In 1** | candidate A for "data from the GBA" |
| **SI** | **Pulse In 2** | candidate B — temporarily an input too |
| **SD** | leave disconnected | unused in normal/SIO32 mode |
| **VCC** | leave disconnected, tape it off | the GBA *sources* this; it is an output |
| — | **Pulse Out 2: nothing** | we drive no data in this mode |

The only change from the working build: **the SI wire moves off Pulse Out 2 onto Pulse In 2.**

The same thing by socket pin number, if you ever need to re-derive it: pin 5 = SC, pin 6 =
GND, pin 2 = SO, pin 3 = SI, pin 4 = SD, pin 1 = VCC.

Flash `cablecheck.uf2`. **LED4 + LED5 always show the mode in binary** — MODE 0 is both off.

### WHEN to take the reading

This matters, and an earlier build got it wrong. The activity LEDs now describe a **rolling
2-second window**, not all of history — the first version latched forever, so plugging and
power-on transients stayed on screen and the display showed activity on both inputs even with
the GBA switched **off**.

The clean sequence:

1. Computer on, `cablecheck` **already running in MODE 0** — SC must already be clocking when
   the console boots. That is the documented multiboot requirement, not a nicety.
2. Turn the **GBA** on, cartridge-less. Let it settle on the Nintendo logo.
3. **Wait ~5 seconds** so the boot transients age out of the window.
4. Now read LED0/LED1.

The GBA drives its line during boot **before** it reaches the multiboot wait state, so a
reading taken while the logo is still animating is not trustworthy. Let it settle. To retry,
power-cycle the **GBA** only and wait again.

### Read the result

LED0 = Pulse In 1 (your **SO** wire) has shown activity.
LED1 = Pulse In 2 (your **SI** wire) has shown activity.

| What you see | Meaning | Go to |
|---|---|---|
| **LED0 on, LED1 off** | Your **SO** wire carries the GBA's output. **Labels correct, cable STRAIGHT.** | **STEP 2A** |
| **LED1 on, LED0 off** | Your **SI** wire carries it. **Labels swapped, cable CROSSED.** | **STEP 2B** |
| **Both off** | GBA is not driving at all. | **STEP 3** |
| **Both on** | Suspect a short between the two data wires. | MODE 1, then **STEP 3** |

---

## STEP 2A — Cable is STRAIGHT

Your breakout labels are right. Put the SI wire back where it was:

| Your wire | Goes to |
|---|---|
| **SO** | Pulse In 1 |
| **SI** | Pulse Out 2, via 1 kΩ |
| **SC** | Pulse Out 1, via 1 kΩ |
| **GND** | Computer GND |
| SD, VCC | disconnected |

Go to **STEP 4**.

## STEP 2B — Cable is CROSSED: the swap fix

The cable crosses SO/SI, so your breakout labels are the wrong way round. **Wire by
behaviour, not by the label.** SC and GND do not move.

| Your wire | Was | **Now** | Because |
|---|---|---|---|
| labelled **SI** | Pulse Out 2 | **Pulse In 1** | it really carries the GBA's SO — we listen |
| labelled **SO** | Pulse In 1 | **Pulse Out 2**, via 1 kΩ | it really reaches the GBA's SI — we drive |
| **SC** | Pulse Out 1 | unchanged, via 1 kΩ | |
| **GND** | Computer GND | unchanged | |

**Move the 1 kΩ resistor too.** It was on the SI-labelled wire while that went to Pulse
Out 2. After the swap that wire goes to an *input*, so it must have **no series resistor** —
series resistance fights the pull-up that biases the transistor input stage. The resistor
belongs on the SO-labelled wire instead, which is now the driven one.

Final state: 1 kΩ on **SC** and on the **SO-labelled** wire (the two we drive). Nothing in
series with the **SI-labelled** wire (the one we listen to).

**Relabel the breakout now** so you never have to remember this again.

**No firmware change is needed** — the swap happens in copper, and the pin roles in
`gba_spi.h` stay exactly as they are. Move the 1 kΩ resistor with the wire it belongs to, so
the series resistors always sit on the two lines we DRIVE and never on the line we listen to.

Then go to **STEP 4**.

---

## STEP 3 — Nothing is driving. Rule these out in order

1. **Power-on order.** Computer first, *then* the GBA. This is real and documented: the
   console only syncs if the master is already clocking when it powers up. **Leave the
   Computer running and power-cycle the GBA only.**
2. **Cartridge-less, on the logo screen.** Not a game, not the menu.
3. **`cablecheck` MODE 2** (LED4 on, LED5 off) — the GBATEK slave-ready watch. LED0/LED1
   latch if either input has *ever* gone low. If one flickers low as the GBA boots, that line
   is the SO line and the console is alive — go back to STEP 2 with that knowledge.
4. **`cablecheck` MODE 3** (both on) — drives SC as a slow ~2 Hz square. Confirms the clock
   wire actually reaches the console; check it at the socket with a meter.
5. **`cablecheck` MODE 1** with the **GBA disconnected** — all four LEDs should be OFF. Any
   that light indicate a short in your own harness.
6. **Ground.** Re-buzz pin 6. Everything else is meaningless without it.

---

## STEP 4 — `linkcheck.uf2` TEST 2, on the GBATEK-correct timing

Flash the new `linkcheck.uf2`. It now carries **five sampling variants** instead of two, with
per-variant clock arithmetic (they have different cycles-per-bit, which the old single
constant got wrong).

**Set the knobs before you look:**

| Knob | Position | Selects |
|---|---|---|
| **X** | **fully CCW** | variant 0 — `m3-edge`, GBATEK canonical |
| **Y** | **fully CCW** | normal SCK polarity (the measured value) |
| **Main** | **fully CCW** | ~1 kHz, slowest and kindest to the transistor input |

Click to TEST 2 (LED4 on, LED5 off) and power-cycle the GBA while watching.

| LED | Meaning |
|---|---|
| **LED0** | **ECHO** — low-16 came back `0x6202`. The key light. |
| LED1 | **SYNC** — high-16 is `0x7202`. Full recognition. |
| LED2 | Reply looks like real data (≥3 bit transitions) |
| **LED3 flashing fast** | **Our own transport is stalling** — not the GBA's fault |

Solid = live now. Pulsing = latched earlier.

**If LED0 lights, that is the breakthrough** — bit timing is correct and the link works.

---

## STEP 5 — `linkcheck` TEST 3, the full sweep

If TEST 2 gives nothing, click to TEST 3 (LED4 on, LED5 on). It ignores the knobs and walks
**5 variants × 2 polarities × 5 rates = 50 slots**, ~0.7 s each.

**A full pass is ~35 seconds — let it run a full minute.**

LED0/LED1 latch and pulse if echo or sync is ever seen; LED3 lights once any best-slot has
been recorded.

Then **hold UP ~1 s** for the word readout. In TEST 3 the first nibble is the **winning slot
number** (mod 16), followed by the winning 32-bit word. Write down all 8 nibbles.

Decode the slot: `rate = slot % 5`, `polarity = (slot / 5) & 1`, `variant = (slot / 10) % 5`,
where rates are `{1k, 5k, 16k, 50k, 100k}` and variants are
`{m3-edge, m3-hold, m3-late, m3-wide, cpha0}`.

---

## STEP 6 — Read the word out and interpret it

The word readout is the tool that actually diagnoses. Hold UP ~1 s from any test; LED0–3 are
the nibble in binary, LED4 bright for the first four nibbles and dim for the last four, LED5
blinks between them. Click DOWN to leave.

| Word | Meaning |
|---|---|
| `0x00000000` | Line stuck low. Short to GND, or listening on the GBA's input (crossed cable). |
| `0xFFFFFFFF` | Line stuck high. Nothing driving it — we are reading our own pull-up. |
| `0xFFFF0000` or similar | One clean transition, no data. **Not** a reply — this is what fooled us before. |
| `0x????6202` | **ECHO — the link works.** Bit timing correct. |
| `0x7202????` | **Full sync.** |
| A rotation of `0x6202` | Off-by-N clock — the sample point is adjacent to correct; try the neighbouring variant. |
| Bit-reverse or inverse of `0x6202` | Polarity error — try the other Y-knob position. |
| Different every read | Noise, floating input, or a rate too fast for the transistor input. |

---

## Reference: LED conventions (both tools)

**LED4 + LED5 are always the mode/test number in binary** (LED4 = bit 1, LED5 = bit 0).
No test borrows them for data. If you are unsure where you are, read those two.

| LED4 | LED5 | `linkcheck` | `cablecheck` |
|---|---|---|---|
| off | off | 0 idle/wiring | 0 **listen (start here)** |
| off | on | 1 loopback | 1 short/crosstalk |
| on | off | 2 live GBA | 2 slave-ready watch |
| on | on | 3 sweep | 3 SC sanity |

Switch **clicks** — DOWN for next, UP for previous. Nothing needs holding except the ~1 s
UP-hold for the word readout.

---

## Still-open questions (honest list)

* **The sweep has never produced a positive result**, so no variant is yet proven on
  hardware. GBATEK says variant 0; that is a strong prior, not a measurement.
* **`cablecheck` and the five PIO variants have never been run on hardware.** They compile
  clean with `-Wall -Wextra`; that is all that is known.
* **Pulse In 2's pad inversion is assumed** to match Pulse In 1 (non-inverting, as measured).
  If MODE 0 gives a confusing result, that assumption is the first thing to doubt — invert
  the reading of LED1/LED3 mentally and see if it makes more sense.
* **Our sync loop sends `0x6202` and waits for `0x7202`**, following the two reference
  uploaders. GBATEK's own table shows a `6200` → `610y` → `720x` sequence instead. The
  references agree with each other so the code was left alone, but if the handshake starts
  responding and then stalls, this is the next thing to revisit.
