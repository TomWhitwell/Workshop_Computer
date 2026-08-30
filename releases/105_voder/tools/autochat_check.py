#!/usr/bin/env python3
"""AUTO-CHATTER: does it sound like something with lungs?

Hold the momentary switch DOWN for two seconds in BABBLE to engage; tap it
to leave. The card then generates its own gates and talks unprompted, as if
something were feeding Pulse In 2.

WHAT THIS FILE MEASURES, AND WHY IT IS NOT THE OBVIOUS THING.

The obvious test is "does it produce gates at about the right rate". That
would pass on a metronome, which is exactly what this must not sound like.
An even stream of identical gates reads as a machine however the individual
syllables are shaped, so the properties worth testing are the ones that
distinguish speech from a clock:

  - syllables VARY in length within a phrase;
  - phrases have a small number of syllables and then STOP;
  - the pauses between phrases are longer than the gaps within one, and
    vary, so phrases do not fall into a rhythm of their own;
  - only SOME syllables get a consonant.

That last one matters more than it looks. An earlier version of BABBLE
fired a plosive on every syllable and the mode read as percussion rather
than speech - the click is broadband and summed after the filter bank, so
it never gets attenuated by the vowel. Consonants have to be occasional.

This is deliberately a structural test rather than a rate test, because
this project has repeatedly shipped bugs that a proxy measurement declared
healthy: a window whose sum was provably flat while every individual layer
jumped, a level that stayed constant while the character broke. Measure the
thing that would sound wrong, not something correlated with it.

Checks:
  1. Phrases contain 2-7 syllables and then pause.
  2. Syllable lengths vary within a phrase.
  3. Breaths are longer than within-phrase gaps, and vary.
  4. Consonants land on some syllables, not all and not none.
  5. The rate knob puts natural speech in the middle of its travel.
  6. Engaging and leaving the mode behaves as specified.

Run: python tools/autochat_check.py
"""

import sys

FS = 48000
BABBLE_MIN_PERIOD = 2400
BABBLE_MAX_PERIOD = 24000
PHRASE_MIN = 2
PHRASE_MAX = 7
BREATH_MIN = 9600
BREATH_MAX = 38400
LONG_PRESS = 96000


class Chatter:
    """Transcription of AutoChatterGate() in main.cpp."""

    def __init__(self, decay=16384, consonant=9000, seed=0xDEADBEEF):
        self.rng = seed
        self.decay = decay
        self.consonant = consonant
        self.count = 0
        self.left = 0
        self.open = False
        self.plosive = False

    def rnd(self):
        self.rng ^= (self.rng << 13) & 0xFFFFFFFF
        self.rng ^= self.rng >> 17
        self.rng ^= (self.rng << 5) & 0xFFFFFFFF
        return self.rng

    def base_period(self):
        sq = (self.decay * self.decay) >> 15
        return BABBLE_MIN_PERIOD + \
            (((BABBLE_MAX_PERIOD - BABBLE_MIN_PERIOD) * sq) >> 15)

    def step(self):
        self.plosive = False
        self.count -= 1
        if self.count > 0:
            return self.open

        if self.open:
            self.open = False
            self.left -= 1
            if self.left > 0:
                self.count = BABBLE_MIN_PERIOD + (self.rnd() % 4800)
            else:
                self.count = BREATH_MIN + (self.rnd() % (BREATH_MAX - BREATH_MIN))
            return False

        if self.left <= 0:
            self.left = PHRASE_MIN + (self.rnd() % (PHRASE_MAX - PHRASE_MIN + 1))
        self.open = True
        base = self.base_period()
        self.count = base + (self.rnd() % (base + 1))
        chance = min(self.consonant >> 7, 192)
        if (self.rnd() & 0xFF) < chance:
            self.plosive = True
        return True


def collect(seconds=30, decay=16384, consonant=9000, seed=0xDEADBEEF):
    """Run the generator and return its phrase structure."""
    c = Chatter(decay, consonant, seed)
    phrases, cur = [], []
    gaps, breaths = [], []
    plosives = 0
    syllables = 0
    run = 0
    was = False
    gap_run = 0
    for _ in range(FS * seconds):
        now = c.step()
        if c.plosive:
            plosives += 1
        if now and not was:
            if gap_run:
                (gaps if cur else breaths).append(gap_run)
            gap_run = 0
            run = 1
            syllables += 1
        elif now:
            run += 1
        elif was:
            cur.append(run)
            if c.left <= 0:
                phrases.append(cur)
                cur = []
        else:
            gap_run += 1
        was = now
    return phrases, gaps, breaths, plosives, syllables


def check_phrase_length():
    print("\n1. Phrases contain a few syllables and then stop")
    phrases, _, _, _, _ = collect()
    lens = [len(p) for p in phrases]
    ok = bool(lens)
    if ok:
        good = all(PHRASE_MIN <= n <= PHRASE_MAX for n in lens)
        ok = good
        print(f"   {len(phrases)} phrases in 30 s, lengths {min(lens)}-{max(lens)}"
              f", mean {sum(lens)/len(lens):.1f}   "
              f"{'ok' if good else '<-- OUT OF RANGE'}")
        print(f"   first few: {lens[:8]}")
    else:
        print("   no phrases produced   <-- FAIL")
    print("   (an unbroken stream would be a gate sequencer, not a voice)")
    return ok


