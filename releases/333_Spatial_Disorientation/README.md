# Spatial Disorientation — card 333

**Beta · 0.1.0-alpha14 · Adrian Vos (soveda)**

Two independent mono sources become a binaural stereo mix: orbit around the
listener, place each source separately, or choose Fig8, Pendulum and Wander
movement. Musical inspiration: [Neuzeit Instruments Quasar](https://www.neuzeit-instruments.com/Quasar).
This is an independent implementation, with no Quasar code, graphics or data.

Use headphones through a suitable monitoring path. This version renders horizontal
direction and distance; elevation is a [future refinement](docs/IMPLEMENTATION_PLAN.md#future-refinement-elevation-rendering-staged-2026-10-10).
**333** is a mnemonic for three spatial axes, not a claim that height is implemented.

Original code/documentation © 2026 Adrian Vos (soveda), [MIT](LICENSE). Dependencies
and measurement data retain their own terms; see [attribution](THIRD_PARTY_NOTICES.md).
Keep [uf2/NOTICE.txt](uf2/NOTICE.txt) with redistributed firmware. This follows the
notice packaging of Adrian Vos's card 122 and the component-notice standard of
card 369; it does not adopt card 369's application freeware license.

## Load and start

Flash [the tested alpha14 UF2](uf2/Spatial_Disorientation_0.1.0-alpha14-movements.uf2)
using the Workshop Computer BOOTSEL procedure. The binary is unchanged from the
user-tested development build; [checksum](uf2/SHA256SUMS.txt).
Patch mono Audio 1, optionally a different source to Audio 2. Audio Out 1/2 are
the left/right binaural mix. Unplugged inputs are silent. CV outputs are nominal
zero and pulse outputs low.

Normal boot always selects **Twin Orbits**. To select another function, hold
**Down during reset/startup**, choose with Main, then release:

| Main third | Function | Startup LEDs |
|---|---|---|
| Lower | Twin Orbits | All off |
| Middle | Spatial Mixer | Left column |
| Upper | Disorientation | Right column |

Audio is muted during selection. Mode remains fixed until reset and is not saved.

## Twin Orbits

Start Main/X at noon, Y low; turn X slightly right to begin a slow orbit.

| Control | Behaviour |
|---|---|
| Main | Position offset; noon puts A front after phase reset |
| X | Signed speed; centre stops, extremes approach 1.5 turns/sec |
| Y | Distance: quieter, darker, later, with greater reflected/direct balance |
| Up / middle | Linked / opposing traversal |
| Down | Phase reset once per press; retains the prior relationship while held |
| CV1 / CV2 | Add position / distance |
| Pulse1 / Pulse2 | Movement clock / rising-edge phase reset |

X has a centre deadband of approximately ±3% of total travel (6% total width).
Clock lock requires two valid edges; intervals 25 ms–60 s. Clocked speed is capped
at 2 cycles/sec. Unpatching restores manual speed; timeout is the greater of
3 seconds or three clock intervals. Use pulse widths at least 1 ms. Default clock
division is four pulses per turn. CV inputs are nominal, not calibrated.

## Spatial Mixer

Up selects A; Down selects B; middle retains the last selection. Main sets that
source's position (noon front, ends back), X distance and Y panel level. Both
sources retain their placements. CV1/CV2 modulate both positions/distances without
changing the saved base; pulse inputs are unused.

Knobs require near/crossing pickup after source selection or placement restoration
(about 1.6% tolerance). The selected top/middle LED pair blinks until all three
controls are picked up. Defaults: A front, B back, both near at full panel level.
Editor A/B trims multiply the individual panel levels. The editor can Read/Apply/
Save both placements, with separate Mixer settings. Separation/clock fields are
unused in Mixer.

## Disorientation

Choose **Fig8, Pendulum or Wander** in the editor; Apply auditions and Save retains
the choice. Fig8 is the default. Begin with a slow X speed and listen without the
display for the first placement comparison.

| Movement | Main / CV1 | Y / CV2 |
|---|---|---|
| Fig8 | Front/back depth of crossing loops | Overall excursion |
| Pendulum | Arc centre: minimum front, noon right, maximum nearly back | Swing width up to ±90° |
| Wander | Front/back reach of smooth random positions | Overall excursion |

X controls signed phase speed, centre stops, maximum 1.5 cycles/sec. Up/middle
links/opposes the two source phases. **Down held freezes phase; release resumes**.
CV and shape controls can still reshape a frozen position. Pulse1 clocks phase;
Pulse2 resets even while frozen. Editor separation offsets source path phase.

Fig8 crosses in front and reaches behind at greater depth/excursion. Pendulum
reverses along the same arc at fixed 25% distance. Wander smoothly joins
deterministic random positions, with four transitions per phase cycle; reversing
X retraces the route. Reset restarts its sequence. Y minimum collapses Fig8/Wander
to a front point and Pendulum to its selected centre; it does not mute audio.

## Web editor and presets

Open [web/index.html](web/index.html) in desktop Chrome/Edge. Use a USB-C data
cable, close Serial Monitor/other apps, enable MIDI with SysEx permission and
select **Spatial Disorientation** for both ports. iOS is unsupported. If local
file MIDI access is blocked, serve the self-contained page from localhost:

```sh
python3 -m http.server 8793 --bind 127.0.0.1 --directory web
```

Visit http://127.0.0.1:8793. The page has no external scripts/services.

- **Read:** retrieve applied base settings and current Mixer placements.
- **Apply:** audition staged edits. Ordinary slider edits are not sent automatically.
- **Save to card:** persist applied settings/placements/default movement; briefly
  fades audio during the flash write. Apply dirty edits first.
- **Reset to init:** apply defaults for the active mode; Save separately to retain.
- **Export:** download displayed settings, including unapplied edits.
- **Import:** stage a named JSON preset; choose the matching startup mode, then Apply/Save.

Each mode has its own six base settings: phase/angular separation, room, A/B trims,
strength and clock division. Mixer adds six placement values; Disorientation adds
movement. Defaults: 180° separation, about 29% room, 50% trims, full strength,
four clock pulses/cycle, Fig8. Panel controls, CV, phase, motion and live 8mu
overrides are excluded from saved configuration.

This editor/firmware uses SysEx and JSON v3, flash v4. Old v1/v2 presets import;
old Disorientation presets choose Fig8. Flash v1/v2/v3 migrates in RAM without a
boot write, preserving prior banks/placements. Only explicit Save writes flash.
Older firmware cannot read v4; export presets before saving if you may downgrade.
A corrupt/absent record restores defaults; power-loss recovery is not guaranteed.
[Protocol details](docs/PROTOCOL.md).

## Direct 8mu control

Connect 8mu before reset. Rev 1.1 chooses host/device USB role at boot; earlier
hardware retains device/editor mode. Change cables and reset when switching roles.
Editor and 8mu are used in separate sessions. The inherited identify handshake
temporarily loads the 8mu default assignments/bank 1; controller bank selection
returns after power-cycling the 8mu. [Host adaptation](vendor/EightMU/SOURCE.md).

| Fader | Orbits / Disorientation | Mixer |
|---|---|---|
| 1 | Signed speed | Selected-source distance |
| 2 | Distance / excursion | Selected-source panel level |
| 3 | Angular / path-phase separation | Selected-source position |
| 4 | Room | Room |
| 5 / 6 | A / B trims | A / B trims |
| 7 | Spatial strength | Spatial strength |
| 8 | Motion amount | Unused |

Faders require independent near/crossing pickup. In Mixer, source selection
rearms faders 1–3 against stored placements. Waiting LEDs blink; picked LEDs show
level. Mixer LED8 identifies A steady/B blinking; Mixer ignores motion.

| Button | Orbits | Disorientation | Mixer |
|---|---|---|---|
| A | Reset/recentre | Reset/recentre | Select A |
| B | Toggle motion | Toggle motion | Select B |
| C held | Stop orbit speed temporarily | Freeze phase | Unused |
| D | Hold for timing | Tap cycles movement; hold for timing | Hold for timing |

Motion initially off. Orbits motion replaces Main's position; Disorientation
motion replaces Main's depth/arc centre. Roll offsets the captured pose; gyro yaw
is integrated as a rate. Disorientation Main values clamp at limits. Fader8 scales
tilt/rate; zero removes tilt influence and stops rate integration, retaining its
held base. A recaptures physical Main/current pose. CV remains additive.

In Disorientation, D release before 500 ms cycles Fig8→Pendulum→Wander; a longer
hold shows timing and never cycles on release. LEDs1/2/3 identify the choice for
one second. Live choice reverts to the saved movement on disconnect/new session.
C and panel Down combine: release both to resume. LED8 steady bright indicates
motion, bright blinking indicates Disorientation freeze; timing/selection notices
take priority. Turning motion off/disconnecting holds owned Main/X/Y until panel
pickup. Mixer handback preserves placements with pickup; Orbits returns to panel.
Shared editor trims return to saved values on disconnect. No 8mu gesture saves flash.

## LEDs, timing and verification

Top LED pair shows A's left/right direction, middle pair B's. Equal brightness
can mean front or back. Bottom left indicates linked motion or Mixer editing A.
Bottom right latches for callback ≥18 µs, block ≥1,200 µs or queue fault; hardware
reset clears it. Crossing a warning threshold does not stop audio. An actual
missed playback block outputs 64 frames of silence (1.33 ms); late audio is discarded.
8mu D diagnostics show 150 µs block-time bands; flashing all LEDs indicates a warning.

The user reports alpha14 tests pass, with peak callback **13 µs** and peak block
**997 µs**, below warning thresholds. Run duration and measurement USB role were
not supplied; callback timing excludes surrounding framework ISR work. Build and
twelve sanitizer host suites plus editor lifecycle/preset tests pass. Generic
short HRTFs may give front/back confusion; headphones and listener differences
matter. [Hardware protocols and results](docs/TEST_PROTOCOL.md).

Build: set `PICO_SDK_PATH` to Pico SDK 2.3.0, then `cmake -S . -B build` and
`cmake --build build -j2`. ComputerCard 0.4.0 is vendored unmodified; 192 MHz / 1.15 V,
48 kHz, 32 taps, RAM execution, 64-frame block processing with two-block transport
latency (2.67 ms). [Build provenance](licenses/BUILD_COMPONENTS.md).

## Attribution and licenses

Original DSP, controls, editor and documentation: Adrian Vos (soveda), MIT.
Hardware/build/editor conventions: Chris Johnson's ComputerCard 0.4.0 and
web_interface; host integration: Chris Johnson's WaveSeq/EightMU and rppicomidi,
MIT notices preserved. Block scheduling adapts Adrian Vos/contributors'
Workshop_BlockAudioCard; mathematical sine values reuse You spin me round.
Pico SDK is BSD-3-Clause with component notices; TinyUSB is MIT. Newlib/GCC runtime
notices and the GCC runtime exception accompany the firmware.

Measured HRTF data: **Bill Gardner and Keith Martin, MIT Media Laboratory (1994)**,
*HRTF Measurements of a KEMAR Dummy-Head Microphone*, Technical Report #280.
[Original source](https://sound.media.mit.edu/resources/KEMAR.html);
[retained terms and derivation](vendor/KEMAR/SOURCE_TERMS.md). The measurements
retain their attribution terms and are not relicensed under MIT. Horizontal
32-tap minimum-phase filters use separate analytical ear delays and a synthetic
room model. Full source mapping, revisions and notices:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
