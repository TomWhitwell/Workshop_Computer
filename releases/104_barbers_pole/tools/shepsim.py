#!/usr/bin/env python3
"""
shepsim.py - render the SHEPARD engine to WAV so it can be HEARD before
flashing.

Every other test in tools/ verifies a number. None of them answers the
question the whole card lives or dies on:

    IS THE OCTAVE WRAP AUDIBLE?

The mathematics says no - the window is zero at both ends, so the layer
leaving and the layer entering are both silent at the seam. But "provably
zero at the boundary" and "sounds seamless to a person" are different
claims, and only one of them can be checked by listening.

So this runs the EXACT integer engine - the same MulQ15, the same
interpolated table lookups, the same >> 5 output shift - and writes audio.
What comes out of the speakers here is what will come out of the card,
sample for sample, minus the DAC.

Default output is the wrap-seam set, at the layer counts where a seam would
be most exposed. N=3 is the critical case: fewest layers means least
masking, so if the wrap is audible anywhere it is audible there.

    python tools/shepsim.py              # the wrap-seam set
    python tools/shepsim.py --all        # every mode
    python tools/shepsim.py --spectrogram  # PNGs too (needs matplotlib)

WAVs land in tools/ and are gitignored.
"""

import argparse
import math
import os
import struct
import sys
import wave

SR = 48000
MIN_LAYERS = 3
# Array sizing only. The CARD's cap is 11 - at 12 the top layer sits above
# Nyquist, carries window gain but makes no sound, and drops the audible
# centre a semitone. See kMaxLayers in shepard.h.
MAX_LAYERS = 12
CARD_MAX_LAYERS = 11
BASE_INC = 1229782          # 13.75 Hz (A-1) as a Q32 phase increment

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------------------
# The engine, mirrored exactly from the firmware.
#
# These are deliberately verbatim copies rather than imports of a shared
# module: the point of this file is to be an INDEPENDENT reimplementation
# that happens to agree, so a transcription error in one shows up as audible
# nonsense rather than being silently shared.
# ---------------------------------------------------------------------------

SIN_LUT = [int(round(math.sin(2.0 * math.pi * i / 1024) * 32767.0))
           for i in range(1024)]

POW2_LUT = []
for _i in range(257):
    _v = (2.0 ** (_i / 256.0)) * 1073741824.0
    POW2_LUT.append(2147483647 if _v >= 2147483647.0 else int(round(_v)))

INV_SQRT_N = [0, 0, 0, 18919, 16384, 14654, 13377, 12385,
              11585, 10923, 10362, 9880]

# The LIVE path's normaliser: 1/N^0.70, not 1/sqrt(N). Mirrors kInvPowN in
# shepard.cpp - the octave stack's layers are correlated copies of one source,
# so they sum faster than incoherent oscillators do.
INV_POW_N = [0, 0, 0, 23018, 18820, 16098, 14169, 12720,
             11585, 10668, 9910, 9270]

SCALE12 = [0x00000000, 0x15555555, 0x2AAAAAAB, 0x40000000,
           0x55555555, 0x6AAAAAAB, 0x80000000, 0x95555555,
           0xAAAAAAAB, 0xC0000000, 0xD5555555, 0xEAAAAAAB]
SCALE_MAJ = [0x00000000, 0x2AAAAAAB, 0x55555555, 0x6AAAAAAB,
             0x95555555, 0xC0000000, 0xEAAAAAAB]
SCALE_PENT = [0x00000000, 0x2AAAAAAB, 0x55555555, 0x95555555, 0xC0000000]
SCALES = {0: None, 1: SCALE12, 2: SCALE_MAJ, 3: SCALE_PENT}

def mul_q15(w, x):
    hi = x >> 15
    lo = x - (hi << 15)
    return w * hi + ((w * lo) >> 15)


def sin_q15(phase):
    idx = (phase >> 22) & 1023
    frac = (phase >> 7) & 0x7FFF
    s0 = SIN_LUT[idx]
    s1 = SIN_LUT[(idx + 1) & 1023]
    return s0 + mul_q15(frac, s1 - s0)


def xfade_q15(mix):
    """Equal-power crossfade legs, from the sine table. Mirrors XfadeQ15.

    Returns (g_synth, g_live). A linear fade between two UNRELATED sources
    dips 3 dB at the midpoint; cos/sin holds the power flat.
    """
    return (sin_q15(((32767 - mix) << 15) & 0xFFFFFFFF),
            sin_q15((mix << 15) & 0xFFFFFFFF))


