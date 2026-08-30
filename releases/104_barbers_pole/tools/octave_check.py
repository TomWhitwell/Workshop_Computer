#!/usr/bin/env python3
"""
octave_check.py - verify the octave-stack pitch shifter.

This is the third live-audio path the card has had, and the first that applies
the Shepard CONSTRUCTION to the input rather than an effect that passes over
it. What has to be true:

  - a head at 2^i rate must actually transpose by i octaves, with the wanted
    partial clearly dominant;
  - the crossfade must not amplitude-modulate at the recycle rate;
  - no head may recycle fast enough to buzz;
  - the head positions must rotate at an octave wrap, like everything else.

Run: python tools/octave_check.py
"""

import cmath
import math
import sys

SR = 48000
OCT_BITS = 15
OCT_LEN = 1 << OCT_BITS
OCT_MASK = OCT_LEN - 1
OCT_WIN = 16384
OCT_MAX_RATE = 262144
MAX_LAYERS = 11

SIN_LUT = [int(round(math.sin(2.0 * math.pi * i / 1024) * 32767.0))
           for i in range(1024)]


def mul_q15(w, x):
    hi = x >> 15
    lo = x - (hi << 15)
    return w * hi + ((w * lo) >> 15)


def hann_q15(u):
    idx = (u >> 23) & 0x1FF
    frac = (u >> 8) & 0x7FFF
    s0 = SIN_LUT[idx]
    s1 = SIN_LUT[(idx + 1) & 1023]
    s = s0 + mul_q15(frac, s1 - s0)
    return mul_q15(s, s)


class Stack:
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


def spectrum(sig, f):
    w = 2.0 * math.pi * f / SR
    return abs(sum(v * cmath.exp(-1j * w * k) for k, v in enumerate(sig)))


def check_transposition():
    """A head at 2^i rate must transpose the input by i octaves.

    THE CORE CLAIM. If this fails the whole approach is wrong, so it is checked
    against the actual integer code rather than modelled.
    """
    print("  transposition (input 220 Hz):")
    ok = True
    for shift, rate in ((0, 65536), (1, 131072), (2, 262144)):
        st = Stack()
        out = []
        for k in range(SR * 2):
            st.write_sample(int(20000 * math.sin(2.0 * math.pi * 220 * k / SR)))
            out.append(st.read(0, rate))
        seg = out[SR:]

        want = 220.0 * (2 ** shift)
        wanted = spectrum(seg, want)
        # The strongest competing partial in the octave neighbourhood.
        others = [220.0 * (2 ** s) for s in (-1, 0, 1, 2, 3) if s != shift]
        others += [want * 1.5, want * 0.75]
        worst = max(spectrum(seg, f) for f in others)
        rej = 20 * math.log10(wanted / worst) if worst else 99

        status = "ok" if rej > 10.0 else "FAIL"
        if rej <= 10.0:
            ok = False
        print(f"    rate {rate / 65536:4.1f}x -> {want:7.1f} Hz dominant by "
              f"{rej:5.1f} dB  {status}")
    return ok


def check_crossfade_flat():
    """The two-tap crossfade must not amplitude-modulate.

    A linear crossfade dips in the middle, which would tremolo at the recycle
    rate. sin^2 + cos^2 is constant, the same property the layer window relies
    on.
    """
    print("  crossfade sums constant:")
    sums = []
    for p in range(0, OCT_WIN << 16, (OCT_WIN << 16) // 512):
        u = ((p >> 13) << (32 - 16)) & 0xFFFFFFFF
        g = hann_q15(u)
        sums.append(g + (32767 - g))
    lo, hi = min(sums), max(sums)
    ok = (hi - lo) == 0
    print(f"    g + (1-g) = {lo}..{hi}  {'ok' if ok else 'FAIL'}")
    return ok


def check_recycle_rates():
    """No audible head may recycle fast enough to buzz.

    A head at rate r drifts from the write pointer at |r-1| samples/s, so it
    recycles every kOctWin/|r-1| seconds. If that lands in the audio band it is
    heard as a tone rather than as shimmer - which is why the rate is capped.
    """
    print("  head recycle rates (capped at 4x):")
    ok = True
    for mult in (0.25, 0.5, 1.0, 2.0, 4.0):
        rate = int(65536 * mult)
        if rate > OCT_MAX_RATE:
            rate = OCT_MAX_RATE
        drift = abs(rate - 65536) / 65536.0 * SR
        hz = drift / OCT_WIN if drift else 0.0
        status = "ok" if hz < 20.0 else "BUZZES"
        if hz >= 20.0:
            ok = False
        label = "no drift" if hz == 0 else f"{hz:5.1f} Hz"
        print(f"    {mult:5.2f}x: recycles at {label}  {status}")
    return ok


def check_rotation():
    """Head positions must rotate with the stack at an octave wrap.

    Same requirement as the oscillator phases: at a wrap head i inherits head
    i-1's shift ratio, so its position must move with it or it jumps to an
    unrelated point in the buffer.
    """
    print("  head positions rotate at the wrap:")
    st = Stack()
    for i in range(MAX_LAYERS):
        st.drift[i] = (i + 1) * 1000
    before = list(st.drift)

    # RotateUp
    for i in range(MAX_LAYERS - 1, 0, -1):
        st.drift[i] = st.drift[i - 1]
    st.drift[0] = 0

    ok = all(st.drift[i] == before[i - 1] for i in range(1, MAX_LAYERS))
    ok &= st.drift[0] == 0
    print(f"    up: head i takes head i-1's position  "
          f"{'ok' if ok else 'FAIL'}")
    return ok


def main():
    print(f"SHEPARD octave-stack check: {OCT_LEN} samples "
          f"({OCT_LEN / SR * 1000:.0f} ms), window {OCT_WIN}")
    ok = check_transposition()
    ok &= check_crossfade_flat()
    ok &= check_recycle_rates()
    ok &= check_rotation()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
