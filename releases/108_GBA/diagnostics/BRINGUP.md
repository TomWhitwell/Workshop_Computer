# Hardware bring-up notes (GBA)

> # WORKING (2026-09-06) — see [`POSTMORTEM.md`](POSTMORTEM.md)
> Kept as the accumulated background. Note that several conclusions recorded below as
> "measured" were later **disproved** — most importantly the MISO polarity, whose scope
> "measurement" had both probes on the same net. Treat this file as a history rather than
> a reference.

> **Start at `TESTPLAN.md`** — it is the current step-by-step bench procedure, with a
> decision tree for each outcome. This file is the accumulated background behind it.

Status: **ELECTRICAL LAYER CLEAR — ready for the live GBA handshake (stage 3).**
Readings A, B and C all passed on 2026-09-03; a working cable is built. The only untested
element left in the chain is the GBA itself. Pick up at "Reading D" below.

### Bench session 2026-09-03 — what was proven

| Check | Result |
|---|---|
| A — output swing (stage 1, scope) | 0 V low, **6 V high**, anti-phase, sharp edges. The old −2 V is gone. |
| B — MISO pad polarity (patch cable, 2 probes) | Both ends go high together => input stage does **NOT** invert => `NORMAL`. |
| C — loopback transport (stage 2, scope) | **32 clean clock pulses**, LED0 **solid** = bit-exact 32-bit word. |

Three bugs were found and fixed as a result (commit `08fb83f`):

1. **MISO polarity was wrong in the applet** (`INVERT`, should be `NORMAL`). Would have
   broken multiboot over any cable. All overrides now live as single constants in
   `../gba_spi.h` so the four init sites cannot disagree again.
2. **clkdiv divided by 3, but both PIO programs are 4 cycles/bit** — every rate ran 4/3 fast.
3. **The diagnostics called put/get_blocking from `ProcessSample`** (48 kHz, 20.8 µs budget)
   while a word takes ~854 µs. That stalled the audio callback for ~41 sample periods and
   left the SM half-fed — the exact cause of the "SC idles high, only 2–3 clock pulses"
   symptom. Now a non-blocking `post()`/`poll()` pair. The applet was never affected: it
   runs the link on core 1, where blocking is correct.

**Note on the SC idle level:** SC resting HIGH at 6 V between bursts is *correct*, not a
fault. The SM stalls on `side 0` (pad low) and Pulse Out 1 inverts in hardware. Don't chase
it again.

### THE CABLE IS CROSSED (settled 2026-09-06)

Measured with `cablecheck` MODE 0, which listens on Pulse In 1 and Pulse In 2 simultaneously.
With the GBA settled on the logo screen, only **Pulse In 2** showed activity, and it arrived
in **periodic bursts** — the BIOS multiboot-wait loop. (Crosstalk from our own SC clocking
would have been continuous, since MODE 0 clocks continuously; the bursts rule it out.)

