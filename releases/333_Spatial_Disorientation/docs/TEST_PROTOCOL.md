# Beta3 tester protocol and feedback

© 2026 Adrian Vos (soveda), MIT. Hardware reports in this conversation informed
these checks. [Source/data/dependency attribution](../THIRD_PARTY_NOTICES.md).
Test **0.4.0-beta3**, two sources/64 taps, with its matching editor. Headphones and
a suitable modular-to-headphone output path are required for binaural listening.

1. **Install and startup.** Export old presets before beta Save. Load the beta UF2,
   reset and confirm normal boot is Orbits. Hold Down at reset: Main low = no LEDs /
   Orbits, middle = left LEDs / Mixer, high = right LEDs / Disorientation. Release
   confirms. Repeat reset/power cycle. Unplugged audio inputs should stay silent.
2. **Orbits.** Use contrasting mono sources A/B. Main positions them, X centre
   stops and either side reverses, Y/CV2 changes distance. Up linked, middle
   opposing, Down resets once. Check CV1, Pulse1 clock (saved default4 pulses/cycle)
   and Pulse2 reset. Listen at slow and maximum speeds.
3. **Mixer.** Select A with Up and B with Down; release retains B. Main position,
   X distance, Y level should affect only selected stored source. Pick up knobs
   after selection/editor changes. CV1/CV2 modulate both sources. Test A/B height
   below/level/above and intermediate positions, and save/restore source isolation.
4. **Disorientation.** Audition all3 movements from editor. Main depth/arc centre,
   X signed rate/centre stop, Y excursion; CV1/2, clock/reset. Hold Down to freeze
   phase, release to resume. CV/shape changes may still move frozen positions.
   First centre0°, vertical excursion0°, tilt0°. Then test vertical bob, and slope
   alone at40° uphill front/right/diagonal. Confirm both ears/sources and audible
   elevation without watching. Combine bob/tilt: heights must stay within±40°.
5. **Editor/presets.** Try the bottom Randomise elevation/Reset elevation tools
   in each displayed mode. They must stage without sending; unrelated settings
   stay unchanged. Apply, Read and Save separately. Orbits reset height0°/tilt0°,
   then change and Apply again: readback must match and sound return.
   Repeat20 changed Apply/Save cycles, waiting for replies, with no CV patched:
   sound/controls resume, no phantom CV or unresponsive Save. Ten identical saves
   should skip interruption/flash. Then check the normal editor lifecycle.
   USB-C data cable; Chrome/Edge SysEx permission; matching
   ports. Changes wait for Apply; Read discards staged changes. Test init without
   automatic Save, named Export/Import offline, reconnect retaining staged edits,
   mode mismatch rejection, old preset import, explicit Apply/Save/reset/Read.
   Verify all3 mode banks remain independent and Mixer placements/heights plus
   Disorientation movement/centre/depth/uphill/tilt survive. Malformed files should
   leave current edits/card unchanged. Disconnect/reconnect under audio load.
6. **8mu, separate boot.** Save desired height/slope with editor first; reset into
   host with8mu connected. Test fader pickup and mappings in README. Mixer A/B
   source selection, continuous fader8 height and panel handback; motion unused.
   Orbits/Disorientation B motion takeover, A reset/recentre, C hold freeze, fader8
   tilt sensitivity in Orbits / motion amount in Disorientation. Disorientation D tap cycles movement; long D hold shows timing
   and must not cycle on release. No host gesture autosaves. Unplug/reconnect and
   reset; check pickup and restoration. Report if using older hardware USB role.
   Orbits: D tap<500ms toggles normal/height pages. LEDs1–3 identify normal;
   LEDs6–8 height, for1second. Check held values and fresh pickup for faders1–3,
   shared4–8, C release returning prior speed and longD not switching. Connect
   with D held: release must stay normal. Height page faders1/2/3=height/tilt/uphill.
   Disconnect restores saved elevation and panel pickup; no flash changes.
   **Pitch/roll direction check:** begin with source A only, Main/X centred, no CV
   and approximately flat8mu; A recaptures neutral, B enables motion. In Orbits,
   back edge raised = front, right edge down = right, front edge raised = back,
   left edge down = left, relative to captured Main. Combined tilt moves between
   directions. Hold a pose30seconds: no accumulated yaw motion. Return nearflat:
   last direction holds. Small jitter should not spin; twisting at unchanged tilt
   should not add motion. Lower fader8 needs more tilt; zero returns anchor.
   A at a tilted pose recaptures neutral. Add slow X, then C hold/release to check
   automatic orbit combines with motion. Both Orbits pages retain this behaviour.
   In Disorientation, pitch changes bounded front/back Main depth, roll turns the
   whole path towards the lowered edge. Check all3 movements and steady poses.
   Fader8 scales depth/rotation; zero restores anchor/zero rotation. A recaptures,
   C freezes phase while tilt can still reshape placement, B restores panel pickup
   and eases rotation tozero. Test non-flat recapture and disconnect/reconnect.
   Height/slope remain configured in the editor, not through a Disorientation page.
7. **Timing/stability.** Both sources, high room/strength, moving intermediate
   heights, large tilt/bob, fast knobs/CV and available audio-rate CV. Reboot
   before each Fig8/Pendulum/Wander comparison so peaks are independent. Run
   each movement20–30min; check Orbits/Mixer under load too. Log callback/block
   peaks, warning flags and bottom-rightLED. Target<18µs callback/<1200µs block;
   deadline1333.3µs. Listen for clicks, silent gaps, distorted bursts or locked
   controls. In8mu boot holdD: eight steadyLEDs are a timing band, all flashing
   means a latched warning. Report warnings even if audio stays clean.

This is beta validation on additional setups, not a requirement to own every
piece of test equipment. Report which checks you could perform; missing checks
are not automatically passes. Keep fallback UF2/editor/old exports available.

## Feedback template

Copy this into an issue or message; no feedback is sent automatically.

```text
Version: 0.4.0-beta3 (UF2 SHA256 if checked)
Workshop Computer hardware revision / program card:
Headphones and monitoring path:
Audio sources / connected inputs:
USB role: editor / direct8mu / disconnected; computer/browser:
Mode and movement; linked/opposing:
Speed, room, levels, strength, heights, bob, uphill direction, tilt:
CV inputs/rate/depth; clock/reset:
Duration and callback/block peak (reboot before measurement?):
Bottom-rightLED / slowDSP / callback / queue warning:
Audio: slope/height audible? clicks/gaps/distortion? blind vs visual listening:
Editor Apply/Read/Save/preset/reset and migration result:
8mu mapping/pickup/motion/disconnect result (if tested):
Steps to reproduce a problem; attached exported preset if useful:
```

Submit manually through the [repository issues](https://github.com/soveda/Spatial-Disorientation/issues)
or to Adrian Vos(soveda). Please distinguish timing warnings from actual audio
faults and subjective spatial impressions. Archived firmware/editor formats may
need old exports to restore after downgrade; see README migration instructions.
