# Experimental changelog

## 0.4.0-experimental — 8 November 2026

- Replace CV2 Activity modulation with seven D Dorian chord destinations over
  approximately 0–5V, including C major beyond the original fixed sequence.
- Prepare with CV2; commit only on Pulse In 2. Latch the value at the edge.
- Pause automatic chord changes while CV2 is connected. Restore the original
  progression and Pulse In 2 next-chord fallback when it is unplugged.
- Preserve CV1 transposition and the normal Down-switch advance action.
- Retain stable 0.3's sound, bell phrasing, rests, tide and shared-tone transitions.
- Add quantizer hysteresis, quiet same-chord recommits and destination tests.
