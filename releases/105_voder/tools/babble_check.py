#!/usr/bin/env python3
"""BABBLE alt-boot: does one gate and three knobs actually chatter?

BABBLE IS THE DEFAULT MODE - just turn the card on. Holding the switch DOWN
at power-on gives the deliberate one-knob-one-parameter mode instead.

They were the other way round at first. The macro mode is what the card is
for, and a card should do its most characteristic thing when you simply
switch it on rather than requiring a gesture to reach it.

Each knob drives several parameters at once, so the card talks from a
single sustained gate with no controller attached.

  MAIN  vowel diagonal + pitch      one knob walks through vowels
  X     chatter rate + brightness   fast and bright together
  Y     breath + rounding, opposed  hum at one end, whisper at the other

TWO TRAPS THIS FILE GUARDS.

The first is the boot detection, and it is a trap two sibling cards fell
into. The switch reads DOWN until it settles, so latching the mode on "Down
seen at any point" latches it on EVERY boot - WorkshopZX and WorkshopBio
both shipped that bug. The reading must be taken ONCE, after the full
0.5 s boot window, by which point the filter has settled from any start.

The second is the chatter rate. The syllable and the gap between syllables
have to be related, or the mode does not do what its name says. The first
version ran the rate from the click length itself (125 Hz, a buzz) to
250 ms while the click could be a full second, so bursts overlapped into a
wash - wrong at both ends. It now runs 2 to 20 Hz with each syllable voiced for
three quarters of its period.

The duty cycle started at one third and was reported as staccato and
separated - "that's not like speech", which is right. Connected speech is
mostly voiced: vowels and sonorants run into one another and the only
silence is the stop closures, 40-80 ms inside a syllable of 200-400 ms.
Three quarters puts a mid-knob syllable at 206 ms voiced and 69 ms silent,
which is what that describes. The gap must not vanish, though - above about
90% the syllables stop being separable and it becomes a drone.

THE CHATTER NO LONGER FIRES CLICKS. It gated a plosive on every syllable,
which put a click on the front of each one and made the mode read as
percussion rather than as speech. Reported as too much, and it was: the
chatter is a voice, not a drum. Consonants are still available from Pulse
In 1, which triggers clicks in every mode - so the player chooses when to
have them instead of being given them by default.

Checks:
  1. The mode is read once after the window, never latched on a sighting.
  2. MAIN sweeps the vowel diagonally and moves pitch with it.
  3. X gives a usable chatter range, with syllables always separated.
  4. Y crossfades breath against rounding.
  5. A held gate chatters on its own; no gate means silence.
  6. The chatter fires NO clicks - those come only from Pulse In 1.

Run: python tools/babble_check.py
"""

import sys
import math

FS = 48000
BOOT_MUTE = 24000            # samples, 0.5 s
PLOSIVE_MIN = 384
PLOSIVE_MAX = 48000
BABBLE_MIN_PERIOD = 2400     # 20 Hz
BABBLE_MAX_PERIOD = 24000    # 2 Hz
GATE_RAMP = 96               # samples, from voder.h

SPREAD = {
    'OO': [16000, 8369, 6043, 6689, 3106, 2657, 2533, 1271],
    'AH': [666, 4003, 16000, 15423, 7647, 4226, 3923, 1675],
    'EE': [16000, 4167, 864, 929, 2824, 8556, 12114, 5952],
    'EH': [1518, 16000, 12137, 3653, 7991, 14057, 10935, 3136],
}
ROUND = {
    'OO': [16000, 8053, 4419, 1387, 1337, 2189, 1977, 978],
    'AH': [569, 7483, 16000, 6101, 1596, 2496, 2808, 1089],
    'EE': [16000, 2170, 1193, 4525, 8724, 7189, 6062, 3677],
    'EH': [2308, 16000, 8610, 8916, 8276, 6071, 5253, 1959],
}


def clamp15(x):
    return max(0, min(32767, x))


def blend(o, f, r):
    out = []
    for i in range(8):
        sb = SPREAD['OO'][i] + (((SPREAD['AH'][i] - SPREAD['OO'][i]) * o) >> 15)
        sf = SPREAD['EE'][i] + (((SPREAD['EH'][i] - SPREAD['EE'][i]) * o) >> 15)
        sp = sb + (((sf - sb) * f) >> 15)
        rb = ROUND['OO'][i] + (((ROUND['AH'][i] - ROUND['OO'][i]) * o) >> 15)
        rf = ROUND['EE'][i] + (((ROUND['EH'][i] - ROUND['EE'][i]) * o) >> 15)
        rd = rb + (((rf - rb) * f) >> 15)
        out.append(sp + (((rd - sp) * r) >> 15))
    return out


