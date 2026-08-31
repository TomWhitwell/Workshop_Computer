#!/usr/bin/env python3
"""Can the card be silenced by a jack-detection fault? Regression test.

THE BUG THIS EXISTS FOR. TRACT8's first hardware run produced no sound at
all except a click from the plosive key. Plosives are summed AFTER the
filter bank, so their working proved the ISR, the DAC and the output path
were all fine, and localised the fault to whatever feeds the bank.

Two lines were responsible, and both had the same shape:

    ext_gate = Connected(Pulse2) ? PulseIn2() : true;
    use_ext  = Connected(Audio1);

ComputerCard forces a DISCONNECTED input's value to zero. So if the
normalisation probe wrongly reports a jack as connected:

  - Pulse2 misdetected -> PulseIn2() reads low forever -> both excitation
    levels are gated to zero -> silence.
  - Audio1 misdetected -> the internal excitation is replaced by a constant
    zero -> the bank is fed silence -> silence.

Either one alone mutes the card, and neither is visible from the panel.

Every other test in this directory checks DSP: filter geometry, aliasing,
vowel distinctness, MIDI decoding. All of them passed while the card was
silent, because none of them modelled the CONTROL FLOW that decides what
reaches the DSP in the first place. That is the gap this file closes.

The rule it enforces: no single jack-detection fault may reduce the card to
silence. Failures must fall back to making sound, because a droning card is
diagnosable from the front panel and a mute one is not.

Check 5 was added when the momentary switch stopped being a plosive key and
became a gate. Anything that joins the gate path has to be checked BOTH
ways: that it can rescue a card whose gate is stuck low, and that it cannot
latch gate_seen_ - a switch that latched would mute the card the moment it
was released with nothing patched, which is the original bug by another
route.

Run: python tools/silence_check.py
"""

import sys

# Transcribed from main.cpp.
EXT_GATE_LEVEL = 30
EXT_HOLD_SAMPLES = 4800


class Panel:
    """Model of the excitation routing in VoderCard::ProcessSample()."""

    def __init__(self):
        self.ext_hold = 0
        self.gate_seen = False

    def step(self, *, pulse2_high, audio_in, midi_voiced=0, midi_noise=0,
             switch_down=False):
        """Returns (voiced_level, noise_level, use_ext, excitation_reaches_bank)."""
        # --- gate path (current implementation) ---
        if pulse2_high:
            self.gate_seen = True
        # gate_seen_ latches on the JACK only. The switch must not make the
        # card think a cable has appeared - if it did, releasing the switch
        # would leave the gate closed and the card silent with nothing
        # patched, which is the exact failure mode the latch exists to
        # prevent.
        jack_gate = pulse2_high if self.gate_seen else True
        ext_gate = jack_gate or switch_down

        any_midi = midi_voiced or midi_noise
        if any_midi:
            voiced = 32767 if midi_voiced else 0
            noise = 32767 if midi_noise else 0
        else:
            voiced = 32767 if ext_gate else 0
            noise = 32767 if ext_gate else 0

        # --- external input path (current implementation) ---
        if audio_in > EXT_GATE_LEVEL or audio_in < -EXT_GATE_LEVEL:
            self.ext_hold = EXT_HOLD_SAMPLES
        elif self.ext_hold > 0:
            self.ext_hold -= 1
        use_ext = self.ext_hold > 0

        # Does anything non-zero actually reach the filter bank?
        if use_ext:
            reaches = audio_in != 0
        else:
            reaches = (voiced > 0) or (noise > 0)

        return voiced, noise, use_ext, reaches


def scenario(name, *, pulse2_high, audio_in, midi_v=0, midi_n=0,
             steps=2, expect_sound=True, switch_down=False):
    p = Panel()
    reaches = False
    for _ in range(steps):
        _, _, _, reaches = p.step(pulse2_high=pulse2_high, audio_in=audio_in,
                                  midi_voiced=midi_v, midi_noise=midi_n,
                                  switch_down=switch_down)
    ok = (reaches == expect_sound)
    verdict = "sound" if reaches else "SILENT"
    want = "sound" if expect_sound else "silent"
    print(f"   {name:52} {verdict:7} (want {want:6}) "
          f"{'ok' if ok else '<-- FAIL'}")
    return ok


def check_nothing_patched():
    print("\n1. Nothing patched - the out-of-the-box case")
    ok = True
    # This is the exact configuration that was silent on hardware. With
    # nothing plugged in, ComputerCard reports the inputs as zero whether
    # or not the probe has settled, so the card must still make sound.
    ok &= scenario("nothing patched, probe correct",
                   pulse2_high=False, audio_in=0)
    ok &= scenario("nothing patched, Pulse2 MISdetected as connected",
                   pulse2_high=False, audio_in=0)
    ok &= scenario("nothing patched, Audio1 MISdetected as connected",
                   pulse2_high=False, audio_in=0)
    ok &= scenario("nothing patched, BOTH misdetected",
                   pulse2_high=False, audio_in=0)
    print("   (all four are the same call now: the fix removed the")
    print("    dependency on Connected() entirely, so a misdetected jack")
    print("    can no longer change the outcome)")
    return ok


