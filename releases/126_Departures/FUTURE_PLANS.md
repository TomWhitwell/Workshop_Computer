# Departures — future plans

## Completed: woodier ensemble and startup sound selection

Requested and implemented 8 November 2026; promoted to stable **0.5.0** after
Adrian reported hardware tests passed.

- Hold **Z Down while booting or resetting** to open a silent sound selector.
- Main left selects **Bells** (physical LEDs 1+3); right selects **Wood Chimes**
  (LEDs 2+4). Release Z to confirm and start. Main then returns to Activity.
- Normal boot defaults to Bells. Choice lasts until reset and is not saved.
- Wood Chimes adds shorter rounded resonances and warmer pads, with the same
  score, phrasing, CVs, bass, effects and accepted melody mix.
- Runtime Down still advances a chord. The boot gesture is consumed separately.
- Previous stable 0.4.1 is preserved for comparison in the archive.

See [README.md](README.md) for operating instructions and
[CHANGELOG.md](CHANGELOG.md) for release history. No further sound-palette
changes are currently scheduled.