So this link cable swaps SO/SI between its ends, and the breakout labels were reversed. Wire
by behaviour: the **SI-labelled** wire goes to **Pulse In 1** (it carries the GBA's SO) and
the **SO-labelled** wire goes to **Pulse Out 2** via 1 kΩ (it reaches the GBA's SI). SC and
GND do not move, and no firmware change is needed — the swap is in copper.

This retro-explains the whole 2026-09-03/04 session: the `0x00000000` word readout and the
dead LEDs in `linkcheck` tests 2 and 3 were us listening on the GBA's *input* pin.

### The cable that worked

A point-to-point GBA link cable into a bought GBA link socket, with **SI, SO, SC and GND**
continuous and clean. Notes from getting there:

- A **multi-play cable with a mid-cable breakout** was tried and rejected — it read as an
  SI–GND short. Multi-play wiring is not normal/SIO32 wiring; avoid breakout cables.
- **SD (pin 4) is never needed.** Normal/SIO32 mode ignores it. Needing only SO/SI/SC/GND is
  the signature of the right cable; an "SD is connected" reading suggests a multi-play cable.
- **Pin 1 (VCC 3.3 V) is an output the GBA sources.** Leave it isolated.
- Ground may ride the **shield/braid** rather than a discrete core — a three-cores-plus-shield
  cable is normal and fine. Buzz pin 6 to the braid and the shell, not just the conductors.
- Multiboot is **full duplex**: SO (pin 2) and SI (pin 3) are separate conductors, never
  multiplexed. Only multi-play and JOY-bus modes share a data line.

## Historical: the electrical fault that caused the pause (SUPERSEDED — now fixed)

Kept for context only. These readings were taken over the **old chopped cheap cable** and are
no longer the state of the bench; the readings in the table at the top of this file replace
them. A multimeter (reference = Computer GND) then read:

| Line | Measured | Should be | Verdict |
|------|----------|-----------|---------|
| SC (clock) | **−2 V** | 0–3.3 V | ❌ negative = invalid/harmful |
| SI (MOSI)  | **+6 V** | 0–3.3 V at the GBA | ⚠️ Workshop pulse out swings ~5–6V; needs series R |
| SO (MISO)  | **+6 V** | ≤3.3 V (GBA drives it) | ❌ 6V here = back-drive, not from GBA |

These are out of the GBA's 0–3.3 V spec (−2 V is actively bad). **Do not run the GBA connected
until these are sane** — risk of damaging the console's serial pins.

The build checklist that cleared it (items 1-3 are **done**; item 2 is still outstanding
hardware work before a GBA is connected):

1. **Common ground.** Measure DC from Computer-GND to GBA-GND (e.g. headphone-jack sleeve).
   It MUST read ~0 V. Anything else (esp. a volt or two) = grounds aren't common, and *every*
   other reading is garbage. This is the #1 suspect. A proper GND wire through the cable
   (pin 6) is far better than borrowing the headphone sleeve.
2. **Series resistors.** Put ~1 kΩ in series on **SC and SI** (Workshop pulse outputs swing
   ~5–6 V; the GBA inputs are 3.3 V). The GBA's clamp diode + the resistor limits current.
   Do NOT put a resistor on SO (GBA drives that at 3.3 V into us — fine as-is).
3. With power off, continuity-check the cable end you kept: GBA socket **pin 2→SO, pin 3→SI,
   pin 5→SC, pin 6→GND**. Beware the link-cable crossover (the two ends swap pins 2/3). Trust
   the GBA-socket pin numbers, not wire colour. **See `CABLES.md`** for the per-cable-type
   checklist — a GameCube→GBA cable (straight-through) avoids the crossover entirely and is
   the preferred one.
4. Re-measure SC/SI/SO with the probe running: all should sit within 0–3.3 V (SI/SC after
   their series resistors), none negative.

## Operational gotcha (documented, real)

**Power the Computer (master) BEFORE turning on the GBA.** The GBA only syncs if the master is
already clocking when it powers up. Power-cycle the *GBA* (not the Computer) to retry.

## Firmware state (all PROVEN, don't re-derive)

- **Loopback passes** — re-proven on the scope 2026-09-03 with `bringup.uf2` stage 2
  (switch MIDDLE, jumper Pulse Out 2 → Pulse In 1): 32 clean clock pulses per burst and LED0
  solid = a bit-exact 32-bit round trip. Prefer `bringup` stage 2 over the older
  `gba_loopback.uf2`. Loopback blind spot: it only checks MOSI-invert XOR MISO-invert, so it
  cannot catch either line's polarity alone — that is what Reading B was for.
- **Pull-up fix is in** `gba_multiboot.cpp`: after `pio_gpio_init` on the MISO pin, we
  re-`gpio_pull_up(GBA_MISO_PIN)` — the Pulse In 1 transistor input is dead without it.
- **Sample edge — THE EARLIER NOTE HERE WAS WRONG (corrected 2026-09-04).** This file used
  to say `gba_spi_cpha0` (leading-edge) was probably right and `gba_spi` probably wrong. An
  audit against GBATEK says the opposite. GBATEK, *SIO Normal Mode*: "During inactive
  transfer, the shift clock (SC) is high" and "When master sends SC=LOW, each master and
  slave must output the next outgoing data bit to SO. When master sends SC=HIGH, each master
  and slave must read out the opponents data bit from SI." That is **SPI mode 3**: idle high,
  drive on the falling edge, sample on the rising edge — which is what `gba_spi` already
  does, and what the applet has always used. `gba_spi_cpha0` samples during the DRIVE phase.
  The old claim rested on reasoning about the reference uploaders, never on a measurement.
  The scope has since confirmed the polarity half independently: with the SM stalled on
  `side 0` the SC jack idles HIGH at 6 V (Pulse Out 1 inverts), exactly GBATEK's idle state.
  `linkcheck` now carries five sampling variants and defaults to the GBATEK-canonical one.
- **Best sync test:** GBATEK — in normal mode the reply's LOW 16 bits echo the master's sent
  low-16, so sending 0x00006202 returns 0x????6202. That echo is a loopback *through the GBA*
  and confirms bit timing independent of recognition (upper 16 = 0x7202).

## SUPERSEDED — MISO polarity is **INVERT** (settled by the GBA, 2026-09-06)

The section below concluded NORMAL from a scope check. **That conclusion was wrong, and the
measurement behind it was invalid.** It drove Pulse Out 2 into Pulse In 1 through a patch
cable and probed *both jacks* — which track each other because a patch cable is a wire. The
probe never saw the pad, so it never observed the input stage.

The GBA settled it: with the wiring corrected, the reply read `0x8DFD9DFD`, the exact
bit-complement of `0x72026202` — `0x7202` recognition and `0x6202` echo. `GBA_MISO_INOVER` is
now `GPIO_OVERRIDE_INVERT`.

Note also that **the loopback passing never contradicted this**: it only ever proved
MOSI-inversion XOR MISO-inversion == 0, its blind spot from day one, so a matched pair of
errors sailed through. Expect the loopback to fail now; that is correct, not a regression.

<details><summary>Original (wrong) 2026-09-03 conclusion, kept for the record</summary>

## RESOLVED: MISO polarity is NORMAL (scope-measured 2026-09-03)

Was an open contradiction — the applet said `INVERT`, both diagnostics said `NORMAL`, and
loopback structurally cannot tell them apart (it only proves MOSI-invert XOR MISO-invert, so
a matched pair of errors passes).

**Settled by measurement, not derivation.** Patch cable Pulse Out 2 -> Pulse In 1, a scope
probe on each end: both go high together, so the Workshop input stage does **not** invert.

The old derivation was wrong because it conflated ComputerCard's `PulseIn1()` returning
`!gpio_get` — a *software* convention — with a *pad* inversion. It is not one. The applet's
`INVERT` was a real bug that would have broken multiboot over any cable, and it also explains
why the diagnostics were the only thing ever returning structured bits from a real GBA.

All four sites now read the single constants in `../gba_spi.h` (`GBA_SCK_OUTOVER`,
`GBA_MOSI_OUTOVER`, `GBA_MISO_INOVER`), so they cannot silently disagree again.

</details>

## Diagnostics in this folder

- `bringup.cpp`   → `bringup.uf2` — **start here.** The staged tool: switch UP = signal
  generator (scope the raw output swing, GBA disconnected), MIDDLE = loopback, DOWN = live
  GBA handshake. Its file header documents the LED meaning per stage. **Stages 1 and 2 are
  now hardware-proven** (2026-09-03); stage 3 has not yet been run against a real GBA.
- `loopback.cpp`  → `gba_loopback.uf2` — transport self-test (no GBA). Superseded by
  `bringup` stage 2.
- `gba_probe.cpp` → `gba_probe.uf2` — live GBA handshake probe; sweeps edge/polarity/clock,
  can report the raw reply as CV1/CV2 voltages. Rebuild: configure once, then
  `cmake --build build`.

Build them all (paths as per the root `CLAUDE.md`):

```sh
cd diagnostics
cmake -G Ninja -B build -S . && cmake --build build
```

## Next session: an oscilloscope is now available

The blocker above was diagnosed with a multimeter, which cannot see edge quality, ringing or
a clock that only misbehaves at speed. With a scope, `bringup` stage 1 becomes the real first
test: it emits a slow anti-phase square on SC/SI plus a ~10 kHz burst so both the DC levels
and the edges can be judged directly.

A **browser diagnostic UI** (USB-MIDI SysEx + a `web/index.html`, as in `../WorkshopNibbleDrum`
and `../WorkshopZX`) is the natural next tool once the electrical layer is sane — it would
report the raw 32-bit reply word per attempt instead of encoding it into six LEDs, which is
the current bottleneck when reading stage 3. Not started; no USB code exists in this repo yet.
