#!/usr/bin/env python3
"""8mu accelerometer semantics: they are LIFT gestures.

THE FACT EVERYTHING TURNS ON, and it took five wrong versions to establish:

    Each gesture reads 0 when the device is LEVEL and rises as that side is
    lifted. A level 8mu sends 0 on all four. They are NOT a complementary
    pair adding to 127, and there is NO centre detent at 64.

So a physical axis is the DIFFERENCE of its pair:

    axis = lift_front - lift_back      -127 .. +127, zero when level

Both halves must be read, because here each carries real information -
unlike a complementary pair, where one half is redundant.

The version before this assumed a detent at 64. The consequence is worth
spelling out, because it is a good example of an assumption producing a
symptom that looks like something else entirely:

    volume = full - (|cc42 - 64|)^2

    level     (cc42 = 0)   -> deviation 64 -> SILENT
    half lift (cc42 = 64)  -> deviation  0 -> full volume
    full lift (cc42 = 127) -> deviation 63 -> SILENT

Full volume happened only at one specific half-lifted angle, and the
natural resting position was silent. Reported from hardware as "it feels
like only a very specific angle has volume" - which is precisely what the
arithmetic predicts, and a much better description of the fault than
anything the code comments claimed at the time.

VOLUME IS NOW A FADER PLUS A BIPOLAR TILT. Fader 7 sets the base level and
the front/back lift swings a full scale either side of it, so a fader at
half gives a full swell above and a full duck below. Rounding is the
left/right lift, bipolar about level in the sense that lifting either side
moves toward the rounded (OO) face - direction does not matter there, only
distance.

Mute is disabled in this build at the player's request; it was confusing
the diagnosis. Its polarity note is kept in midi8mu.h for whenever it comes
back.

Checks:
  1. A LEVEL device is at full volume when the fader is up.
  2. The fader alone is a working volume control.
  3. Lifting the front ducks; lifting the back swells.
  4. The tilt curve is gentle near level and steep at a real lift.
  5. Rounding is unrounded when level and rounded at either lift.
  6. The click defaults to FULL level and length without an 8mu.
  7. Fader 6 sets the click level.
  8. Fader 2 sets the click decay, from a short hit to a steady tone.
  9. The volume tilt is inverted: lifting the back swells.

Run: python tools/gesture_check.py
"""

import sys
import math

CC_CLICK_DECAY = 35   # fader 2
CC_CLICK_LEVEL = 39   # fader 6
CC_VOLUME = 40        # fader 7 - base volume
CC_LIFT_FRONT = 42
CC_LIFT_BACK = 43
CC_LIFT_LEFT = 44
CC_LIFT_RIGHT = 45
TILT_VOLUME_RANGE = 32767


class Firmware:
    """Transcription of the accelerometer path in midi8mu.cpp."""

    def __init__(self):
        self.vol_fader = 32767
        self.front = 0
        self.back = 0
        self.left = 0
        self.right = 0
        self.volume = 32767
        self.round = 0
        self.freeze = 0
        # Both default full: with no 8mu the panel's plosive inputs must
        # be at full level and full length, since no knob controls them.
        self.click_level = 32767
        self.click_decay = 32767
        self.click_level_from_midi = 0
        self.click_decay_from_midi = 0

    @staticmethod
    def _tilt_signed(a, b):
        d = a - b
        mag = abs(d)
        q = (mag * mag * 32767) // (127 * 127)
        return -q if d < 0 else q

    def _update_volume(self):
        # back minus front: lifting the back swells. See midi8mu.cpp.
        v = self.vol_fader + (
            (self._tilt_signed(self.back, self.front) * TILT_VOLUME_RANGE)
            >> 15)
        self.volume = max(0, min(32767, v))

    def _update_round(self):
        t = self._tilt_signed(self.left, self.right)
        self.round = abs(t)

    def cc(self, cc, v):
        if cc == CC_CLICK_LEVEL:
            self.click_level = v << 8
            self.click_level_from_midi = 1
        elif cc == CC_CLICK_DECAY:
            self.click_decay = v << 8
            self.click_decay_from_midi = 1
        elif cc == CC_VOLUME:
            self.vol_fader = v << 8
            self._update_volume()
        elif cc == CC_LIFT_FRONT:
            self.front = v
            self._update_volume()
        elif cc == CC_LIFT_BACK:
            self.back = v
            self._update_volume()
        elif cc == CC_LIFT_LEFT:
            if not self.freeze:
                self.left = v
                self._update_round()
        elif cc == CC_LIFT_RIGHT:
            if not self.freeze:
                self.right = v
                self._update_round()


def db(v):
    return 20 * math.log10(max(v, 1) / 32767)


