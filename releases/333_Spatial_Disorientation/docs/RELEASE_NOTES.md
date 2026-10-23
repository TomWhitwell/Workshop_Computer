# 0.3.0-beta2 — changes from beta1

©2026 Adrian Vos(soveda), MIT. Existing measurement/dependency notices and terms
remain in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and the firmware bundle.

Beta2 promotes the exact user-tested alpha3 firmware. It adds:

- Twin Orbits height, uphill direction and tilt, saved independently from Disorientation.
- Orbits8mu normal/height pages selected by a short D release. Faders1–3 switch
  between speed/distance/separation and height/tilt/uphill, with separate pickup and
  retained values. Faders4–8 retain their mapping; D hold retains timing diagnostics.
- Brief8mu page indication: LEDs1–3 normal, LEDs6–8 height, then pickup feedback.
- Bottom editor Randomise elevation and Reset elevation tools, staged until Apply.
- Orderly sampling pause/restart around changed flash saves, addressing the
  repeated-save failure; unchanged saves skip flash/mute.
- Orbits height/tilt readback checking after Apply/init, with a clear mismatch error.

CPU192MHz,48kHz,two64tap sources, all three modes and existing physical controls
remain. Mixer/Disorientation mappings stay as before. Runtime8mu controls do not
write flash. The user reports alpha3 all tests pass, effective page indication and
audible elevation with randomisation. No new alpha3 timing/duration values supplied.

Use the matching beta2 editor. Formats are SysEx7/JSON7/flash8, compatible with
0.3.0alpha1–3. Beta1 presets/flash migrate, with new Orbits height/slope initially
level/zero tilt. Export old presets before Save: beta1 cannot read flash8/JSON7.
Archive/beta1 preserves its UF2/editor/source/docs and original testerZIP.

Share the current tester ZIP with [the protocol/feedback template](TEST_PROTOCOL.md).
Additional testers should report board/headphones, separate editor/8mu sessions,
exact settings/CV, duration, peaks and any sound/control/save faults.
