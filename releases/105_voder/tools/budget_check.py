#!/usr/bin/env python3
"""Cycle budget estimate for TRACT8's audio ISR.

READ THIS BEFORE TRUSTING ANY NUMBER BELOW.

Every figure in this file is a PREDICTION, derived from instruction counts
and published cycle timings for the Cortex-M0+. It is not a measurement,
and on this hardware that distinction has bitten before: the sibling card
WorkshopSpectral modelled its DSP load at 51% and measured 231% on real
silicon - a factor of four and a half out - because the model did not
account for flash-resident helper calls and XIP cache contention.

The authority on this card's cost is CV Out 2, which reports measured
microseconds per sample scaled against the 20.83 us budget. When TRACT8 has
run on hardware, replace the summary line at the bottom of this file with
the reading from that pin, and treat any disagreement as this model being
wrong rather than the hardware.

What this file IS good for: showing where the cost sits, so that if the
measured figure comes back too high there is a ranked list of things to
attack. It is a map, not a scale.

Run: python tools/budget_check.py
"""

import sys

FS = 48000
CLOCK_MHZ = 192
BANDS = 8

# Cortex-M0+ timings, from the ARMv6-M architecture reference. Where a
# range is given the pessimistic end is used.
CYC = {
    'mul32':      1,    # MULS, 32x32 -> low 32
    'add':        1,
    'shift':      1,
    'load':       2,
    'store':      2,
    'branch':     3,    # taken
    'cmp':        1,
    # __aeabi_lmul: 64x64 -> 64 via four 32x32 partial products plus the
    # shifts and adds to combine them. Measured elsewhere at ~40 cycles
    # when resident in RAM; substantially worse from flash, which is the
    # entire reason for PICO_INT64_OPS_IN_RAM=1 in CMakeLists.txt.
    'lmul_ram':  40,
    'lmul_flash': 90,
}


def biquad_cost(in_ram=True):
    """One 2-pole section: 3 64-bit products, plus state shuffling."""
    lmul = CYC['lmul_ram'] if in_ram else CYC['lmul_flash']
    c = 0
    c += 3 * lmul              # b0*(x-x2), a1*y1, a2*y2
    c += 2 * CYC['add']        # combine the three products
    c += CYC['shift']          # >>15
    c += 4 * CYC['load']       # x1 x2 y1 y2
    c += 4 * CYC['store']      # shuffle them back
    c += lmul                  # y * band_gain
    c += CYC['shift']
    c += 2 * CYC['add']        # accumulate sum and energy
    c += CYC['cmp'] + CYC['branch']   # loop overhead
    return c


def excitation_cost():
    c = 0
    c += CYC['lmul_ram']                      # f0 * kIncPerMilliHz_Q16
    c += 4 * CYC['add'] + 3 * CYC['shift']    # phase, saw shaping
    c += 2 * (CYC['cmp'] + CYC['branch'])     # f0 clamping
    # polyBLEP fires on roughly one sample in (fs/f0). At 110 Hz that is
    # 1 in 436, so its cost is negligible amortised - but it includes a
    # 64-bit divide, which is NOT cheap, so it is counted at full price
    # divided by the period.
    c += (CYC['lmul_ram'] * 3 + 60) // 436
    c += 3 * CYC['shift'] + 3 * CYC['add']    # xorshift32
    c += 4 * (CYC['cmp'] + CYC['add'])        # gate ramps
    c += 3 * CYC['lmul_ram'] + 2 * CYC['shift']  # source mix + gates
    return c


# Vowel morph recompute interval, from main.cpp. The morph is amortised
# over this many samples.
MORPH_DIV = 384


def panel_cost():
    # One knob read per sample, plus the vowel morph. The morph is 8 bands
    # x (lerp + tilt), each with a 64-bit product, and at full rate it was
    # the second-largest cost in the ISR - 700 cycles, 28% of budget, to
    # recompute a value that only changes when a hand moves a knob.
    # main.cpp now runs it once every MORPH_DIV samples (125 Hz), so it is
    # amortised here the same way.
    c = CYC['load'] * 2
    morph = BANDS * (2 * CYC['lmul_ram'] + 4 * CYC['add'] + 3 * CYC['shift'])
    c += morph // MORPH_DIV
    return c


def overhead_cost():
    c = 0
    c += 2 * CYC['load']                 # time_us_32 x2
    c += 10 * CYC['store']               # AudioOut, CVOut, PulseOut
    c += 20 * CYC['add']                 # assorted bookkeeping
    c += CYC['lmul_ram']                 # load scaling
    return c


def report(in_ram):
    bank = BANDS * biquad_cost(in_ram)
    exc = excitation_cost()
    pan = panel_cost()
    ovh = overhead_cost()
    total = bank + exc + pan + ovh

    budget_cyc = CLOCK_MHZ * 1e6 / FS
    load = total / budget_cyc * 100

    label = "RAM (PICO_INT64_OPS_IN_RAM=1)" if in_ram else "FLASH (the bug)"
    print(f"\n  __aeabi_lmul in {label}")
    print(f"    filter bank  ({BANDS} biquads)   {bank:6d} cycles  "
          f"{bank/total*100:5.1f}%")
    print(f"    panel + morph (1/{MORPH_DIV} rate)      {pan:6d} cycles  "
          f"{pan/total*100:5.1f}%")
    print(f"    excitation                    {exc:6d} cycles  "
          f"{exc/total*100:5.1f}%")
    print(f"    IO and overhead               {ovh:6d} cycles  "
          f"{ovh/total*100:5.1f}%")
    print(f"    {'':30}{'-'*6}")
    print(f"    total                         {total:6d} cycles")
    print(f"    budget at {CLOCK_MHZ} MHz / {FS} Hz   {budget_cyc:6.0f} cycles")
    print(f"    PREDICTED LOAD                {load:6.1f}%")
    return load


def main():
    print("TRACT8 cycle budget - PREDICTION ONLY, see the docstring")
    print(f"  {BANDS} bands, {CLOCK_MHZ} MHz, {FS} Hz, "
          f"{1e6/FS:.2f} us per sample")

    load_ram = report(True)
    load_flash = report(False)

    print(f"\n  Keeping the 64-bit helper in RAM is worth "
          f"{load_flash - load_ram:.0f} points of load.")
    print("  That is what PICO_INT64_OPS_IN_RAM=1 buys, and why it is not")
    print("  optional. Verify it took effect with:")
    print("    arm-none-eabi-nm build/tract8.elf | grep aeabi_lmul")
    print("  and check the address is in RAM (0x2000....), not flash")
    print("  (0x1000....).")

    print("\n  MEASURED ON HARDWARE, and this model was wrong.")
    print()
    print("    int64 filter bank : predicted 44.2%, MEASURED ~100%")
    print("    int32 filter bank : MEASURED 60% typical, 70% peak")
    print()
    print("  The model does not know that (int64_t)a * b is a CALL to")
    print("  __aeabi_lmul on an M0+ rather than an instruction, nor what")
    print("  that costs in context. It was out by more than a factor of")
    print("  two in the direction that matters.")
    print()
    print("  CV Out 2 carried this measurement and now carries the vowel")
    print("  instead - the number stopped changing, and the jack was")
    print("  worth more than the meter. To measure again, put the")
    print("  timing back around the ISR body; see docs/BRINGUP.md.")

    # This script cannot fail: it measures nothing. It exists to inform,
    # and to be replaced by a hardware reading.
    print("\n  (no PASS/FAIL - this script measures nothing)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
