# Fixed-point Computer DSP

> The player has approved the timbral changes and confirmed the USB connection works on the Computer. Timing, noise and calibration still need measurement.

`mtm::Voice`, implemented in `voice.cpp` with a declaration in `voice.hpp`, runs at 24 kHz. `setParameters(Parameters)` accepts direct 0…32767 Q15 String,
Jawari, Swell, Sympathy, plus bipolar −32767…32767 Pick; `setPitchQ16()` accepts Q16 octaves relative to C4 and clamps
the corresponding period to 40–2000 Hz. `trigger()` adds a finite pluck while
preserving the string histories and all sympathetic states. `damp()` briefly
increases losses through a smoothed control. `next(externalString, externalHalo)`
returns `{mix, dry, wet}`, each in signed Q15 output units.

The hot path uses 32-bit integer products. `setPitchQ16()` and a changed Swell target use 64-bit control-rate division.
Calls with unchanged pitch and parameters skip coefficient work.
Inputs and internal coordinates are bounded before multiplication. Per-sample
allocation, trigonometry, floating point and blocking operations are absent.

## Playing string

Two 1024-sample displacement histories represent normal and weak transverse
motion. Third-order fractional-delay interpolation, frequency-dependent loss
and an allpass provide the propagation approximation. Positive bridge contact
shortens the effective delay and creates a small asymmetric loss. The bridge
also radiates a velocity-like signal that excites the sympathetic bank.

The pluck combines a one-period combed-noise burst and a short wire-release
profile. Pick is latched at the next pluck and changes the excitation's release
filtering. String adjusts the loop brightness coefficient (.82… .995); Jawari
adjusts asymmetric contact, moving contact delay and transverse coupling. Swell
changes the nominal loop decay (2…5 s) and secondary amplitude rise. Sympathy
scales the bank's audible mix/halo output while its states continue vibrating.
The sympathetic tails do not share the melody note envelope.

Rapid retriggers preserve the melody's existing secondary rise. A decaying
recent-stroke measure bounds new string-to-bank coupling during a phrase while
all stored sympathetic states ring freely; isolated notes retain the prior
sound. This is a musical balance approximation, not a measured bridge law.
See [the phrase experiment](../PHRASE_BALANCE.md) for the frozen baseline,
three measured candidates, real-reference limitations and pending audition.

Tables compensate the ideal fundamental phase delay of the loss/allpass path
when String changes. Allpass coefficient is fixed at .3. Nonlinear contact,
interpolation and fixed-point loss
still cause tuning deviations; the measured pitch report is authoritative.
These controls are musical approximations, not measured bridge/material laws.

## Sympathetic bank

The dominant modal calculation runs at **12 kHz**, with halfband FIR conversion
from/to the 24 kHz voice. Sixty partial slots represent twelve editable strings,
defaulting to the documented D-tonic Yaman adaptation. This coupling experiment
uses harmonics **1, 3, 5, 7, 2**, replacing only the former ninth-harmonic slot.
The second mode allows octave-related excitation without a subharmonic generator.
See [the experiment](../COUPLING_EXPERIMENT.md). Modes at or above the 6 kHz bank
Nyquist frequency are disabled.
Short halfband filters mitigate aliasing but do not make the nonlinear engine
alias-free or preserve the desktop engine's complete bandwidth.

Each partial contains two damped second-order resonators with the same target
frequency and different decay times. Their difference approximates a causal
response proportional to

`sin(w*t) * (exp(-t/tauSlow) - exp(-t/tauFast))`.

For each branch, stored coordinates have eight extra fractional bits and follow

`y[n] = a*y[n-1] - y[n-2] + loss*y[n-2] + excitation[n]`,

where `a=2*r*cos(w)` is Q17 and `loss=1-r*r` is Q24. Storing the small loss
separately prevents a long decay rounding to an undamped pole. Split 32-bit
products retain the wide state precision, following NOTES.md, without 64-bit
hot-path multiplication. States are bounded below 2^24; coefficient/loss bounds
keep intermediate products within int32. Native tests compare split arithmetic
with full-width boundary references, exercise corners, and check natural decay.

This replaces the previous per-coordinate fraction-carry magic-circle scheme,
which instruction profiling showed could not meet the 144 MHz budget. It preserves
all sixty independently blooming partials, without per-note resets or a shared
artificial bank envelope. Radiation phase and rounding change, so it is not a
bit-exact or listening-approved substitute for the previous bank. Melody envelope
and DC helpers retain their existing fractional/magnitude accumulation.

No free-running source is added: common string/input vibration excites the bank.
See UPSTREAM_REVIEW.md and reports/arm-profile.json for measured instruction counts
and their limits. Physical deadline/underrun measurements remain necessary.

## Reference relationship and checks

The design derives from the accepted sitar project core at `18bc6ab`. Q15
precision, 24/12 kHz processing, the dual-decay modal law and omitted desktop
onset/EQ layers make this an adaptation. The direct control mapping is independent of the Rack plugin. Defaults are String .8, Jawari/Swell .65,
Sympathy 1 and Pick 0; physical knobs select direct values at startup.

UBSan tests cover control corners, Pick contrast, Sympathy mute, retriggers,
repeatability, damping, silence, external excitation and natural 24-second decay.
See parent VALIDATION.md and generated reports for the current measured results.

Coefficient arrays are reproducibly generated by `tools/generate_tables.py`.
The firmware controller/filter paths and executable test reports live in the
parent port directory.

## Exact rate-conversion filter optimisation

`Fir` uses the original 17 coefficients, pairing symmetric taps and skipping zero
taps. Its mirrored 34-word history supplies contiguous samples with one ring-head
update. A compile-time check enforces symmetry, the zero-tap pattern and an
absolute coefficient sum no greater than 65536. With inputs clipped to ±32767,
all products and partial sums fit int32. Output rounding and latency match the
original direct FIR exactly. The extra history costs 68 bytes per filter.

An independent int64 direct-convolution reference checks a million inputs under
UBSan. Complete rendered WAV hashes also match the prior firmware DSP. See
`reports/fir-optimisation.json` for the comparison and instruction-count limits.

Substantial implementations are in `fixed.cpp`, `filter.cpp`, `modal.cpp` and
`voice.cpp`. Only small numeric primitives, constexpr definitions and read-only
accessors remain inline. `tables.hpp` is generated and included only where its
coefficients are used; do not edit or format it manually. See SOURCE_REVIEW.md in
the parent folder for the behaviour-preserving refactor and performance results.

### Duration parameters

`Parameters::melodyDecayPercent` and `sympatheticDecayPercent` are independent
integer percentages, clamped to 25–400 with boot defaults of 100. The first
multiplies the existing Swell-dependent melody loop time (nominally 2–5 s at
100%); Swell still controls the secondary rise. Losses and the bridge also affect
actual audible decay, so these percentages are musical scale factors, not
calibrated seconds. The second scales both decay constants of the independently
blooming bank. Changing melody decay can also change halo energy because the
melody excites the bank; changing halo duration does not change the melody.

The worker smooths changes, distributes bank coefficient updates across modes,
and retains all histories. Feedback is adjusted with pole loss to approximately
preserve each fixed frequency; integer quantisation remains. DAMP remains separate.
At 100%, the pre-existing control/audio trace is bit-identical. Values can be set
through `Parameters` in `Packet.settings.params` before queueing; the USB editor exposes both values; they reset to 100% on reboot. The editor populates both fields at the core0
packet boundary; stable text IDs are `melody_decay_percent` and
`sympathetic_decay_percent`.
