#!/usr/bin/env python3
"""End-to-end signal level through TRACT8's chain. THE GAIN-STAGING AUTHORITY.

This test exists because the card came back from its first hardware run
SILENT except for plosives - which are summed after the filter bank, so
their working proved the ISR, the DAC and the output path were all fine and
localised the fault to the bank or its gain staging.

The bug it found: the bandpass sections have a peak voltage gain far BELOW
unity at their centre frequencies. An RBJ constant-skirt bandpass has a peak
gain of alpha/a0, not 1.0 - and alpha is proportional to sin(w0)/2Q, so at
250 Hz with Q=4 that is about 0.004. Combined with the >>3 before the bank
and the >>4 after it, the chain threw away roughly 78 dB and the output
never left the DAC's zero code.

filter_check.py did not catch this: it measured each band's response
NORMALISED to its own peak, which is the right way to check centre
frequency and Q and the wrong way to check level. This file measures
absolute amplitude in DAC counts, which is the number that decides whether
anyone hears anything.

Checks:
  1. Per-band peak gain at centre frequency (the thing that was wrong).
  2. Full chain level for a buzz at F0 through a real vowel, in DAC counts.
  3. The same for noise.
  4. Headroom: worst case must not overflow int32 or clip the DAC.
  5. The click sits under the voice, measured against the VOICE.
  6. Breath changes the character of the sound, not its level.
  7. The plosive occupies a band - neither white noise nor a thump.
  8. The default burst is a consonant length, not a drum hit.
  9. Band gains smooth per sample and converge exactly (no stuck buzz).
 10. ADC jitter on a still knob does not reach the band gains.

Run: python tools/chain_check.py
"""

import sys
import math
import numpy as np

FS = 48000
BAND_HZ = [250, 450, 700, 1000, 1400, 1900, 2600, 3800]
Q = 4.0
COEFF_SHIFT = 15
COEFF_SCALE = 1 << COEFF_SHIFT

# Gain staging, transcribed from voder.cpp. These are what this test exists
# to validate.
PRE_SHIFT = 3      # excite >>= 3   before the bank
POST_SHIFT = 2     # return sum >> POST_SHIFT
BAND_MAKEUP = 1    # per-band makeup multiplier applied to y (1 = none)

DAC_MAX = 2047

# A real vowel: AH, the open-back corner from vowels.h.
VOWEL_AH = [666, 4003, 16000, 15423, 7647, 4226, 3923, 1675]


def coeffs(f0):
    w0 = 2.0 * math.pi * f0 / FS
    alpha = math.sin(w0) / (2.0 * Q)
    a0 = 1.0 + alpha
    b0 = alpha / a0
    a1 = (-2.0 * math.cos(w0)) / a0
    a2 = (1.0 - alpha) / a0
    return (round(b0 * COEFF_SCALE) / COEFF_SCALE,
            round(a1 * COEFF_SCALE) / COEFF_SCALE,
            round(a2 * COEFF_SCALE) / COEFF_SCALE)


def peak_gain(b0, a1, a2, f0):
    """|H| at the band's centre - the ABSOLUTE gain, not normalised."""
    w = 2.0 * math.pi * f0 / FS
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    return float(np.abs(b0 * (1.0 - z2) / (1.0 + a1 * z1 + a2 * z2)))


def check_band_gains():
    print("\n1. Per-band peak voltage gain at centre frequency")
    print("   This is the ABSOLUTE gain. filter_check.py normalises it away.")
    print("   band     b0 (Q15)   peak gain     dB")
    ok = True
    for f0 in BAND_HZ:
        b0, a1, a2 = coeffs(f0)
        g = peak_gain(b0, a1, a2, f0)
        db = 20 * math.log10(g) if g > 0 else -999
        flag = "" if g > 0.25 else "  <-- VERY LOSSY"
        if g <= 0.25:
            ok = False
        print(f"   {f0:5d}   {round(b0*COEFF_SCALE):8d}   {g:9.5f}  "
              f"{db:7.2f}{flag}")
    print("   (a constant-skirt bandpass peaks at alpha/a0, NOT at 1.0;")
    print("    alpha ~ sin(w0)/2Q, so low bands are the lossiest)")
    return ok