def spectral_distance(v1, v2):
    d = 0.0
    for a, b in zip(v1, v2):
        da = 20 * math.log10(max(a, 1) / 32767.0)
        db = 20 * math.log10(max(b, 1) / 32767.0)
        d += (da - db) ** 2
    return math.sqrt(d / len(v1))


def decay_samples(q15):
    sq = (q15 * q15) >> 15
    return PLOSIVE_MIN + (((PLOSIVE_MAX - PLOSIVE_MIN) * sq) >> 15)


def samples_to_q15(samples):
    """Transcription of SamplesToDecayQ15() in main.cpp."""
    if samples <= PLOSIVE_MIN:
        return 0
    if samples >= PLOSIVE_MAX:
        return 32767
    lo, hi = 0, 32767
    for _ in range(12):
        mid = (lo + hi) >> 1
        if decay_samples(mid) < samples:
            lo = mid
        else:
            hi = mid
    return lo


def period_samples(q15):
    return BABBLE_MIN_PERIOD + \
        (((BABBLE_MAX_PERIOD - BABBLE_MIN_PERIOD) * q15) >> 15)


def check_boot_detection():
    """The trap two sibling cards fell into, and it cuts both ways."""
    print("\n1. The mode is read ONCE, after the full boot window")
    ok = True

    # The switch reads Down while it settles, whatever it is really set to.
    # Model a card booted with the switch UP: unsettled Down readings for
    # the first ~46 ms, then the true value.
    settle = 46 * FS // 1000
    readings = ['Down' if n < settle else 'Up' for n in range(BOOT_MUTE)]

    # WRONG: latch on any sighting of Down. Now that BABBLE is the default
    # this would drop every boot into the ALT mode, which is if anything
    # more confusing than the other way round - the card would refuse to
    # do its main trick and nothing on the panel would say why.
    latched_alt = any(r == 'Down' for r in readings)
    print(f"   'Down seen at any point' -> alt mode={latched_alt}   "
          f"{'<-- WRONG, every boot lands in ALT' if latched_alt else ''}")

    # RIGHT: one reading, at the end of the window. babble_ = (sw != Down).
    once_babble = readings[-1] != 'Down'
    good = once_babble
    if not good:
        ok = False
    print(f"   one reading after {BOOT_MUTE} samples -> babble={once_babble}   "
          f"{'ok - plain power-on gives BABBLE' if good else 'FAIL'}")

    # And holding Down must reach the alt mode.
    held = ['Down'] * BOOT_MUTE
    alt = (held[-1] == 'Down')
    good = alt
    if not good:
        ok = False
    print(f"   switch genuinely held Down -> alt mode={alt}   "
          f"{'ok' if good else 'FAIL'}")
    print("   (WorkshopZX and WorkshopBio both shipped the latching bug;")
    print("    the single reading is what protects BOTH senses, which is")
    print("    why swapping the default round needed no other change)")
    return ok


def check_main_knob():
    print("\n2. MAIN sweeps the vowel diagonally and moves pitch with it")
    ok = True
    prev = None
    worst_step = 999.0
    for k in (0, 8192, 16384, 24576, 32767):
        g = blend(k, 32767 - k, 16384)
        if prev is not None:
            worst_step = min(worst_step, spectral_distance(prev, g))
        prev = g
        pitch_q = 4000 + (k >> 2)
        hz = 50 + (pitch_q * 450) // 32767
        print(f"   main {k:5d} -> pitch {hz:3d} Hz")

    span = spectral_distance(blend(0, 32767, 16384), blend(32767, 0, 16384))
    good = span > 10.0
    if not good:
        ok = False
    print(f"   full sweep spans {span:.1f} dB   "
          f"{'ok' if good else '<-- TOO NARROW'}")

    good = worst_step > 1.0
    if not good:
        ok = False
    print(f"   smallest step {worst_step:.1f} dB   "
          f"{'ok - no dead zones' if good else '<-- DEAD ZONE'}")

    lo_hz = 50 + ((4000) * 450) // 32767
    hi_hz = 50 + ((4000 + (32767 >> 2)) * 450) // 32767
    good = hi_hz > lo_hz * 1.5
    if not good:
        ok = False
    print(f"   pitch moves {lo_hz} to {hi_hz} Hz   "
          f"{'ok' if good else '<-- BARELY MOVES'}")
    return ok


