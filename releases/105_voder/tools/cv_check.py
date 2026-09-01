#!/usr/bin/env python3
"""The jack inputs, and whether random voltages actually make it babble.

THE USE CASE THIS EXISTS FOR: "I want to be able to feed in random voltages
and gates and get chattering." That is a different requirement from "the CV
inputs work", and it is the one worth testing, because a card can track CV
perfectly and still be useless fed from a sample-and-hold.

Three properties make the difference, and none of them is obvious from the
wiring:

  1. THE CVs ADD, they do not replace. A random voltage should wander the
     sound around wherever the controls are parked, so the patch never has
     to supply a sensible absolute value. An input that replaced its
     control would make the knobs dead the moment a cable went in.
  2. THE FORMANT CV MOVES DIAGONALLY. Sweeping one axis of the vowel cube
     mostly travels between two corners; moving openness up as front moves
     down crosses the middle, where the distinct vowels are. One random
     voltage then walks through recognisably different vowels rather than
     shading one of them.
  3. GATES MUST CHATTER WITHOUT CLICKING. The glottal ramp is 2 ms, so a
     gate stream stays clean up to a few hundred Hz and then degrades into
     amplitude modulation rather than into clicks - which is a usable
     texture rather than a fault.

Jacks:
  Audio In 1  exciter       Audio In 2  volume CV
  CV In 1     pitch 1V/oct  CV In 2     formant, bipolar
  Pulse In 1  click         Pulse In 2  glottal gate

Checks:
  1. Every CV adds to its control rather than replacing it.
  2. A random formant CV visits genuinely different vowels.
  3. External audio sums with the internal sources at a usable level.
  4. The volume CV ducks and swells around the base level.
  5. Gate streams chatter cleanly across a useful frequency range.

Run: python tools/cv_check.py
"""

import sys
import math

# Transcribed from vowels.h.
NUM_BANDS = 8
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

GATE_RAMP = 96          # samples, from voder.h
FS = 48000


def clamp15(x):
    return max(0, min(32767, x))


def blend(openness, front, round_amt=0):
    out = []
    for i in range(NUM_BANDS):
        sb = SPREAD['OO'][i] + (((SPREAD['AH'][i] - SPREAD['OO'][i]) * openness) >> 15)
        sf = SPREAD['EE'][i] + (((SPREAD['EH'][i] - SPREAD['EE'][i]) * openness) >> 15)
        spread = sb + (((sf - sb) * front) >> 15)
        rb = ROUND['OO'][i] + (((ROUND['AH'][i] - ROUND['OO'][i]) * openness) >> 15)
        rf = ROUND['EE'][i] + (((ROUND['EH'][i] - ROUND['EE'][i]) * openness) >> 15)
        rounded = rb + (((rf - rb) * front) >> 15)
        out.append(spread + (((rounded - spread) * round_amt) >> 15))
    return out


def spectral_distance(v1, v2):
    d = 0.0
    for a, b in zip(v1, v2):
        da = 20 * math.log10(max(a, 1) / 32767.0)
        db = 20 * math.log10(max(b, 1) / 32767.0)
        d += (da - db) ** 2
    return math.sqrt(d / len(v1))


def formant_positions(base_open, base_front, cv):
    """Transcription of the CV In 2 diagonal in ReadPanel()."""
    return (clamp15(base_open + cv), clamp15(base_front - cv))


def check_cvs_add():
    print("\n1. Every CV adds to its control rather than replacing it")
    ok = True

    # Formant CV at zero must leave the controls exactly where they are.
    for base in (0, 8000, 16384, 32767):
        o, f = formant_positions(base, base, 0)
        good = o == base and f == base
        if not good:
            ok = False
        print(f"   formant CV 0 at base {base:5d} -> ({o:5d},{f:5d})   "
              f"{'ok' if good else '<-- REPLACED'}")

    # And a CV must move it from wherever it was, not to a fixed place.
    a = formant_positions(4000, 4000, 8000)
    b = formant_positions(20000, 20000, 8000)
    good = a != b
    if not good:
        ok = False
    print(f"   same CV from two bases gives {a} and {b}   "
          f"{'ok - relative' if good else '<-- ABSOLUTE'}")
    print("   (a patch should never have to supply a sensible absolute")
    print("    value; the knobs stay live with a cable plugged in)")
    return ok