def hann_q15(u):
    idx = (u >> 23) & 0x1FF
    frac = (u >> 8) & 0x7FFF
    s0 = SIN_LUT[idx]
    s1 = SIN_LUT[(idx + 1) & 1023]
    s = s0 + mul_q15(frac, s1 - s0)
    return mul_q15(s, s)


def pow2_q30(f):
    idx = f >> 24
    frac = (f >> 9) & 0x7FFF
    return POW2_LUT[idx] + mul_q15(frac, POW2_LUT[idx + 1] - POW2_LUT[idx])


def soft_clip_out(x):
    lim, knee = 2047, 1450
    ax = -x if x < 0 else x
    if ax <= knee:
        return x
    over = ax - knee
    room = lim - knee
    y = knee + (over * room) // (room + over)
    return -y if x < 0 else y


def snap_to_scale(pos, scale):
    tab = SCALES.get(scale)
    if tab is None:
        return pos
    n = len(tab)
    lo = 0
    for i in range(n):
        if tab[i] <= pos:
            lo = i
        else:
            break
    below = tab[lo]
    above = tab[lo + 1] if lo + 1 < n else tab[0]
    d_below = (pos - below) & 0xFFFFFFFF
    d_above = (above - pos) & 0xFFFFFFFF
    return below if d_below <= d_above else above


OCT_LEN = 1 << 15
OCT_MASK = OCT_LEN - 1
OCT_WIN = 16384
OCT_MAX_RATE = 262144