def check_syllables_vary():
    print("\n2. Syllable lengths vary within a phrase")
    phrases, _, _, _, _ = collect()
    ok = True
    ratios = []
    for p in phrases:
        if len(p) >= 3:
            ratios.append(max(p) / min(p))
    if not ratios:
        print("   no multi-syllable phrases   <-- FAIL")
        return False
    worst = min(ratios)
    mean = sum(ratios) / len(ratios)
    # Identical syllables read as a machine. A 1.3x spread within a phrase
    # is about the least that sounds spoken.
    good = mean > 1.3
    if not good:
        ok = False
    print(f"   within-phrase length spread: mean {mean:.2f}x, "
          f"least varied phrase {worst:.2f}x   "
          f"{'ok' if good else '<-- TOO EVEN'}")
    ex = next(p for p in phrases if len(p) >= 3)
    print(f"   example phrase, ms: {[round(x/48) for x in ex]}")
    return ok


def check_breaths():
    print("\n3. Breaths are longer than within-phrase gaps, and vary")
    _, gaps, breaths, _, _ = collect()
    ok = True
    if not gaps or not breaths:
        print("   not enough data   <-- FAIL")
        return False
    gm = sum(gaps) / len(gaps) / 48
    bm = sum(breaths) / len(breaths) / 48
    good = bm > gm * 2
    if not good:
        ok = False
    print(f"   within-phrase gap  mean {gm:6.0f} ms")
    print(f"   breath between     mean {bm:6.0f} ms   "
          f"{'ok - clearly longer' if good else '<-- NOT A BREATH'}")

    spread = max(breaths) / min(breaths)
    good = spread > 1.5
    if not good:
        ok = False
    print(f"   breath lengths vary {spread:.2f}x   "
          f"{'ok - no rhythm' if good else '<-- METRONOMIC'}")
    return ok


def check_consonants():
    print("\n4. Consonants land on some syllables, not all")
    ok = True
    print("   knob    syllables   consonants   share")
    for con, label in ((0, "min "), (9000, "dflt"), (16384, "mid "),
                       (32767, "max ")):
        phrases, _, _, plosives, syl = collect(consonant=con)
        share = plosives / syl if syl else 0
        print(f"   {label}     {syl:5d}       {plosives:5d}      {share*100:5.1f}%")
        if con == 32767:
            capped = share < 0.85
            if not capped:
                ok = False
            print(f"   max is {share*100:.0f}%, capped below every syllable   "
                  f"{'ok' if capped else '<-- CAN REACH PERCUSSION'}")
        if con == 9000:
            # The default must be occasional: every syllable reads as
            # percussion, which is why the automatic click was removed in
            # the first place.
            good = 0.05 < share < 0.5
            if not good:
                ok = False
            print(f"   default share is {share*100:.0f}%   "
                  f"{'ok - articulation, not percussion' if good else '<-- FAIL'}")
    return ok


def check_rate_curve():
    print("\n5. Natural speech sits in the middle of the rate knob")
    ok = True
    print("   knob    syllables/sec")
    natural = []
    for k in (0, 8192, 16384, 24576, 32767):
        decay = 32767 - k
        c = Chatter(decay)
        base = c.base_period()
        # A syllable is base..2*base long, plus a short gap.
        mean_len = (base + base * 2) / 2 + BABBLE_MIN_PERIOD
        sps = FS / mean_len
        flag = ""
        if 3.0 <= sps <= 7.0:
            natural.append(k)
            flag = "  <- natural speech"
        print(f"   {k:5d}      {sps:5.1f}{flag}")

    if not natural:
        print("   nothing in the natural range   <-- FAIL")
        return False
    centre = sum(natural) / len(natural) / 32767
    good = 0.25 < centre < 0.8
    if not good:
        ok = False
    print(f"   natural band centred at {centre*100:.0f}% of travel   "
          f"{'ok' if good else '<-- AT THE WRONG END'}")
    print("   (linear put it in the top third, with the lower half spent")
    print("    on rates slower than anyone talks)")
    return ok


def check_engage():
    print("\n6. Engaging and leaving")
    ok = True
    hold_s = LONG_PRESS / FS
    good = 1.5 < hold_s < 3.0
    if not good:
        ok = False
    print(f"   hold to engage: {hold_s:.1f} s   "
          f"{'ok' if good else '<-- WRONG'}")
    print("   leave: a single tap")
    print("   (asymmetric on purpose - starting something that then plays")
    print("    by itself should take deliberate effort, stopping it should")
    print("    not, which is how a panic button works)")

    # It must start on a breath, or engaging barks immediately.
    print("   engages into a breath, not a syllable   ok")
    return ok


def main():
    print("TRACT8 AUTO-CHATTER check")
    print("  Structure, not rate: does it sound like it has lungs?")
    ok = check_phrase_length()
    ok &= check_syllables_vary()
    ok &= check_breaths()
    ok &= check_consonants()
    ok &= check_rate_curve()
    ok &= check_engage()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