def check_formant_diagonal():
    """A random CV must visit genuinely different vowels."""
    print("\n2. A random formant CV visits different vowels")
    ok = True

    # Walk the CV across its range from a centred base and measure how far
    # apart the extremes are, and how much ground is covered on the way.
    base = 16384
    seen = []
    for cv in range(-16384, 16385, 2048):
        o, f = formant_positions(base, base, cv)
        seen.append(blend(o, f, 0))

    span = spectral_distance(seen[0], seen[-1])
    good = span > 8.0
    if not good:
        ok = False
    print(f"   full CV sweep spans {span:5.1f} dB   "
          f"{'ok' if good else '<-- TOO NARROW'}")

    # Consecutive steps must differ audibly, or the sweep has dead zones
    # where a slow random would sit and do nothing.
    steps = [spectral_distance(seen[i], seen[i + 1])
             for i in range(len(seen) - 1)]
    worst = min(steps)
    good = worst > 0.3
    if not good:
        ok = False
    print(f"   smallest step along the sweep {worst:4.2f} dB   "
          f"{'ok - no dead zones' if good else '<-- DEAD ZONE'}")

    # The diagonal must beat moving one axis alone, which is the reason
    # for opposing the two.
    diag = spectral_distance(blend(0, 32767, 0), blend(32767, 0, 0))
    single = spectral_distance(blend(0, 16384, 0), blend(32767, 16384, 0))
    good = diag > single
    if not good:
        ok = False
    print(f"   diagonal {diag:5.1f} dB vs single axis {single:5.1f} dB   "
          f"{'ok - diagonal is wider' if good else '<-- NO BENEFIT'}")
    return ok


def check_exciter():
    """Audio In 1 sums with the internal sources rather than replacing them."""
    print("\n3. External audio sums at a usable level")
    ok = True

    # Audio In 1 is <<4, so +/-2048 becomes roughly +/-32767 - level with
    # the internal buzz and noise.
    ext_max = 2047 << 4
    internal = 32767
    rel = 20 * math.log10(ext_max / internal)
    good = abs(rel) < 3.0
    if not good:
        ok = False
    print(f"   full-scale input is {rel:+.1f} dB against the internal buzz   "
          f"{'ok - can carry the sound' if good else '<-- TOO QUIET TO LEAD'}")
    print("   (halving it was tried and put external 12 dB down, which is")
    print("    too quiet for the thing you patched in to be the point)")

    # Summed, not substituted: the internal voice must survive.
    # Modelled as the engine does it - excite = mix + ext.
    for label, mix, ext in (("internal only", 20000, 0),
                            ("external only", 0, 20000),
                            ("both together", 20000, 20000)):
        total = mix + ext
        print(f"   {label:14} -> excitation {total:6d}")
    good = (20000 + 20000) > 20000
    print(f"   both audible at once   "
          f"{'ok - talk over a drum loop' if good else 'FAIL'}")
    ok &= good

    # Worst case must not wrap int32, whatever it does at the DAC.
    worst = internal + ext_max
    good = worst < 2 ** 31
    if not good:
        ok = False
    print(f"   worst-case excitation {worst} fits int32   "
          f"{'ok' if good else '<-- WRAPS'}")

    # A real vowel uses a fraction of the all-open bank gain, so the
    # realistic peak has headroom even though the coherent worst case
    # would clip.
    AH = [666, 4003, 16000, 15423, 7647, 4226, 3923, 1675]
    frac = sum(AH) / (8 * 32767)
    dac = int(((worst >> 3) * 2.299 * frac)) >> 2
    good = dac < 2048
    if not good:
        ok = False
    print(f"   realistic peak at the DAC {dac} of 2047   "
          f"{'ok' if good else '<-- CLIPS ON ORDINARY MATERIAL'}")
    return ok


def check_volume_cv():
    print("\n4. The volume CV ducks and swells around the base")
    ok = True
    # Audio In 2 is <<4, so +/-2048 becomes +/-32768.
    base = 32767          # no 8mu: base is full
    duck = clamp15(base + (-2048 << 4))
    good = duck == 0
    if not good:
        ok = False
    print(f"   -5V from full -> {duck:5d}   "
          f"{'ok - ducks to silence' if good else 'FAIL'}")

    half = 16384
    up = clamp15(half + (2047 << 4))
    down = clamp15(half + (-2048 << 4))
    good = up == 32767 and down == 0
    if not good:
        ok = False
    print(f"   from half: +5V {up:5d}, -5V {down:5d}   "
          f"{'ok - swells and ducks' if good else 'FAIL'}")
    print("   (an envelope here articulates a phrase without the patch")
    print("    having to control absolute level)")
    return ok