def check_real_patches():
    print("\n2. Real patches must still work")
    ok = True
    ok &= scenario("gate patched and high",
                   pulse2_high=True, audio_in=0)
    ok &= scenario("audio patched with real signal",
                   pulse2_high=False, audio_in=5000)
    ok &= scenario("8mu button gating voiced",
                   pulse2_high=False, audio_in=0, midi_v=1)
    ok &= scenario("8mu button gating noise",
                   pulse2_high=False, audio_in=0, midi_n=1)
    return ok


def check_deliberate_silence():
    """Silence a player ASKED for must still be possible."""
    print("\n3. Deliberate silence must remain reachable")
    ok = True
    # A patched gate that has gone low is a player choosing silence.
    p = Panel()
    p.step(pulse2_high=True, audio_in=0)     # gate seen high once
    _, _, _, reaches = p.step(pulse2_high=False, audio_in=0)
    good = not reaches
    print(f"   {'patched gate, seen high then low':52} "
          f"{'SILENT' if not reaches else 'sound':7} (want silent) "
          f"{'ok' if good else '<-- FAIL'}")
    ok &= good
    return ok


def check_ext_hold():
    print("\n4. External-input hold does not chatter on zero crossings")
    p = Panel()
    # A signal that crosses zero must not drop out of external mode.
    p.step(pulse2_high=False, audio_in=5000)
    held = True
    for _ in range(1000):                    # 1000 samples at zero
        _, _, use_ext, _ = p.step(pulse2_high=False, audio_in=0)
        if not use_ext:
            held = False
            break
    print(f"   external mode held across 1000 zero samples          "
          f"{'ok' if held else '<-- CHATTERS'}")

    # But it must eventually release when the signal really stops.
    p2 = Panel()
    p2.step(pulse2_high=False, audio_in=5000)
    for _ in range(EXT_HOLD_SAMPLES + 10):
        _, _, use_ext, _ = p2.step(pulse2_high=False, audio_in=0)
    released = not use_ext
    print(f"   external mode released after {EXT_HOLD_SAMPLES} silent samples  "
          f"{'ok' if released else '<-- STUCK'}")
    return held and released


def check_switch_gate():
    """The switch became a GATE rather than a plosive key.

    It used to fire one click on the press and then do nothing for the rest
    of the hold - the panel's copy of Pulse In 1. It is now the panel's copy
    of Pulse In 2: held down it opens the voice for as long as it is held.

    That puts it in the gate path, which is what this file guards, so it
    gets two checks pulling in opposite directions.
    """
    print("\n5. The switch is a gate, and joining the gate path is safe")
    ok = True

    # It must SOUND while held - the whole point of the change.
    ok &= scenario("switch held, nothing else patched",
                   pulse2_high=False, audio_in=0, switch_down=True)

    # And it must RESCUE a gate stuck low. This is the direction that
    # matters for this file: a real cable in Pulse In 2 sitting at 0 V
    # legitimately silences the card, and the switch is then the one
    # control on the panel that can prove the card is still alive.
    p = Panel()
    p.step(pulse2_high=True, audio_in=0)          # a real jack rises once
    _, _, _, silent = p.step(pulse2_high=False, audio_in=0)
    _, _, _, rescued = p.step(pulse2_high=False, audio_in=0, switch_down=True)
    good = (not silent) and rescued
    print(f"   {'gate latched then held low -> switch still sounds':52} "
          f"{'ok' if good else '<-- CANNOT RESCUE'}")
    ok &= good

    # The other direction, and the trap: the switch must NOT latch
    # gate_seen_. If it did, pressing and releasing the switch with nothing
    # patched would leave jack_gate reading PulseIn2() forever - which is
    # low - and the card would be permanently mute afterwards. That is the
    # v1.0.0 silence bug reintroduced by the back door.
    p2 = Panel()
    for _ in range(4):
        p2.step(pulse2_high=False, audio_in=0, switch_down=True)
    _, _, _, after = p2.step(pulse2_high=False, audio_in=0, switch_down=False)
    print(f"   {'switch released, nothing patched -> still drones':52} "
          f"{'ok' if after else '<-- MUTED BY THE SWITCH'}")
    ok &= after
    return ok


def main():
    print("TRACT8 silence regression check")
    print("  Rule: no single jack-detection fault may mute the card.")
    ok = check_nothing_patched()
    ok &= check_real_patches()
    ok &= check_deliberate_silence()
    ok &= check_ext_hold()
    ok &= check_switch_gate()
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