def check_level_is_full():
    """THE regression. A level device must be loud, not silent."""
    print("\n1. A LEVEL device is at full volume")
    ok = True

    f = Firmware()
    good = f.volume == 32767
    print(f"   before any message        -> {f.volume:5d}   "
          f"{'ok' if good else 'FAIL'}")
    ok &= good

    # Fader up, device flat: all four lifts at zero.
    f = Firmware()
    f.cc(CC_VOLUME, 127)
    for cc in (CC_LIFT_FRONT, CC_LIFT_BACK, CC_LIFT_LEFT, CC_LIFT_RIGHT):
        f.cc(cc, 0)
    good = f.volume > 32000
    print(f"   fader up, device level    -> {f.volume:5d}  {db(f.volume):5.1f} dB"
          f"   {'ok' if good else '<-- SILENT WHEN LEVEL (the bug)'}")
    ok &= good
    print("   (the centre-detent version scored 0 here - full volume")
    print("    happened only at a specific half-lifted angle)")
    return ok


def check_fader():
    print("\n2. Fader 7 alone is a working volume control")
    ok = True
    prev = None
    monotonic = True
    for cc in (0, 32, 64, 96, 127):
        f = Firmware()
        f.cc(CC_VOLUME, cc)
        if prev is not None and f.volume < prev:
            monotonic = False
        prev = f.volume
        print(f"   fader {cc:3d} -> {f.volume:5d}  {db(f.volume):6.1f} dB")
    lo = Firmware()
    lo.cc(CC_VOLUME, 0)
    hi = Firmware()
    hi.cc(CC_VOLUME, 127)
    good = lo.volume < 500 and hi.volume > 32000 and monotonic
    if not good:
        ok = False
    print(f"   full range and monotonic   {'ok' if good else '<-- FAIL'}")
    return ok


def check_tilt_swings():
    print("\n3. Lifting the front ducks, lifting the back swells")
    ok = True

    # From the top, lifting the FRONT must reach silence.
    f = Firmware()
    f.cc(CC_VOLUME, 127)
    f.cc(CC_LIFT_FRONT, 127)
    good = f.volume < 500
    print(f"   fader full, front lifted  -> {f.volume:5d}   "
          f"{'ok - silent' if good else '<-- CANNOT DUCK'}")
    ok &= good

    # From half, both directions must reach the ends.
    f = Firmware()
    f.cc(CC_VOLUME, 64)
    f.cc(CC_LIFT_BACK, 127)
    up = f.volume
    f2 = Firmware()
    f2.cc(CC_VOLUME, 64)
    f2.cc(CC_LIFT_FRONT, 127)
    down = f2.volume
    good = up > 32000 and down < 500
    print(f"   fader half: back {up:5d}, front {down:5d}   "
          f"{'ok - full swing both ways' if good else '<-- LIMITED'}")
    ok &= good
    print("   (this is the expressive part: the fader sets where the")
    print("    wrist's neutral sits, and the tilt swings around it)")
    return ok


def check_tilt_curve():
    print("\n4. The tilt curve is gentle near level")
    ok = True
    print("   lift front  volume      dB")
    for b in (0, 16, 32, 64, 96, 127):
        f = Firmware()
        f.cc(CC_VOLUME, 127)
        f.cc(CC_LIFT_FRONT, b)
        print(f"      {b:3d}       {f.volume:5d}   {db(f.volume):6.1f}")

    # A small unintended lift must not cost real level.
    f = Firmware()
    f.cc(CC_VOLUME, 127)
    f.cc(CC_LIFT_FRONT, 32)
    d = db(f.volume)
    good = d > -1.5
    if not good:
        ok = False
    print(f"   a quarter lift costs {d:.1f} dB   "
          f"{'ok - holding it roughly level is fine' if good else '<-- TWITCHY'}")
    return ok


def check_round():
    print("\n5. Rounding: unrounded when level, rounded at either lift")
    ok = True

    f = Firmware()
    f.cc(CC_LIFT_LEFT, 0)
    f.cc(CC_LIFT_RIGHT, 0)
    good = f.round < 200
    print(f"   level          -> round {f.round:5d}   "
          f"{'ok - unrounded' if good else '<-- ROUNDED AT REST'}")
    ok &= good

    for cc, name in ((CC_LIFT_LEFT, "left"), (CC_LIFT_RIGHT, "right")):
        f = Firmware()
        f.cc(cc, 127)
        good = f.round > 32000
        if not good:
            ok = False
        print(f"   {name:5} lifted   -> round {f.round:5d}   "
              f"{'ok - toward OO' if good else '<-- FAIL'}")

    # Freeze must hold the vowel.
    f = Firmware()
    f.cc(CC_LIFT_LEFT, 100)
    before = f.round
    f.freeze = 1
    f.cc(CC_LIFT_LEFT, 0)
    good = f.round == before
    if not good:
        ok = False
    print(f"   freeze holds rounding   {'ok' if good else 'FAIL'}")
    return ok




