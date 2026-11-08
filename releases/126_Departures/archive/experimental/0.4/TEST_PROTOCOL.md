# CV chord destination hardware test

On 8 November 2026, Adrian reported that the hardware tests pass for this image.
Individual durations and exact patch settings were not recorded. Keep stable
0.3.0 available; this image remains 0.4.0-experimental.

1. Listen with CV2 unpatched: the card should behave like stable 0.3.
2. Patch CV2. Sweep 0–5V without triggers; the harmony should stay put and Main's
   beat rate/activity should not follow the voltage.
3. Prepare each centre voltage from the README, then trigger Pulse In 2. Confirm
   all seven destinations, including C major. Check gentle shared-tone changes.
4. Hold one voltage near a boundary and repeat triggers: small noise should not
   cause rapid destination chatter. Recommitting the same chord should be quiet.
5. Select Z Up: external commits still work. Sweep CV1 and confirm transposition
   stays independent of destination selection.
6. Use stepped CV with a separate commit clock. Change voltage immediately after
   a trigger and confirm it affects the next commitment, not the previous edge.
7. Stop a patched beat clock: pad commits still work; no new beat-driven notes
   arrive until the clock restarts. Unplug it to resume internal timing.
8. Unplug CV2: automatic progression returns and Pulse In 2 advances normally.
   Tap Down with CV2 patched and confirm it still advances the normal sequence.
9. Stress maximum controls, rapid CV jumps, 5–10Hz commit/beat clocks, extreme
   transpose CV and hot external audio. Physical LED 6 should stay off.
10. Reset/power-cycle repeatedly, then optionally soak for 20–30 minutes.

Record patch, settings, selected voltage, timing LED state and any symptoms.