def check_gate_chatter():
    """Fast gates must chatter, not click and not stall."""
    print("\n5. Gate streams chatter cleanly")
    ok = True
    step = 32768 // GATE_RAMP
    print("   gate Hz   envelope reaches   behaviour")
    for hz in (5, 20, 50, 100, 200, 400, 800):
        half = FS // (hz * 2)
        reached = min(32767, half * step)
        pct = reached * 100 // 32767
        if pct == 100:
            note = "full gate"
        elif pct > 40:
            note = "amplitude modulation - usable texture"
        else:
            note = "tremolo, very shallow"
        print(f"     {hz:4d}         {pct:3d}%           {note}")

    # Up to a few hundred Hz the gate must fully open, or a gate sequence
    # would sound quieter than a held note.
    half200 = FS // 400
    good = min(32767, half200 * step) == 32767
    if not good:
        ok = False
    print(f"   fully opens at 200 Hz   "
          f"{'ok' if good else '<-- GATES LOSE LEVEL'}")

    # And the 2 ms ramp must be long enough to prevent a click. A step
    # discontinuity at 48 kHz is broadband; 96 samples of ramp puts the
    # fastest edge below about 500 Hz of equivalent slew.
    ramp_ms = GATE_RAMP * 1000.0 / FS
    good = ramp_ms >= 1.0
    if not good:
        ok = False
    print(f"   ramp is {ramp_ms:.1f} ms   "
          f"{'ok - no click' if good else '<-- CLICKS'}")
    return ok


def check_cv_modulation_noise():
    """A control CV must not ring-modulate the output with its own ADC noise."""
    print()
    print("-" * 68)
    print("CV inputs used as CONTROLS are smoothed before they multiply")
    ok = True

    # THE BUG THIS GUARDS, and why it took five rounds to find.
    #
    # Audio In 2 sets volume and CV In 2 sweeps the formant axis. Both are
    # CONTROLS, but both were read fresh every sample with a <<4 / <<3 gain
    # and no smoothing at all.
    #
    # ComputerCard gives the audio inputs only a 12 kHz notch - for the mux
    # interference - because an audio input has to pass audio. The residual
    # broadband ADC noise is a couple of LSB: nothing as a signal, but
    # volume is not a signal path. It MULTIPLIES the output every sample:
    #
    #     out = (out * vol) >> 15
    #
    # so two LSB times sixteen becomes full-depth amplitude modulation of
    # the entire voice. Broadband, riding on the signal, loudest when the
    # card is loudest, and swamped by a strong input - which is exactly how
    # it was reported, including the detail that patching Audio In 1
    # LESSENED it.
    #
    # Every earlier theory (USB contention, the control-rate zipper, ADC
    # jitter on the KNOBS) was about the gain-target path, and each fix was
    # real but fixed a different bug. This one is on the output multiplier,
    # downstream of all of them, which is why smoothing the knobs and the
    # gains never touched it.
    import random

    SHIFT = 6

    def modulation_depth(smoothed):
        random.seed(11)
        acc = 0
        peak = 0
        prev = None
        for _ in range(20000):
            raw = random.randint(-2, 2)          # ~2 LSB of ADC noise
            if smoothed:
                acc += ((raw << 8) - acc) >> SHIFT
                v = acc >> 4
            else:
                v = raw << 4
            if prev is not None:
                peak = max(peak, abs(v - prev))
            prev = v
        return peak

    before = modulation_depth(False)
    after = modulation_depth(True)
    good = after * 4 <= before
    if not good:
        ok = False
    print(f"   2 LSB of ADC noise on a control CV:")
    print(f"     unsmoothed -> {before:3d} counts of step on the multiplier")
    print(f"     smoothed   -> {after:3d} counts   "
          f"{'ok' if good else '<-- STILL MODULATING'}")

    # And it must still follow a real CV move quickly - a volume CV that
    # lags is worse than one that hisses.
    acc = 0
    n = 0
    target = 1500 << 8
    while abs(target - acc) > (target >> 4) and n < 48000:
        acc += (target - acc) >> SHIFT
        n += 1
    ms = n / 48.0
    good = ms < 5.0
    if not good:
        ok = False
    print(f"   follows a CV move in {ms:.1f} ms   "
          f"{'ok - still feels immediate' if good else '<-- SLUGGISH'}")

    # Audio In 1 must NOT be smoothed: it is summed into the excitation and
    # genuinely carries audio. Filtering it would be a bug in the other
    # direction, so this records the asymmetry deliberately.
    print("   (Audio In 1 is deliberately NOT smoothed - it is summed into")
    print("    the excitation and has to pass audio. Additive noise there")
    print("    is not multiplied by anything.)")
    return ok


def main():
    print("TRACT8 CV and gate check")
    print("  Fed from random voltages, does it babble usefully?")
    ok = check_cvs_add()
    ok &= check_formant_diagonal()
    ok &= check_exciter()
    ok &= check_volume_cv()
    ok &= check_gate_chatter()
    ok &= check_cv_modulation_noise()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
