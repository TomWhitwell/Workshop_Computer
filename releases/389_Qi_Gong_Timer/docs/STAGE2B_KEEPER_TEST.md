# Stage 2b: the silence keeper (after you finish the stage 2 test)

**Please finish the stage 2 test first** (`STAGE2_HARDWARE_TEST.md`) with the stage 2 firmware: check 8 there (hum between alerts)
tells us how things sound *without* the keeper.

## What the keeper does
No Workshop System filter or Ring Mod closes completely, so a faint tone leaks through between alerts. The keeper sits in front of
the effects, learns how loud that leak is, and lets sound through only when a real sound arrives: always at once when the card
fires its own pulse (Pulse 1 for In 1, Pulse 2 for In 2), and also for sounds you trigger by hand (the Slope's button).

## Two firmware files
| File | Between alerts | During a chime or bell |
|---|---|---|
| `qi_timer_2b_keeper_send.uf2` | the **reverb/delay stop sustaining** the leak; the leak itself still passes through dry | unchanged |
| `qi_timer_2b_keeper_dry.uf2` | **true silence** at the outputs | unchanged (bit-for-bit); the gate only acts once a tail has faded to about -50 dBFS |

## Try this
1. Flash `qi_timer_2b_keeper_send.uf2`. Run a short session (1:00 x 2) and turn the mixer up during the quiet between alerts. Compare with what you heard in stage 2.
2. Flash `qi_timer_2b_keeper_dry.uf2`. Same again. Is the quiet now silent? Do the chime's attack and its fading tail sound exactly as before?
3. Trigger a Slope by hand (its button) between alerts: does that sound still get its reverb?
4. Optional: play something quiet and steady into In 1 for a minute or two: it should never be cut off.

## Please tell me
* Which do you prefer: **send only** (the dry signal is never touched) or **send + dry** (true silence between alerts)?
* Anything cut off, softened, or "breathing" that shouldn't be?
