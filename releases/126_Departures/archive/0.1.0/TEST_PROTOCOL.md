# Hardware test protocol

For Departures 0.1.0. Use stereo Audio Outs 1/2, start the knobs near noon and
reset with Z in Middle. Watch **physical LED 6, bottom right** (firmware index 5).
It latches if the callback reaches 18 microseconds; reset clears it.

| Test | Procedure | Pass condition |
| --- | --- | --- |
| Maximum workload | Set Main, X and Y fully clockwise. Apply a 5–10Hz beat clock to Pulse In 1 and periodically trigger Pulse In 2. Suggested duration: five minutes. | Stable audio and responsive controls; LED 6 remains off. |
| Control changes | Keep the dense patch running. Sweep all knobs and alternate Middle, Up and taps Down. | No processing dropouts, frozen controls or uncontrolled output jumps. |
| CV extremes | Sweep each CV input through approximately −5V to +5V. | Transposition remains limited to ±1 octave, activity stays bounded and controls remain responsive. |
| External audio / feedback | Raise Y and send sustained tones, bass and percussion into both audio inputs, then remove the source. | No runaway feedback or persistent harsh oscillation; the external sound's tails decay beneath the continuing ensemble. |
| Clock stop / restart | Stop the external clock without disconnecting it, restart, then unplug it. Repeat. | New melody/bass beats stop while connected; pad breaths and tails continue. Restart follows the external clock; unplugging resumes the internal clock. |
| Harmony | Hold Up for a minute, tap Down, and separately trigger Pulse In 2 while Up is selected. | Up holds automatic chord changes and phrase mutations. Down advances once per entry. External advance works during Hold. |
| Reset / power | Reset repeatedly, including after a dense patch, then power-cycle several times. | Every boot reaches normal playback and all controls remain responsive. |
| Extended soak | Optionally leave all knobs at maximum for 20–30 minutes. | Stable playback and no latched timing warning. |

## Recorded result

On 7 November 2026, Adrian reported **all suggested tests pass** for the
0.1.0-prototype image. Release 0.1.0 uses the identical binary. No individual
test durations, exact patch settings or instrumented measurements were supplied;
the suggested durations above are instructions, not a log of measured run times.

For a failure report, note the patch and settings, whether LED 6 lit, whether
the controls still responded, and whether reset restored normal playback.
A short recording can help identify a synthesis artifact versus a dropout.

For automated host tests and the ARM timing estimate, see
[BUILD_REPORT.md](BUILD_REPORT.md) and the README build instructions.