PLOSIVE_MIN = 384
PLOSIVE_MAX = 48000


def decay_samples(q15):
    sq = (q15 * q15) >> 15
    return PLOSIVE_MIN + (((PLOSIVE_MAX - PLOSIVE_MIN) * sq) >> 15)


def check_click_defaults():
    """With no 8mu the click must be FULL, not quiet or clipped short."""
    print("\n6. Click defaults to full without an 8mu")
    ok = True
    f = Firmware()
    good = f.click_level == 32767 and f.click_level_from_midi == 0
    print(f"   level  {f.click_level:5d}, from_midi {f.click_level_from_midi}   "
          f"{'ok - full' if good else '<-- QUIET WITHOUT A CONTROLLER'}")
    ok &= good

    good = f.click_decay == 32767 and f.click_decay_from_midi == 0
    ms = decay_samples(f.click_decay) / 48.0
    print(f"   decay  {f.click_decay:5d} = {ms:.0f} ms   "
          f"{'ok - full length' if good else '<-- CLIPPED SHORT'}")
    ok &= good
    print("   (the panel has no knob for either, so a card on its own must")
    print("    not be quieter than one with a controller plugged in)")
    return ok


def check_click_level():
    print("\n7. Fader 6 sets the click level")
    ok = True
    prev = None
    monotonic = True
    for cc in (0, 32, 64, 96, 127):
        f = Firmware()
        f.cc(CC_CLICK_LEVEL, cc)
        if prev is not None and f.click_level < prev:
            monotonic = False
        prev = f.click_level
        print(f"   cc {cc:3d} -> level {f.click_level:5d}  "
              f"{db(f.click_level):6.1f} dB")
    lo = Firmware()
    lo.cc(CC_CLICK_LEVEL, 0)
    hi = Firmware()
    hi.cc(CC_CLICK_LEVEL, 127)
    good = lo.click_level == 0 and hi.click_level > 32000 and monotonic
    if not good:
        ok = False
    print(f"   silent at 0, full at 127, monotonic   "
          f"{'ok' if good else 'FAIL'}")
    return ok


def check_click_decay():
    print("\n8. Fader 2 sets the click decay, short hit to steady tone")
    ok = True
    for cc in (0, 32, 64, 96, 127):
        f = Firmware()
        f.cc(CC_CLICK_DECAY, cc)
        ms = decay_samples(f.click_decay) / 48.0
        print(f"   cc {cc:3d} -> {ms:8.1f} ms")

    lo = Firmware()
    lo.cc(CC_CLICK_DECAY, 0)
    hi = Firmware()
    hi.cc(CC_CLICK_DECAY, 127)
    lo_ms = decay_samples(lo.click_decay) / 48.0
    hi_ms = decay_samples(hi.click_decay) / 48.0
    good = lo_ms < 15 and hi_ms > 900
    if not good:
        ok = False
    print(f"   {lo_ms:.0f} ms to {hi_ms:.0f} ms   "
          f"{'ok - consonant to sustained' if good else 'FAIL'}")

    # The short end must get most of the travel: that is where the useful
    # consonant lengths live, and a linear map would waste the fader on
    # the difference between 800 ms and 900 ms.
    mid = Firmware()
    mid.cc(CC_CLICK_DECAY, 64)
    mid_ms = decay_samples(mid.click_decay) / 48.0
    good = mid_ms < hi_ms / 2
    if not good:
        ok = False
    print(f"   halfway is {mid_ms:.0f} ms, well under half of {hi_ms:.0f}   "
          f"{'ok - short end has the travel' if good else 'FAIL'}")
    return ok


def check_volume_inverted():
    """Lifting the BACK must swell, not duck."""
    print("\n9. Volume tilt is inverted: lifting the back swells")
    ok = True
    f = Firmware()
    f.cc(CC_VOLUME, 64)
    f.cc(CC_LIFT_BACK, 127)
    back = f.volume
    f2 = Firmware()
    f2.cc(CC_VOLUME, 64)
    f2.cc(CC_LIFT_FRONT, 127)
    front = f2.volume
    good = back > 32000 and front < 500
    print(f"   from half: back lifted {back:5d}, front lifted {front:5d}   "
          f"{'ok - back swells' if good else '<-- NOT INVERTED'}")
    ok &= good
    return ok


def main():
    print("TRACT8 8mu accelerometer and click check")
    print("  LIFT gestures: 0 when level, rising as a side is lifted.")
    ok = check_level_is_full()
    ok &= check_fader()
    ok &= check_tilt_swings()
    ok &= check_tilt_curve()
    ok &= check_round()
    ok &= check_click_defaults()
    ok &= check_click_level()
    ok &= check_click_decay()
    ok &= check_volume_inverted()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