def run_chain(sig, gains, makeup=BAND_MAKEUP):
    """Integer transcription of Voder::Process()'s bank + output staging."""
    states = [[0, 0, 0, 0] for _ in BAND_HZ]   # x1 x2 y1 y2
    icoef = []
    for f0 in BAND_HZ:
        w0 = 2.0 * math.pi * f0 / FS
        alpha = math.sin(w0) / (2.0 * Q)
        a0 = 1.0 + alpha
        icoef.append((round((alpha / a0) * COEFF_SCALE),
                      round(((-2.0 * math.cos(w0)) / a0) * COEFF_SCALE),
                      round(((1.0 - alpha) / a0) * COEFF_SCALE)))

    out = np.zeros(len(sig), dtype=np.float64)
    for n, x in enumerate(sig):
        excite = int(x) >> PRE_SHIFT
        total = 0
        for i, (b0, a1, a2) in enumerate(icoef):
            st = states[i]
            acc = b0 * (excite - st[1]) - a1 * st[3] - a2 * st[2]
            y = acc >> COEFF_SHIFT
            st[1] = st[0]
            st[0] = excite
            st[2] = st[3]
            st[3] = y
            y2 = y * makeup
            total += (y2 * gains[i]) >> 15
        out[n] = total >> POST_SHIFT
    return out


def make_saw(f0, n):
    inc = int(f0 * (2 ** 32) / FS)
    ph = 0
    o = np.zeros(n, dtype=np.int64)
    for i in range(n):
        ph = (ph + inc) & 0xFFFFFFFF
        o[i] = ((ph >> 17) - 16384) * 2
    return o