class OctaveStack:
    """OctaveStack from octave.h."""

    def __init__(self):
        self.buf = [0] * OCT_LEN
        self.write = 0
        self.drift = [0] * MAX_LAYERS

    def write_sample(self, x):
        self.buf[self.write & OCT_MASK] = max(-32768, min(32767, x))
        self.write = (self.write + 1) & OCT_MASK

    def _tap(self, p):
        idx = (p >> 16) & OCT_MASK
        frac = p & 0xFFFF
        a = self.buf[idx]
        b = self.buf[(idx + 1) & OCT_MASK]
        return a + (((b - a) * frac) >> 16)

    def read(self, i, rate_q16):
        self.drift[i] = (self.drift[i] + rate_q16 - 65536) & 0xFFFFFFFF
        while self.drift[i] >= (OCT_WIN << 16):
            self.drift[i] -= (OCT_WIN << 16)
        p0 = (((self.write - OCT_WIN) << 16) + self.drift[i]) & 0xFFFFFFFF
        p1 = (p0 + ((OCT_WIN // 2) << 16)) & 0xFFFFFFFF
        a = self._tap(p0)
        b = self._tap(p1)
        u = ((self.drift[i] >> 14) << (32 - 16)) & 0xFFFFFFFF
        g = hann_q15(u)
        return mul_q15(a, 32767 - g) + mul_q15(b, g)


class Engine:
    """The card's normal-boot render path, as main.cpp runs it."""

    def __init__(self, layers=8, width=0, scale=0, source_mix=0,
                 layer_frac=0):
        self.layers = layers
        self.layer_frac = layer_frac
        self.width = width
        self.scale = scale
        self.source_mix = source_mix

        self.osc = [0] * MAX_LAYERS
        self.inc = [0] * MAX_LAYERS
        self.oct_rate = [65536] * MAX_LAYERS
        self.win_l = [0] * MAX_LAYERS
        self.win_r = [0] * MAX_LAYERS

        self.master_free = 0
        self.master_out = 0
        self.win_phase = 0
        self.active = layers
        self.inv_sqrt = INV_SQRT_N[layers]
        self.inv_pow = INV_POW_N[layers]
        self.prev_master = None
        self.octave = OctaveStack()
        self.rate = 0
        self.shift_up = True

    def set_rate(self, octaves_per_sec):
        """Glide rate, as a Q32 master-phase increment PER SAMPLE.

        update_control() multiplies by 32 because it runs once per control
        block - see the note in main.cpp's UpdateControl(). The firmware got
        this wrong and glided 32x too slow, in audible steps.
        """
        self.rate = int(octaves_per_sec * (1 << 32) / SR)
        self.shift_up = octaves_per_sec >= 0

    def update_control(self):
        """UpdateControl() from main.cpp - runs every 32 samples."""
        self.master_free = (self.master_free + self.rate * 32) & 0xFFFFFFFF

        if self.scale == 0:
            self.master_out = self.master_free
        else:
            target = snap_to_scale(self.master_free, self.scale)
            err = (target - self.master_out) & 0xFFFFFFFF
            if err >= (1 << 31):
                err -= (1 << 32)
            self.master_out = (self.master_out + (err >> 6)) & 0xFFFFFFFF

        m = pow2_q30(self.master_out)
        inc0 = ((BASE_INC >> 5) * (m >> 15)) >> 10


        n = self.layers
        od = (0xFFFFFFFF // n) + 1
        wd = ((self.width << 17) // n) & 0xFFFFFFFF
        md = self.master_out // n

        # The octave wrap RELABELS which layer holds which gain: after the
        # wrap layer i sits where layer i-1 was. The oscillator phases must
        # rotate the same way, or a layer keeps its old phase while taking a
        # new gain and frequency - which is the big click.
        if self.prev_master is not None:
            if self.prev_master > 0xC0000000 and self.master_out < 0x40000000:
                for i in range(len(self.osc) - 1, 0, -1):
                    self.osc[i] = self.osc[i - 1]
                    self.octave.drift[i] = self.octave.drift[i - 1]
                self.octave.drift[0] = 0
            elif self.prev_master < 0x40000000 and self.master_out > 0xC0000000:
                for i in range(len(self.osc) - 1):
                    self.osc[i] = self.osc[i + 1]
                    self.octave.drift[i] = self.octave.drift[i + 1]
                self.octave.drift[-1] = 0
        self.prev_master = self.master_out

        # DISCRETE layer count - no fade. Adding a layer respaces the whole
        # stack (N is the window divisor), so there is no "entering layer" to
        # fade; both attempts to do so made things worse. See UpdateControl().
        self.active = n

        for i in range(self.active):
            inc = (inc0 << i) & 0xFFFFFFFF
            if inc > 0x7FFFFFFF:
                inc = 0x7FFFFFFF
            self.inc[i] = inc
            centre = i - (n >> 1)
            r = m >> 14
            r = (r << centre) if centre >= 0 else (r >> (-centre))
            self.oct_rate[i] = max(1024, min(OCT_MAX_RATE, r))
            u_l = (md + i * od) & 0xFFFFFFFF
            u_r = (u_l + wd) & 0xFFFFFFFF
            self.win_l[i] = hann_q15(u_l)
            self.win_r[i] = hann_q15(u_r)

    def render(self, n_samples, audio_in=None):
        """Returns (left, right) lists of int16-range samples."""
        left = []
        right = []
        for k in range(n_samples):
            if (k & 31) == 0:
                self.update_control()

            if audio_in and self.source_mix > 0:
                self.octave.write_sample(audio_in[k] << 5)

            syn_l = 0
            syn_r = 0
            liv_l = 0
            liv_r = 0
            for i in range(self.layers):
                synth = 0
                if self.source_mix < 32767:
                    self.osc[i] = (self.osc[i] + self.inc[i]) & 0xFFFFFFFF
                    synth = sin_q15(self.osc[i])

                filtered = 0
                if self.source_mix > 0:
                    filtered = self.octave.read(i, self.oct_rate[i])

                syn_l += mul_q15(synth, self.win_l[i])
                syn_r += mul_q15(synth, self.win_r[i])
                liv_l += mul_q15(filtered, self.win_l[i])
                liv_r += mul_q15(filtered, self.win_r[i])

            # Each source gets ITS OWN normalisation law, then the two are
            # crossfaded at equal power. See kInvPowN / XfadeQ15.
            self.inv_sqrt += (INV_SQRT_N[self.layers] - self.inv_sqrt) >> 5
            self.inv_pow += (INV_POW_N[self.layers] - self.inv_pow) >> 5
            gs, gl = xfade_q15(self.source_mix)
            sl = mul_q15(syn_l, self.inv_sqrt)
            sr = mul_q15(syn_r, self.inv_sqrt)
            vl = mul_q15(liv_l, self.inv_pow)
            vr = mul_q15(liv_r, self.inv_pow)
            left.append(soft_clip_out((mul_q15(sl, gs) + mul_q15(vl, gl)) >> 5))
            right.append(soft_clip_out((mul_q15(sr, gs) + mul_q15(vr, gl)) >> 5))
        return left, right


# ---------------------------------------------------------------------------
# WAV output
# ---------------------------------------------------------------------------

def write_wav(name, left, right):
    """Write 16-bit stereo. The card's output is +-2047 (12-bit), so scale by
    16 to fill the 16-bit range - a pure level change, no shaping."""
    path = os.path.join(HERE, name)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        # struct.pack per sample is the obvious form and is ~10x slower than
        # packing the whole buffer in one call. At 48 kHz stereo that is the
        # difference between a usable tool and one nobody runs twice.
        n = len(left)
        inter = [0] * (n * 2)
        for i in range(n):
            a = left[i] * 16
            b = right[i] * 16
            inter[i * 2] = -32768 if a < -32768 else (32767 if a > 32767 else a)
            inter[i * 2 + 1] = -32768 if b < -32768 else (32767 if b > 32767 else b)
        w.writeframes(struct.pack(f'<{n * 2}h', *inter))
    dur = len(left) / SR
    print(f"    {name:34s} {dur:5.1f}s")
    return path


def measure(left, right):
    """Peak, RMS and how often the soft knee is reached."""
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right))
    rms = math.sqrt(sum(v * v for v in left) / len(left))
    knee = sum(1 for v in left if abs(v) > 1450) / len(left) * 100
    return peak, rms, knee


# ---------------------------------------------------------------------------
# The renders
# ---------------------------------------------------------------------------

def render_wrap_seam(seconds=24.0):
    """THE critical listening test.

    Each render sweeps through several complete octave wraps at a rate slow
    enough that the seam, if there is one, has time to be noticed.

    N=3 is the exposed case - fewest layers, least masking. If the wrap is
    audible anywhere it is audible there. N=12 is the smoothest.

    Listen for: a click, a lurch, a level dip, or a moment where the pitch
    "resets" rather than continuing. Any of those is a real defect. What
    SHOULD happen is that the pitch appears to rise forever with no event
    marking where one cycle ends and the next begins.
    """
    print("  wrap-seam renders (the make-or-break test):")
    n = int(seconds * SR)
    out = []
    for layers in (3, 6, 12):
        for direction, tag in ((0.25, "up"), (-0.25, "dn")):
            e = Engine(layers=layers)
            e.set_rate(direction)
            l, r = e.render(n)
            peak, rms, knee = measure(l, r)
            name = f"out_wrap_{tag}_N{layers:02d}.wav"
            write_wav(name, l, r)
            print(f"      peak {peak:5d}  rms {rms:6.1f}  "
                  f"knee {knee:5.2f}%   ({seconds * 0.25:.0f} full wraps)")
            out.append(name)
    return out


def render_wrap_closeup(seconds=8.0):
    """A fast glide at N=3, so several seams pass in a short clip.

    At 1 octave/second the wrap arrives every second - if there is a
    periodic artefact it becomes a rhythm, which is far easier to hear than
    a single event in a long slow sweep.
    """
    print("  fast wrap at N=3 (seams become a rhythm if they exist):")
    n = int(seconds * SR)
    e = Engine(layers=3)
    e.set_rate(1.0)
    l, r = e.render(n)
    peak, rms, knee = measure(l, r)
    write_wav("out_wrap_fast_N03.wav", l, r)
    print(f"      peak {peak:5d}  rms {rms:6.1f}  "
          f"{seconds:.0f} wraps in {seconds:.0f}s")
    return ["out_wrap_fast_N03.wav"]


def render_stationary(seconds=6.0):
    """Main at centre. Should be a steady octave-stack drone, dead still.

    Any drift, beating or wobble here means the deadzone is not actually
    zeroing the rate, or something is accumulating that should not be.
    """
    print("  stationary (Main centred - should be perfectly still):")
    n = int(seconds * SR)
    e = Engine(layers=8)
    e.set_rate(0.0)
    l, r = e.render(n)
    peak, rms, _ = measure(l, r)
    write_wav("out_stationary_N08.wav", l, r)
    print(f"      peak {peak:5d}  rms {rms:6.1f}")
    return ["out_stationary_N08.wav"]


def render_density(seconds=4.0):
    """One clip per layer count, so density can be compared by ear.

    The level should be perceptually constant across all of these - that is
    what 1/sqrt(N) is for. If the high counts sound louder or quieter, the
    normalisation is wrong.
    """
    print("  density sweep (level should be constant - 1/sqrt(N)):")
    n = int(seconds * SR)
    out = []
    for layers in range(MIN_LAYERS, MAX_LAYERS + 1, 3):
        e = Engine(layers=layers)
        e.set_rate(0.25)
        l, r = e.render(n)
        _, rms, _ = measure(l, r)
        name = f"out_density_N{layers:02d}.wav"
        write_wav(name, l, r)
        print(f"      rms {rms:6.1f}")
        out.append(name)
    return out


def render_scales(seconds=10.0):
    """The three stepped modes, plus smooth for comparison.

    Listen for: clean steps with no click at the transition. The slew is
    1.4ms, so steps should sound articulated but not clicky.
    """
    print("  quantisation modes:")
    n = int(seconds * SR)
    out = []
    for scale, tag in ((0, "smooth"), (1, "chromatic"),
                       (2, "major"), (3, "pentatonic")):
        e = Engine(layers=8, scale=scale)
        e.set_rate(0.2)
        l, r = e.render(n)
        name = f"out_scale_{tag}.wav"
        write_wav(name, l, r)
        out.append(name)
    return out


def render_width(seconds=6.0):
    """Stereo width. Check the mono sum does not thin out."""
    print("  stereo width:")
    n = int(seconds * SR)
    out = []
    for pct in (0, 50, 100):
        e = Engine(layers=8, width=int(pct / 100 * 32767))
        e.set_rate(0.25)
        l, r = e.render(n)
        mid = [(a + b) >> 1 for a, b in zip(l, r)]
        rms_l = math.sqrt(sum(v * v for v in l) / len(l))
        rms_m = math.sqrt(sum(v * v for v in mid) / len(mid))
        loss = 20 * math.log10(rms_m / rms_l) if rms_l else -99
        name = f"out_width_{pct:03d}.wav"
        write_wav(name, l, r)
        print(f"      mono sum {loss:+.2f} dB vs left")
        out.append(name)
    return out


def make_test_input(n, kind):
    """Source material for the live-audio path."""
    sig = []
    if kind == "drone":
        for k in range(n):
            v = (1200 * math.sin(2 * math.pi * 110 * k / SR) +
                 600 * math.sin(2 * math.pi * 220 * k / SR) +
                 300 * math.sin(2 * math.pi * 330 * k / SR))
            sig.append(int(v))
    elif kind == "noise":
        s = 12345
        for _ in range(n):
            s = (1664525 * s + 1013904223) & 0xFFFFFFFF
            sig.append(((s >> 16) & 0xFFF) - 2048)
    elif kind == "chord":
        for k in range(n):
            v = sum(700 * math.sin(2 * math.pi * f * k / SR)
                    for f in (220.0, 277.2, 329.6))
            sig.append(int(v))
    return sig


def render_comb(seconds=8.0):
    """The live-audio path.

    Expect it to be INHARMONIC - a frequency shifter moves partials by hertz
    rather than by ratio, so a harmonic input comes back clanging. That is
    the character of the mode, not a fault. What would be a real defect is a
    strong unshifted component leaking through, which would mean the
    quadrature is broken.
    """
    print("  Hilbert live path (expect inharmonic - that is the mode):")
    n = int(seconds * SR)
    out = []
    for kind in ("drone", "chord", "noise"):
        src = make_test_input(n, kind)
        e = Engine(layers=8, source_mix=32767)
        e.set_rate(0.25)
        l, r = e.render(n, audio_in=src)
        name = f"out_comb_{kind}.wav"
        write_wav(name, l, r)
        out.append(name)

    # And a 50/50 blend, which is how it is most likely to be played.
    src = make_test_input(n, "drone")
    e = Engine(layers=8, source_mix=16384)
    e.set_rate(0.25)
    l, r = e.render(n, audio_in=src)
    write_wav("out_comb_blend50.wav", l, r)
    out.append("out_comb_blend50.wav")
    return out


def analyse_seam(layers, wraps=6, rate=1.0):
    """Measure the wrap seam objectively, rather than only by ear.

    CALIBRATED AGAINST A KNOWN-BROKEN BUILD. That calibration mattered: the
    first version of this function reported three measures and two of them
    turned out to be worthless. Deliberately breaking the window lookup (the
    truncation bug the unit tests caught) and re-running gave:

                        correct        broken
        level ratio     1.1896         1.1893      <- cannot tell them apart
        HF splatter     1.0934         1.0930      <- cannot tell them apart
        seam step       0.8000         1.0000      <- the real signal

    So level periodicity and HF splatter at low layer counts are NOT seam
    artefacts. They are ordinary beating between a small number of widely
    spaced oscillators - the folded profile is scattered rather than showing
    a dip at any consistent phase - and a detector built on them fires on a
    perfectly good card.

    What actually discriminates is SEAM STEP: the largest sample-to-sample
    jump near a wrap boundary, as a fraction of the largest jump anywhere in
    the signal. A discontinuity at the wrap is a step change, so a broken
    build puts the global maximum exactly at the seam and scores 1.00.

    The other two are still reported, because a large ABSOLUTE level swing
    would be worth knowing about, but they no longer decide the verdict.
    """
    n = int(wraps / abs(rate) * SR)
    e = Engine(layers=layers)
    e.set_rate(rate)
    l, _r = e.render(n)

    period = int(SR / abs(rate))          # samples per octave wrap

    # --- 1. level periodicity, phase-folded ---
    block = 256
    nblocks = len(l) // block
    energy = [0.0] * nblocks
    for b in range(nblocks):
        seg = l[b * block:(b + 1) * block]
        energy[b] = math.sqrt(sum(v * v for v in seg) / block)

    blocks_per_period = period / block
    folded = {}
    for b in range(nblocks):
        ph = int((b % blocks_per_period) / blocks_per_period * 32)
        folded.setdefault(ph, []).append(energy[b])
    means = [sum(v) / len(v) for v in folded.values() if v]
    level_ratio = (max(means) / min(means)) if means and min(means) > 0 else 99.0

    # --- 2. sample-to-sample discontinuity at the seam ---
    diffs = [abs(l[i + 1] - l[i]) for i in range(len(l) - 1)]
    max_diff = max(diffs)
    # Where do the wrap boundaries fall? The master phase starts at 0, so
    # wraps land at multiples of the period.
    seam_max = 0
    guard = 64
    for w in range(1, wraps):
        c = w * period
        lo = max(0, c - guard)
        hi = min(len(diffs), c + guard)
        if lo < hi:
            seam_max = max(seam_max, max(diffs[lo:hi]))
    seam_ratio = seam_max / max_diff if max_diff else 0.0

    # IS THAT MAXIMUM UNIQUE? If not, the ratio means nothing.
    #
    # This is the third measure in this file to need a correction of exactly
    # this kind, and the reason is always the same: a ratio can only separate
    # signal from noise if its denominator IS the signal.
    #
    # At low layer counts the signal is a handful of low sines, so the
    # largest sample-to-sample jump in the WHOLE render is a few LSB of
    # quantisation - and thousands of samples tie at it. Whether one of those
    # ties happens to land within the seam guard is then luck, so N=3 scored
    # a flat 1.0000 and was condemned as broken.
    #
    # Measured, at N=3 over 8 wraps:
    #     real build     max diff 5   shared by 27,975 samples  (3.6%)
    #     broken control max diff 8   shared by 5      samples  (0.0%)
    #
    # A genuine discontinuity is a large jump that stands ALONE. So the tie
    # count is reported alongside the ratio, and a maximum shared by more
    # than a handful of samples disqualifies the verdict rather than
    # producing one.
    ties = sum(1 for v in diffs if v == max_diff)

    # --- 3. periodic HF splatter ---
    # First difference is a crude highpass; fold it at the wrap period the
    # same way as the level.
    hf_folded = {}
    for b in range(nblocks):
        seg = diffs[b * block:(b + 1) * block]
        if not seg:
            continue
        ph = int((b % blocks_per_period) / blocks_per_period * 32)
        hf_folded.setdefault(ph, []).append(
            math.sqrt(sum(v * v for v in seg) / len(seg)))
    hf_means = [sum(v) / len(v) for v in hf_folded.values() if v]
    hf_ratio = (max(hf_means) / min(hf_means)) if hf_means and min(hf_means) > 0 else 99.0

    return level_ratio, seam_ratio, hf_ratio, ties, max_diff, len(diffs)


def check_seams():
    """Run the seam analysis, with a built-in control that proves it works.

    A detector that never fires is indistinguishable from a detector that
    cannot fire, so this deliberately breaks the window lookup and confirms
    the measure responds before trusting it on the real build.
    """
    print("  SEAM ANALYSIS - is the octave wrap detectable?")
    print("    Verdict is on SEAM STEP: the largest sample jump near a wrap,")
    print("    over the largest jump anywhere. 1.00 means the biggest")
    print("    discontinuity in the whole signal sits exactly at the seam -")
    print("    but ONLY if that maximum is unique. A maximum shared by many")
    print("    samples is the quantisation floor, not a seam, and the ratio")
    print("    is then meaningless (see analyse_seam's note on ties).")
    print("    (level and HF are reported for information - calibration")
    print("    showed they do not discriminate either.)\n")

    # --- control: prove the measure responds to a real defect ---
    import builtins
    this = sys.modules[__name__]
    good_hann = this.hann_q15

    def truncated(u):
        idx = (u >> 23) & 0x1FF
        s = SIN_LUT[idx]
        return mul_q15(s, s)

    this.hann_q15 = truncated
    ctrl_lv, ctrl_sm, ctrl_hf, ctrl_ties, ctrl_mx, len_diffs = analyse_seam(3)
    this.hann_q15 = good_hann

    print(f"    control (window lookup deliberately truncated):")
    print(f"      N=  3  seam step {ctrl_sm:.4f}  max jump {ctrl_mx} "
          f"shared by {ctrl_ties}   <- a known-broken build\n")

    print(f"    {'N':>3}  {'seam step':>10}  {'max':>5}  {'ties':>7}  "
          f"{'level':>8}  {'HF':>8}   verdict")

    ok = True
    results = []
    for layers in (3, 4, 6, 8, CARD_MAX_LAYERS):
        lv, sm, hf, ties, mx, _n = analyse_seam(layers)
        results.append((layers, lv, sm, hf, ties))
        # A maximum shared by a large FRACTION of samples is the
        # quantisation floor, not a discontinuity. Measured over 768k
        # samples: the broken control ties at 0.004% while the N=3 false
        # positive ties at 1.456% - a factor of 350, so 0.5% separates them
        # with a wide margin and no case sits near the line.
        noisy = ties > len_diffs * 0.005
        bad = (sm > 0.98) and not noisy
        if bad:
            ok = False
        verdict = "SEAM" if bad else ("no unique jump" if noisy else "seamless")
        print(f"    {layers:3d}  {sm:10.4f}  {mx:5d}  {ties:7d}  "
              f"{lv:8.4f}  {hf:8.4f}   {verdict}")

    # Is the detector actually discriminating on this run? Compare against the
    # best score among layer counts that HAVE a unique maximum - a tied one
    # tells us nothing either way.
    usable = [r[2] for r in results if r[4] <= len_diffs * 0.005]
    best = min(usable) if usable else 1.0
    discriminating = (ctrl_ties <= len_diffs * 0.005) and (ctrl_sm - best) > 0.05

    print()
    if not discriminating:
        print("    WARNING: the control scored no higher than the real build,")
        print("    so this measure is not discriminating today. Treat the")
        print("    verdict above as unproven and listen to the renders.")
        return False

    if ok:
        print(f"    Detector confirmed working (control {ctrl_sm:.2f} vs "
              f"best real {best:.2f}).")
        print("    No wrap discontinuity in the real build. The illusion")
        print("    should hold on hardware - but level variation at N=3 is")
        print("    inherent to a sparse stack and will still be audible as")
        print("    gentle beating. That is the sound of three oscillators,")
        print("    not a broken wrap.")
    else:
        print("    A wrap discontinuity IS present - check the window and")
        print("    the layer increments, then listen to out_wrap_fast_N03.")
    return ok


def spectrograms(names):
    """Render spectrograms so the wrap can be SEEN as well as heard.

    A Shepard tone has an unmistakable signature: diagonal streaks that
    march off the top of the picture while new ones appear at the bottom,
    forever. A broken wrap shows as a discontinuity across all the streaks
    at the same instant.
    """
    try:
        import numpy as np
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("  (spectrograms need numpy + matplotlib - skipped)")
        return

    print("  spectrograms:")
    for name in names:
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            continue
        with wave.open(path, 'rb') as w:
            frames = w.readframes(w.getnframes())
        data = np.frombuffer(frames, dtype='<i2')[0::2].astype(float)

        fig, ax = plt.subplots(figsize=(12, 5))
        ax.specgram(data, NFFT=4096, Fs=SR, noverlap=3072,
                    cmap='magma', vmin=-40)
        ax.set_yscale('symlog', linthresh=100)
        ax.set_ylim(20, SR / 2)
        ax.set_xlabel('time (s)')
        ax.set_ylabel('frequency (Hz)')
        ax.set_title(f'{name}  -  diagonal streaks marching off the top '
                     f'is the illusion working')
        png = path.replace('.wav', '.png')
        fig.tight_layout()
        fig.savefig(png, dpi=90)
        plt.close(fig)
        print(f"    {os.path.basename(png)}")


def check_live_levels():
    """The LIVE path's level, across density and across the crossfade.

    Two hardware reports, both about the same thing seen from opposite ends:

      "the audio in/out seems very quiet compared to the generated tones"
      "actually depends on the number of layers of notes"
      "when the X knob is CW the internal sounds massively dominate"

    Three distinct faults were behind them, and this pins all three.

    1. 1/sqrt(N) IS THE WRONG LAW FOR THE LIVE PATH. It is correct for the
       oscillator bank, whose layers are incoherent so their powers add. The
       octave stack is N CORRELATED copies of one source; measured growth is
       N^0.70, so 1/sqrt(N) over-normalises, worst at low N. Fixed by
       kInvPowN.

    2. THE CROSSFADE WAS LINEAR. Two unrelated sources faded linearly lose
       3 dB at the midpoint, so the mix sat below BOTH sources - which reads
       as whichever source is louder dominating. Fixed by XfadeQ15 (cos/sin
       out of the existing sine table).

    3. CREST FACTOR, which is NOT a fault and is not fixed here - recorded so
       it is not chased later. The live path's crest is ~3.8 against the
       synth's ~1.8, because correlated copies peak together. At matched
       PEAK it therefore carries ~6 dB less RMS. Loudness follows RMS and the
       rail is set by peak, so the live path will always sound a little
       softer than its peak level suggests. Raising its gain to match by ear
       would clip it.
    """
    print("  live path level (kInvPowN + equal-power crossfade):")
    n = SR // 2
    src = make_test_input(n, 'drone')
    pk = max(abs(v) for v in src)
    src = [v * 2047 // pk for v in src]

    ok = True
    lv = []
    for N in range(MIN_LAYERS, CARD_MAX_LAYERS + 1):
        e = Engine(layers=N, source_mix=32767)
        e.set_rate(0.25)
        left, _ = e.render(n, audio_in=src)
        r = math.sqrt(sum(v * v for v in left) / n)
        peak = max(abs(v) for v in left)

        e = Engine(layers=N, source_mix=0)
        e.set_rate(0.25)
        sl, _ = e.render(n)
        s = math.sqrt(sum(v * v for v in sl) / n)

        d = 20 * math.log10(r / s)
        hd = 20 * math.log10(2047 / peak) if peak else 99
        lv.append(r)
        flag = "ok"
        if abs(d) > 3.0:
            flag = "TOO FAR FROM SYNTH"
            ok = False
        elif peak > 2047:
            flag = "CLIPS"
            ok = False
        print(f"    N={N:2d}: live {r:6.1f}  synth {s:6.1f}  {d:+5.1f} dB  "
              f"peak {peak:5d} ({hd:+.1f} dB)  {flag}")

    spread = 20 * math.log10(max(lv) / min(lv))
    print(f"    spread across density: {spread:.1f} dB", end="  ")
    if spread <= 1.5:
        print("flat  ok")
    else:
        print("VARIES WITH DENSITY - check kInvPowN")
        ok = False

    # The crossfade legs themselves.
    worst = 0.0
    for mix in range(0, 32768, 97):
        gs, gl = xfade_q15(mix)
        p = math.sqrt((gs / 32767.0) ** 2 + (gl / 32767.0) ** 2)
        worst = max(worst, abs(p - 1.0))
    print(f"    crossfade power deviation: {worst * 100:.4f}%", end="  ")
    if worst < 0.01:
        print("equal power  ok")
    else:
        print("NOT EQUAL POWER")
        ok = False

    # End to end: the midpoint must not sit below both ends. That is the
    # signature of a linear fade between uncorrelated sources.
    for N in (3, 7, 11):
        lev = []
        for mix in (0, 16384, 32767):
            e = Engine(layers=N, source_mix=mix)
            e.set_rate(0.25)
            left, _ = e.render(n, audio_in=src)
            lev.append(math.sqrt(sum(v * v for v in left) / n))
        floor = min(lev[0], lev[2])
        d = 20 * math.log10(lev[1] / floor)
        flag = "ok" if d > -1.5 else "MID-FADE DIP - linear crossfade?"
        if d <= -1.5:
            ok = False
        print(f"    N={N:2d}: synth {lev[0]:6.1f}  mid {lev[1]:6.1f}  "
              f"live {lev[2]:6.1f}   mid vs quieter end {d:+5.1f} dB  {flag}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--all', action='store_true',
                    help='render every mode, not just the wrap-seam set')
    ap.add_argument('--spectrogram', action='store_true',
                    help='also write spectrogram PNGs')
    ap.add_argument('--seconds', type=float, default=16.0,
                    help='length of the wrap-seam renders (default 24)')
    ap.add_argument('--analyse-only', action='store_true',
                    help='run the seam analysis without rendering WAVs')
    args = ap.parse_args()

    print("SHEPARD renderer - the exact integer engine, written to WAV")
    print(f"  {SR} Hz, output scaled x16 from the card's +-2047\n")

    # The objective check runs first and always: it answers the make-or-break
    # question without needing anyone to listen.
    seam_ok = check_seams()
    print()
    level_ok = check_live_levels()
    print()

    if args.analyse_only:
        return 0 if (seam_ok and level_ok) else 1

    names = []
    names += render_wrap_seam(args.seconds)
    names += render_wrap_closeup()
    names += render_stationary()

    if args.all:
        names += render_density()
        names += render_scales()
        names += render_width()
        names += render_comb()

    if args.spectrogram:
        spectrograms(names)

    print(f"\n  {len(names)} files in tools/")
    print("\nWHAT TO LISTEN FOR")
    print("  The wrap renders should sound like a pitch that rises (or")
    print("  falls) forever, with NO event marking where one cycle ends.")
    print("  A click, a lurch, a level dip, or an audible 'reset' is a real")
    print("  defect. N=3 is the exposed case - check that one hardest.")
    print("  out_wrap_fast_N03.wav turns any seam into a rhythm at 1 Hz,")
    print("  which is much easier to notice than a single event.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
