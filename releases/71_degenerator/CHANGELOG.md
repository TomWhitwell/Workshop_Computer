# Changelog

## v1.3 — Improved UI Feedback & Direct Knob Control

- **LED feedback redesign**:
  - LED 2 & 3: effect intensity brightness + subtle zone pulse (steady = zone 0, slow pulse = zone 1, fast pulse = zone 2). Dark when knob near zero.
  - LED 4: loop position fade (bright at start, dark at end) instead of wrap flash.
  - LED 5: clear mode indicator — slow pulse = MIX (YOLO), off = MIX (SLOT), fast flash = DEGRADE, steady on = RECORD / STORE_SLOT.
- **Removed knob reference tracking** — Big Knob now controls MIX level and DEGRADE rate directly from zero. No pickup/crossing behavior.
- **SLOT mode LED 5** changed from steady to off in MIX mode.

## v1.2 — YOLO Mode

- **YOLO / SLOT operating modes**: boot behavior split by Z switch position. Z not Down → YOLO mode (instant-on, silence buffer, no flash features). Z Down → SLOT mode (flash save/load, select slot at boot).
- **SELECT_SLOT**: LEDs changed from binary encoding to 1-of-4 (one LED per slot). Added 20ms settle debounce on Z release.
- **STORE_SLOT**: Z Up cancels to DEGRADE (not previous mode), with knob reference reset.
- **MIX knob reference fix**: effective amount correctly computed as `amt - mixKnobReference`.
- **Oxide shedding re-tuned**: faster damage accumulation, higher patch growth/probability, more aggressive dropouts.
- **RNG reseed**: triggered on loop wrap instead of periodic timer.
- **DEGRADE harmonic tracking**: `applyHarmonic` receives `readSample` instead of `audioIn` (processes buffer, not live input).
- **Boot beacon**: extended from ~4ms to ~1s.
- **Removed** `loadDefaultLoopFromFlash()` — no automatic flash load at boot.
- **Flash polling**: core 0 checks flash writes every 100ms (was 1s).
- **`next_norm_probe` reseed**: static RNG reseeded on first call using a unique magic constant.

## v1.1 — Web Manager, Flash Slots, Oxide Fix, Knob Reference

- **Web manager**: single-file HTML app (`web/degenerator_manager.html`) for uploading/downloading loops via WebUSB.
- **Flash storage**: 4 flash slots with header-based metadata. STORE_SLOT / SELECT_SLOT mode interface.
- **Multicore flash writes**: core 0 handles erase/program via atomic handoff flags with `multicore_lockout`.
- **Oxide shedding bug fix**: persistent patch positions and dropouts across wraps.
- **Knob reference tracking**: `mixKnobReference` / `degradeKnobReference` prevent accidental loud overdubs or rapid degradation on mode entry.
- **STORE_SLOT mode**: Z Down with Big Knob near zero enters store mode, slot selectable by Big Knob.

## v1.0 — Initial Release

- Core looper with RECORD / MIX / DEGRADE modes.
- 5.0-second buffer with μ-law companding at 48 kHz.
- X knob: Saturation, Filter Drift, Tape Hiss with crossfade zones.
- Y knob: Oxide Shedding, Bit Crush, Bit Rot with crossfade zones.
- Bypass thresholds for Big Knob (<50) and X/Y knobs.
- CV inputs for Big Knob and Y knob modulation.
- Pulse I/O for external sync and recording trigger.
- CV outputs for loop position and envelope follower.
- LED output level, knob position, and mode indication.