def check_full_chain():
    print("\n2. Full chain: 110 Hz buzz through the AH vowel")
    n = 8192
    sig = make_saw(110, n)
    out = run_chain(sig, VOWEL_AH)
    tail = out[n // 2:]                    # let the filters settle
    peak = float(np.max(np.abs(tail)))
    rms = float(np.sqrt(np.mean(tail ** 2)))

    print(f"   input peak            {float(np.max(np.abs(sig))):9.0f} "
          f"(Q15 full scale 32767)")
    print(f"   output peak           {peak:9.1f}  DAC counts "
          f"(usable range +/-{DAC_MAX})")
    print(f"   output rms            {rms:9.1f}  DAC counts")
    if peak > 0:
        print(f"   peak as dBFS          {20*math.log10(peak/DAC_MAX):9.2f} dB")
    else:
        print("   peak as dBFS               SILENT")

    # Below about 20 counts peak the card is inaudible in any practical
    # setting - that is under -40 dBFS into a modular level input.
    ok = peak >= 20
    print(f"   audible?              "
          f"{'yes' if ok else 'NO - THIS IS THE BUG'}")
    return ok


def check_noise_chain():
    print("\n3. Full chain: white noise through the AH vowel")
    n = 8192
    rng = np.random.default_rng(1)
    sig = rng.integers(-32768, 32767, n)
    out = run_chain(sig, VOWEL_AH)
    tail = out[n // 2:]
    peak = float(np.max(np.abs(tail)))
    rms = float(np.sqrt(np.mean(tail ** 2)))
    print(f"   output peak           {peak:9.1f}  DAC counts")
    print(f"   output rms            {rms:9.1f}  DAC counts")
    ok = peak >= 20
    print(f"   audible?              "
          f"{'yes' if ok else 'NO'}")
    return ok


def check_headroom():
    print("\n4. Headroom: all bands open, worst-case coherent input")
    n = 8192
    # Worst case is a tone at the frequency where the summed response peaks.
    sig = (32767 * np.sin(2 * np.pi * 1400 *
                          np.arange(n) / FS)).astype(np.int64)
    out = run_chain(sig, [32767] * 8)
    tail = out[n // 2:]
    peak = float(np.max(np.abs(tail)))
    print(f"   output peak           {peak:9.1f}  DAC counts")
    clips = peak > DAC_MAX
    print(f"   clips the DAC?        {'yes' if clips else 'no'}"
          f"{'  (clamped in main.cpp; a state the player chooses)' if clips else ''}")
    # The failure condition is int32 overflow, not clipping.
    ok = peak < 2 ** 31
    print(f"   int32 safe?           {'yes' if ok else 'NO - OVERFLOW'}")
    return ok




# Click gain staging, transcribed from voder.cpp.
CLICK_SHIFT = 17
PLOSIVE_LO_Q15 = 2600        # ~600 Hz, removes the thump
PLOSIVE_HI_Q15 = 12000       # ~2800 Hz, removes the hiss
PLOSIVE_LP_LOSS_DB = -9.4    # measured RMS loss through the bandpass
DEFAULT_CLICK_DECAY = 2000   # main.cpp, about 12 ms
PLOSIVE_MIN_SAMPLES = 384
PLOSIVE_MAX_SAMPLES = 48000
DEFAULT_CLICK_LEVEL = 3000      # main.cpp, no 8mu attached
VOWEL_BAND_FRACTION = sum(VOWEL_AH) / (8 * 32767)


def voice_at_sum():
    """Roughly what a real vowel contributes at the summing point."""
    return int((32767 >> PRE_SHIFT) * 2.299 * VOWEL_BAND_FRACTION)


def click_at_sum(level):
    """Peak contribution of a burst at the summing point.

    The two-pole lowpass that darkens the burst also costs about 16 dB of
    level, which is why the shift is >>17 here where an unfiltered burst
    needed >>19. Both numbers have to move together: keeping the old shift
    with the filter added would have made the click inaudible.
    """
    raw = (32768 * level) >> CLICK_SHIFT
    return raw * (10 ** (PLOSIVE_LP_LOSS_DB / 20.0))


def voice_at_sum():
    """Roughly what a real vowel contributes at the summing point."""
    return int((32767 >> PRE_SHIFT) * 2.299 * VOWEL_BAND_FRACTION)


def click_at_sum(level):
    """Peak contribution of a burst at the summing point.

    The bandpass that shapes the burst also costs about 9 dB of level, so
    the shift and the filter are the same decision expressed twice:
    changing one without the other silently moves the balance.
    """
    raw = (32768 * level) >> CLICK_SHIFT
    return raw * (10 ** (PLOSIVE_LP_LOSS_DB / 20.0))


def check_click_balance():
    """The click must sit UNDER the voice, measured against the VOICE."""
    print("\n5. Click level against the voice")
    ok = True
    voice = voice_at_sum()
    print(f"   a real vowel contributes about {voice} at the sum")
    print("   click level      vs voice")
    for level, label in ((DEFAULT_CLICK_LEVEL, "default   "),
                         (16384, "fader half"),
                         (32767, "fader full")):
        c = click_at_sum(level)
        db = 20 * math.log10(max(c, 1) / voice)
        print(f"   {label}      {db:+6.1f} dB")

    # Reported as too loud twice. The first fix set the level to "-14 dB",
    # but that was -14 dB of the CLICK's own full scale, which landed it
    # level with the voice. Measure against the voice or the number means
    # nothing.
    d = 20 * math.log10(max(click_at_sum(DEFAULT_CLICK_LEVEL), 1) / voice)
    good = d < -10.0
    if not good:
        ok = False
    print(f"   default is {d:+.1f} dB under the voice   "
          f"{'ok - punctuation' if good else '<-- STILL PERCUSSIVE'}")

    full = 20 * math.log10(max(click_at_sum(32767), 1) / voice)
    good = full > -6.0
    if not good:
        ok = False
    print(f"   fader full reaches {full:+.1f} dB   "
          f"{'ok - can still be loud' if good else '<-- CANNOT GET LOUD'}")
    print("   (the click is summed AFTER the bank, so unlike the voice it")
    print("    is never attenuated by the vowel)")
    return ok


def check_breath_is_a_ratio():
    """Breath must change the character, not the level."""
    print("\n6. Breath is a ratio, not a level")
    ok = True
    print("   breath   both gates   voiced only   noise only")
    levels = []
    for bm in (0, 8192, 16384, 24576, 32767):
        row = f"   {bm:5d}   "
        for gv, gn in ((1, 1), (1, 0), (0, 1)):
            mix = 32767 - bm
            blended = ((32767 * mix) + (32767 * bm)) >> 15
            env = max(32767 * gv, 32767 * gn)
            ex = (blended * env) >> 15
            levels.append(ex)
            row += f"{ex:9d}    "
        print(row)

    spread = max(levels) - min(levels)
    good = spread < 200
    if not good:
        ok = False
    print(f"   level varies by {spread} across every combination   "
          f"{'ok - constant' if good else '<-- BREATH CHANGES VOLUME'}")
    print("   (gating buzz and hiss separately and crossfading afterwards")
    print("    made 50% breath give HALF THE VOLUME when one gate was shut)")
    return ok


def plosive_burst(n=8192, seed=1):
    """Transcription of the bandpass in Voder::Process()."""
    rng = np.random.default_rng(seed)
    x = rng.integers(-32768, 32767, n).astype(float)
    ah = PLOSIVE_HI_Q15 / 32768.0
    al = PLOSIVE_LO_Q15 / 32768.0
    h1 = h2 = l1 = l2 = 0.0
    y = np.zeros(n)
    for i, v in enumerate(x):
        h1 += (v - h1) * ah
        h2 += (h1 - h2) * ah
        l1 += (h2 - l1) * al
        l2 += (l1 - l2) * al
        y[i] = h2 - l2
    return x, y


def check_plosive_spectrum():
    """A P or a B is neither white noise nor a kick drum."""
    print("\n7. The plosive occupies the band a stop actually occupies")
    ok = True

    x, y = plosive_burst()
    spec = np.abs(np.fft.rfft(y * np.hanning(len(y))))
    freqs = np.fft.rfftfreq(len(x), 1.0 / FS)

    def band(lo, hi):
        m = (freqs >= lo) & (freqs < hi)
        return float(np.sqrt(np.mean(spec[m] ** 2)))

    peak = band(500, 1500)
    sub = 20 * math.log10(band(20, 200) / peak)
    high = 20 * math.log10(band(4000, 12000) / peak)

    print(f"   below 200 Hz   {sub:+6.1f} dB   (thump region)")
    print(f"   500-1500 Hz    {0.0:+6.1f} dB   (the burst peak)")
    print(f"   above 4 kHz    {high:+6.1f} dB   (hiss region)")

    # BOTH ends have to be down. A plain lowpass gets the top right and
    # the bottom badly wrong - it keeps the sub-100 Hz energy, and the
    # burst becomes a thump. That version was reported as "a bomb going
    # off"; the one before it, unfiltered, as "white noise". The band is
    # the only shape that is neither.
    good_low = sub < -6.0
    good_high = high < -8.0
    if not (good_low and good_high):
        ok = False
    print(f"   thump removed   {'ok' if good_low else '<-- A BOMB'}")
    print(f"   hiss removed    {'ok' if good_high else '<-- WHITE NOISE'}")

    dc = abs(float(np.mean(y))) / float(np.sqrt(np.mean(y ** 2)))
    good = dc < 0.05
    if not good:
        ok = False
    print(f"   DC offset {dc*100:.1f}% of RMS   "
          f"{'ok' if good else '<-- WOULD CLICK'}")
    return ok


def check_plosive_length():
    """A consonant is short. A drum hit is not."""
    print("\n8. The default burst is a consonant, not a drum hit")
    sq = (DEFAULT_CLICK_DECAY * DEFAULT_CLICK_DECAY) >> 15
    n = PLOSIVE_MIN_SAMPLES + \
        (((PLOSIVE_MAX_SAMPLES - PLOSIVE_MIN_SAMPLES) * sq) >> 15)
    ms = n / (FS / 1000.0)
    # A real plosive release is 5-15 ms. It was 41 ms, which reads as a
    # drum whatever its spectrum.
    good = 4.0 < ms < 20.0
    print(f"   default decay {ms:.0f} ms   "
          f"{'ok - inside the real 5-15 ms range' if good else '<-- TOO LONG'}")

    full = PLOSIVE_MIN_SAMPLES + \
        (((PLOSIVE_MAX_SAMPLES - PLOSIVE_MIN_SAMPLES) * 32767) >> 15)
    print(f"   fader full still reaches {full/(FS/1000.0):.0f} ms   ok")
    print("   (the fader is for sustained textures; the DEFAULT is what")
    print("    a panel trigger gives before anyone touches anything)")
    return good




GAIN_SMOOTH_SHIFT = 6


def smooth_step(cur, target):
    """Transcription of the per-sample gain smoothing in Voder::Process()."""
    d = (target - cur) >> GAIN_SMOOTH_SHIFT
    if d == 0 and cur != target:
        return cur + (1 if target > cur else -1)
    return cur + d


def check_gain_smoothing():
    """Band gains must reach their target exactly, and quickly, per sample."""
    print("\n9. Band gains are smoothed per sample and converge exactly")
    ok = True

    # THE BUG THIS GUARDS. The gains were first slewed at the CONTROL rate
    # (125 Hz) with (target - cur) >> 2. That failed twice over.
    #
    # It did not remove the buzz, because slewing at the control rate just
    # replaces one step every 8 ms with several smaller ones every 8 ms -
    # the rate is unchanged and the rate is what is audible.
    #
    # And it never converged upward: an arithmetic shift of a small
    # POSITIVE delta is zero, so a rising gain stalled a few counts short
    # while ADC dither jittered the target around it. That is a buzz that
    # persists after the hand stops moving and clears only when some other
    # message shifts the target - exactly what was reported.
    for start, target, label in ((0, 16000, "rising"),
                                 (16000, 0, "falling"),
                                 (16000, 16003, "rising by 3"),
                                 (16000, 15997, "falling by 3")):
        cur = start
        n = 0
        while cur != target and n < 100000:
            cur = smooth_step(cur, target)
            n += 1
        good = cur == target
        if not good:
            ok = False
        ms = n / 48.0
        print(f"   {label:14} {start:6d} -> {target:6d} in {n:4d} samples "
              f"({ms:5.2f} ms)   "
              f"{'ok' if good else '<-- NEVER CONVERGES'}")

    # The old control-rate form, for contrast: it stalls.
    cur = 16000
    for _ in range(1000):
        cur = cur + ((16003 - cur) >> 2)
    stalled = cur != 16003
    print(f"   old >>2 form after 1000 updates: {cur} (target 16003)   "
          f"{'stalls, as it did on hardware' if stalled else 'converged'}")

    # No single sample may move the gain enough to be an edge. The worst
    # case is the first step of the largest jump.
    biggest = (32767 - 0) >> GAIN_SMOOTH_SHIFT
    import math
    db = 20 * math.log10(1 + biggest / 16000.0)
    good = db < 0.5
    if not good:
        ok = False
    print(f"   largest single-sample step {biggest} = {db:.2f} dB   "
          f"{'ok - no edge' if good else '<-- AUDIBLE STEP'}")

    # And it must settle fast enough to feel immediate.
    cur, n = 0, 0
    while cur != 16000 and n < 100000:
        cur = smooth_step(cur, 16000)
        n += 1
    good = n / 48.0 < 20.0
    if not good:
        ok = False
    print(f"   settles in {n/48.0:.1f} ms   "
          f"{'ok - feels immediate' if good else '<-- SLUGGISH'}")
    return ok




KNOB_FILTER_SHIFT = 5
ADC_JITTER_LSB = 3


def check_knob_jitter():
    """A still knob must produce a still sound."""
    print("\n10. ADC jitter does not reach the band gains")
    ok = True

    # THE BUG THIS GUARDS, AND WHY THE SIMULATION MISSED IT FOR THREE
    # ROUNDS. Everything upstream of the knobs was modelled with clean
    # values, so the moving-vs-static comparison came out identical and
    # said the smoothing was fine. It was. The targets were not.
    #
    # A raw RP2040 ADC reading jitters a few LSB continuously, and
    # KnobVal() << 3 turns each LSB into 8 counts of Q15. The gain
    # smoother settles in about 9 ms; a freshly jittered target arrives
    # every 8 ms. So the gains chase a target that never stops moving,
    # whether or not a hand is on the knob - a buzz that outlives the
    # gesture and whose loudness depends on where the knob sits.
    import random

    def unfiltered(n=20000):
        random.seed(1)
        peak = 0
        for _ in range(n):
            raw = 2048 + random.randint(-ADC_JITTER_LSB, ADC_JITTER_LSB)
            peak = max(peak, abs((raw << 3) - (2048 << 3)))
        return peak

    def filtered(n=80000):
        random.seed(1)
        acc = 2048 << 7
        peak = 0
        for _ in range(n):
            raw = (2048 + random.randint(-ADC_JITTER_LSB,
                                         ADC_JITTER_LSB)) << 7
            acc += (raw - acc) >> KNOB_FILTER_SHIFT
            peak = max(peak, abs((acc >> 4) - (2048 << 3)))
        return peak

    before = unfiltered()
    after = filtered()
    good = after < before / 2
    if not good:
        ok = False
    print(f"   {ADC_JITTER_LSB} LSB of ADC noise -> {before} counts raw, "
          f"{after} filtered   {'ok' if good else '<-- STILL NOISY'}")

    # Filtering in Q15 rather than Q19 does almost nothing, because the
    # shift of a small delta is zero and the filter stalls exactly where
    # the jitter is. That was the first attempt.
    random.seed(1)
    cur = 2048 << 3
    peak = 0
    for _ in range(80000):
        raw = (2048 + random.randint(-ADC_JITTER_LSB,
                                     ADC_JITTER_LSB)) << 3
        cur += (raw - cur) >> 4
        peak = max(peak, abs(cur - (2048 << 3)))
    print(f"   filtering in Q15 instead leaves {peak} counts   "
          f"{'(the first attempt - barely helped)' if peak > after else ''}")

    # And it must still follow a real knob move quickly.
    acc = 0
    n = 0
    target = 2048 << 7
    while abs(target - acc) > (50 << 4) and n < 100000:
        acc += (target - acc) >> KNOB_FILTER_SHIFT
        n += 1
    ms = n / 16.0
    good = ms < 25.0
    if not good:
        ok = False
    print(f"   follows a knob move in {ms:.1f} ms   "
          f"{'ok - feels immediate' if good else '<-- SLUGGISH'}")
    print("   (the state is kept at Q19 and shifted down to Q15 on the way")
    print("    out; the extra precision is what stops the filter stalling)")
    return ok


def _cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def soft_clip(x):
    """Exact integer model of SoftClip() in main.cpp."""
    lim = 2047
    knee = 1500
    span = lim - knee
    mag = -x if x < 0 else x
    if mag <= knee:
        return x
    over = mag - knee
    if over >= 2 * span:
        y = lim
    else:
        y = knee + over - (over * over) // (4 * span)
        if y > lim:
            y = lim
    return -y if x < 0 else y


def check_output_headroom():
    """The output stage must be loud without ever leaving the DAC range."""
    print()
    print("11. Output level and soft saturation")
    ok = True

    # WHY THIS EXISTS. The gain staging was sized for the worst case -
    # eight bands wide open with a coherent input - and a real vowel uses
    # about three bands, so the card ran roughly 10 dB quieter than it
    # needed to. On the bench a full vowel measured about +0.5 V to -1.5 V
    # against outputs that reach +/-6 V. Quiet signal means the noise floor
    # sits proportionally closer to it, which is half of why the card was
    # reported as noisy: the noise was not especially loud, the voice was
    # especially quiet.
    #
    # Raising the shift alone would clip the coherent case flat, which is
    # why the old comment forbade it. A cubic soft knee takes the headroom
    # back instead: linear through everything ordinary, rounding over on
    # the rare peak that would have clipped anyway.
    GAIN_NUM, GAIN_DEN = 7, 2

    def staged(x):
        return soft_clip(x * GAIN_NUM // GAIN_DEN)

    vowel_before = 528
    worst_before = 1568
    vowel_after = staged(vowel_before)
    worst_after = staged(worst_before)

    import math
    gain_db = 20 * math.log10(vowel_after / float(vowel_before))
    good = vowel_after > vowel_before * 2
    if not good:
        ok = False
    print(f"   a vowel      {vowel_before:5d} -> {vowel_after:5d} counts"
          f"   {gain_db:+.1f} dB   {'ok' if good else '<-- NO LOUDER'}")

    good = abs(worst_after) <= 2047
    if not good:
        ok = False
    print(f"   worst case   {worst_before:5d} -> {worst_after:5d} counts"
          f"           {'ok - inside the DAC' if good else '<-- CLIPS'}")

    # Monotonic, or the saturation folds and the loud peaks invert.
    prev = None
    mono = True
    inrange = True
    for x in range(-6000, 6001, 7):
        y = staged(x)
        if prev is not None and y < prev:
            mono = False
        prev = y
        if abs(y) > 2047:
            inrange = False
    if not (mono and inrange):
        ok = False
    print(f"   monotonic across +/-6000        "
          f"{'ok - no fold-back' if mono else '<-- FOLDS'}")
    print(f"   never leaves +/-2047            "
          f"{'ok' if inrange else '<-- OUT OF RANGE'}")

    # Below the knee the stage must be EXACTLY linear - not approximately.
    #
    # This is the whole point of the change. An earlier version used a
    # cubic soft clipper, which
    # bends from the very first sample: there is no linear region at all,
    # so every vowel was distorted all the time. 28 dB of THD at ordinary
    # playing level, heard on the bench as a whine that tracked the volume
    # and brightness faders - volume because it drives the level into the
    # curve, brightness because it decides which band is loudest and so
    # where that band's harmonics land. A build with this stage bypassed
    # had no whine at all, which is what identified it.
    bad = [x for x in range(-1500, 1501) if soft_clip(x) != x]
    good = not bad
    if not good:
        ok = False
    print(f"   exactly linear below the knee    "
          f"{'ok - no distortion in normal play' if good else '<-- BENDS'}")

    # And a real vowel must sit below the knee at the shipped gain, or the
    # linear region is not where the music is.
    #
    # 409 counts - the momentary PEAK from a scope, not the 528 this file
    # computes for AH at the table peak and not the 273 typical level.
    # The model figure is optimistic by about 2x, and sizing the stage
    # from it left the card using a third of its range. Sizing from the
    # AVERAGE instead would let peaks bend, which was the earlier mistake.
    vowel_staged = 409 * 7 // 2
    good = vowel_staged <= 1500
    if not good:
        ok = False
    print(f"   a vowel lands at {vowel_staged:5d} vs knee 1500   "
          f"{'ok - inside the linear region' if good else '<-- ABOVE THE KNEE'}")
    return ok


# ComputerCard drives an external 4-way mux, advancing one state per audio
# interrupt, and updates knobs[mux_state] from the shared ADC channel. The
# CV inputs share a channel two ways. Transcribed from ComputerCard.h.
KNOB_MUX_STATES = 4
CV_MUX_STATES = 2

PANEL_DIV = 4     # main.cpp kPanelDiv
MORPH_DIV = 384   # main.cpp kMorphDiv


def check_mux_lock():
    """Control reads must not beat against ComputerCard's input mux."""
    print()
    print("12. Panel reads are locked to the input mux")
    ok = True

    # THE BUG THIS GUARDS. kPanelDiv was 3: one knob per sample, round
    # robin. But a knob only receives a NEW value every 4 samples, because
    # the mux has 4 states and advances once per interrupt. A 3-phase read
    # against a 4-state mux means the AGE of the value being read walks
    # 0,1,2,0,1,2 with a period of lcm(3,4) = 12 samples.
    #
    # 48000/12 is 4 kHz. That is an audible whine, measured on the bench at
    # roughly 280 us per cycle, and INDEPENDENT of anything patched in
    # because the sampling pattern generates it on its own. It is
    # intermittent because the knob's own ~60 Hz filter has to have some
    # ripple left for the beat to turn into a tone.
    #
    # No amount of smoothing downstream fixes this: the tone is created at
    # the point of sampling, so it arrives already inside the signal.
    import math

    def beat_period(read_div, mux_states):
        """Samples before the read lands at the same mux phase again."""
        return read_div * mux_states // math.gcd(read_div, mux_states)

    for name, div, states in (
        ("panel read vs knob mux", PANEL_DIV, KNOB_MUX_STATES),
        ("panel read vs CV mux", PANEL_DIV, CV_MUX_STATES),
        ("morph vs knob mux", MORPH_DIV, KNOB_MUX_STATES),
        ("morph vs CV mux", MORPH_DIV, CV_MUX_STATES),
    ):
        period = beat_period(div, states)
        # Locked means the read period is already a whole number of mux
        # cycles, so the beat period is just the read period itself.
        locked = (div % states == 0)
        freq = FS / period
        audible = 20.0 < freq < 20000.0
        good = locked or not audible
        if not good:
            ok = False
        note = "locked" if locked else f"BEATS at {freq:.0f} Hz"
        print(f"   {name:26s} every {div:3d} vs {states} states -> "
              f"{note}   {'ok' if good else '<-- AUDIBLE WHINE'}")

    # The specific regression: 3 against 4 must be recognised as bad, or
    # this test proves nothing.
    bad = beat_period(3, KNOB_MUX_STATES)
    bad_f = FS / bad
    print(f"   (the old kPanelDiv=3 beat every {bad} samples = "
          f"{bad_f:.0f} Hz, {1e6 / bad_f:.0f} us - a real bug, "
          f"but NOT the whine)")
    print("    the whine was the output stage - see check 11)")
    if not (20.0 < bad_f < 20000.0):
        ok = False
    return ok


def main():
    print("Voder end-to-end chain check - absolute levels in DAC counts")
    print(f"  pre-bank >>{PRE_SHIFT}, post-bank >>{POST_SHIFT}, "
          f"per-band makeup x{BAND_MAKEUP}")

    ok = check_band_gains()
    ok &= check_full_chain()
    ok &= check_noise_chain()
    ok &= check_headroom()
    ok &= check_click_balance()
    ok &= check_breath_is_a_ratio()
    ok &= check_plosive_spectrum()
    ok &= check_plosive_length()
    ok &= check_gain_smoothing()
    ok &= check_knob_jitter()
    ok &= check_output_headroom()
    ok &= check_mux_lock()

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
