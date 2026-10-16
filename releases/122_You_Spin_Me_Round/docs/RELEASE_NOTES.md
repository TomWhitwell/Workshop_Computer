# You spin me round 0.1.0-rc1

Release candidate by Adrian Vos, 2026-10-07. MIT; see ../LICENSE.

Card number: **122**, referencing the Leslie 122 speaker. Release folder:
`releases/122_You_Spin_Me_Round/`. Number 122 was absent from upstream main's
releases listing on 2026-10-07; no open upstream PR mentioning 122 was found.
This local choice is not an upstream reservation. Recheck before submitting.
Source for the speaker connection:
https://hammondorganco.com/products/leslie-speakers/122-147-981-147a

## Firmware

RC1 is byte-identical to author-tested 0.1.0-alpha3. No DSP, motor, control,
LED or transport behaviour changed for the candidate. The author reports all
tests pass and the sound works nicely. RC packaging updates the version labels,
author attribution, README and structured metadata, following the presentation
of Adrian Vos's Bib and Dub Warning cards.

SHA-256: `678ff40523d110a466d91bd1bf5e8671b69ac3ca8562f02f22cdf838826ec249`.

## Included behaviour

- Independent bass drum and treble horn, slow/fast ranges and unequal inertia.
- Per-sample integer DSP using ComputerCard 0.4.0 at 48 kHz and a 144 MHz CPU.
- Responsive low-X movement, soft cabinet drive and stereo output.
- Default Outside and a distinct Inside mode selected by holding Down at boot.
- Momentary Down brake with independent held-gate brake and speed override.
- Phase/status LEDs and a latched callback timing warning.

## Validation

Author hardware/listening tests pass for the identical alpha3 firmware. Release
build, host address/undefined-behaviour sanitizer tests, strict metadata validation,
and UF2 format/address checks pass. Callback warning is retained; a measured
complete-ISR maximum has not been recorded. There is no claim of a component-level
Leslie 122 model. The preview WAVs are host renders, not hardware recordings.

## Handoff

The user approved copying this release candidate to Workshop_Computer/releases.
The standalone repository remains the development source. The requested reminder
to start Spatial Disorientation has been delivered. No binaural development or
remote publication is implied by the copy.
