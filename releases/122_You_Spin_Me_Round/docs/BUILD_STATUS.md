# Version 0.1.0-rc1 validation

Built 2026-10-07 on macOS with Pico SDK 2.3.0 and ARM GCC 15.2.1.
Release compilation succeeded with configured warnings enabled. Firmware uses
23,212 bytes flash and 28,700 bytes main RAM, plus a 2,048-byte core-0 stack.

The user reports alpha2 tests pass on hardware. Musical feedback: little
Inside/Outside difference and little rotary movement below 9 o'clock on X;
audio remains audible. This is not release approval or a measured ISR duration.
The author subsequently reported alpha3 tests all pass and the sounds work nicely.
RC1 packages exactly those alpha3 firmware bytes, with no DSP/control changes.
No measured complete-ISR maximum has been supplied; the user has approved the release-folder copy.

Host tests pass with address and undefined-behaviour sanitizers: boot default,
selector hysteresis, momentary brake and overlapping gate priorities, CV bounds,
motor inertia, monotonic X mapping/endpoints, low-X stereo response, perspective
difference, stationary equal-channel output at X=0 in both modes, DC decay,
and 500,000 full-range random transients with abrupt parameter changes.
Ordinary 900-count sine input did not clip in the tested modes.

For the 440 Hz/700-count fast-range reference in tools/measure_response.cpp:
at X=512, stereo-difference RMS/output RMS rises from about 0.021 in alpha2 to
0.148 in alpha3. Output/input RMS changes only from about 0.585 to 0.567.
These single-signal measurements quantify response, not perceived sound quality.

The WAV previews have been regenerated through the alpha3 DSP, using identical
synthetic source and settings. They are not recordings from the hardware.
Metadata validation passes with no errors/warnings. UF2 headers, block numbering,
RP2040 family ID, payload size and 2 MB address range verified.

RC1 firmware SHA-256 (identical to tested alpha3):

`678ff40523d110a466d91bd1bf5e8671b69ac3ca8562f02f22cdf838826ec249`

Earlier firmware is retained for comparison; RC1 is the current candidate.

RC checks: rebuild succeeds, host sanitizer tests pass, strict upstream metadata
validation passes with no errors/warnings, and RC1 is byte-identical to both the
alpha3 UF2 and the current build output. Binary packaging changes no sound.

The exported `122_You_Spin_Me_Round` folder also builds successfully from a
separate build directory and passes strict metadata validation independently.
The ZIP includes only RC1 firmware, source, dependencies/notices, documentation,
tests and preview tools/audio; earlier alpha firmware stays in the development repo.