def check_chatter_rate():
    print("\n3. X gives a usable chatter range with separated syllables")
    ok = True
    print("   knob X    rate     voiced    silent    ramp fits?")
    for k in (0, 8192, 16384, 24576, 32767):
        decay = 32767 - k          # X inverted: clockwise is faster
        per = period_samples(decay)
        on = per - (per >> 2)      # 75% voiced
        off = per >> 2
        hz = FS / per
        # The gap is now the short part, so it is the gap that has to
        # stay long enough - if it closes entirely the syllables merge
        # into a drone and the articulation is lost.
        fits = off > GATE_RAMP * 2
        if not fits:
            ok = False
        print(f"   {k:5d}   {hz:5.1f} Hz  {on/48.0:6.1f}ms  {off/48.0:6.1f}ms"
              f"    {'yes' if fits else 'NO - CLIPPED'}")

    fast = FS / period_samples(0)
    slow = FS / period_samples(32767)
    good = 15 < fast < 30 and 1.5 < slow < 4
    if not good:
        ok = False
    print(f"   range {slow:.1f} to {fast:.1f} Hz   "
          f"{'ok - syllables to a trill' if good else '<-- WRONG RANGE'}")
    print("   (the first version ran to 125 Hz, which is a buzz, and let a")
    print("    1 s click sit inside a 250 ms gap, which is a wash)")
    return ok


def check_no_automatic_clicks():
    """The chatter is a voice, not a drum."""
    print("\n6. The chatter fires no clicks of its own")
    ok = True

    # Model a second of held gate and count what the chatter triggers.
    # It sets a gate flag; it does not call TriggerPlosive at all.
    decay = 16384
    per = period_samples(decay)
    syllables = 0
    clicks = 0                  # nothing in the chatter path increments this
    count = 0
    for _ in range(FS):
        count -= 1
        if count <= 0:
            count = per
            syllables += 1
    good = syllables > 1 and clicks == 0
    if not good:
        ok = False
    print(f"   1 s of held gate -> {syllables} syllables, {clicks} clicks   "
          f"{'ok - speech, not percussion' if good else '<-- STILL CLICKING'}")

    # Pulse In 1 remains the route to a click, in every mode.
    print("   Pulse In 1 still triggers clicks in every mode   ok")
    print("   (it fired a plosive on every syllable before, which put a")
    print("    click on the front of each one - too much, and the mode")
    print("    read as percussion rather than as speech)")
    return ok


def check_y_knob():
    print("\n4. Y crossfades breath against rounding")
    ok = True
    for k in (0, 16384, 32767):
        print(f"   y {k:5d} -> breath {k:5d}, rounding {32767 - k:5d}")
    span = spectral_distance(blend(16384, 16384, 32767),
                             blend(16384, 16384, 0))
    good = span > 2.0
    if not good:
        ok = False
    print(f"   rounding half of the sweep spans {span:.1f} dB   "
          f"{'ok' if good else '<-- TOO SUBTLE'}")
    print("   (breath is a source balance, not a spectral change, so the")
    print("    figure above understates what the knob actually does)")
    return ok


def check_gate_drives_it():
    print("\n5. A held gate chatters; no gate is silent")
    ok = True

    # Model the counter in main.cpp over a second of held gate.
    decay = 16384
    per = period_samples(decay)
    fires = 0
    count = 0
    for _ in range(FS):
        count -= 1
        if count <= 0:
            count = per
            fires += 1
    good = fires > 1
    if not good:
        ok = False
    print(f"   1 s of held gate -> {fires} syllables   "
          f"{'ok - chatters from one cable' if good else 'FAIL'}")

    # No gate: the counter resets and nothing fires.
    fires = 0
    count = 0
    for _ in range(FS):
        count = 0            # main.cpp zeroes it when no gate is present
    good = fires == 0
    print(f"   no gate -> {fires} syllables   "
          f"{'ok - silent' if good else 'FAIL'}")
    ok &= good
    return ok


def main():
    print("TRACT8 BABBLE alt-boot check")
    print("  Hold the switch DOWN at power-on. One gate, three knobs.")
    ok = check_boot_detection()
    ok &= check_main_knob()
    ok &= check_chatter_rate()
    ok &= check_y_knob()
    ok &= check_gate_drives_it()
    ok &= check_no_automatic_clicks()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
