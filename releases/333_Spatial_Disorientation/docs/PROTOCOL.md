# Spatial Disorientation editor protocol

Current alpha14 uses **SysEx/presets v3 and storage v4**, defined in the final sections.
The v1 sections below are retained for migration/history.

© 2026 Adrian Vos (soveda), MIT. SysEx/editor transport structure follows Chris
Johnson's ComputerCard web_interface example. See [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

Frame bytes: `F0 7D 53 44 01 command sequence payload F7`. Experimental non-commercial
manufacturer ID 7D, product signature ASCII SD, schema 1. All interior bytes are
7-bit. Sequence 1–127 correlates requests; telemetry uses 0. One outstanding request
per editor. Unknown signatures are ignored; unsupported versions/commands and
invalid settings are rejected. No dynamic buffers in the firmware parser.

| Command (hex) | Payload / response |
|---|---|
| 01 Read | Empty; response 41 with six parameter triples |
| 02 Apply | Six triples; response 42 after audio core consumes settings |
| 03 Save | Empty; response 42 after muted flash save/verification |
| 41 Snapshot | Triples: stable ID, value low 7 bits, value high 7 bits |
| 42 Acknowledge | Request command, status |
| 43 Telemetry | A angle low/high, B angle low/high, distance low/high, flags; about 20 Hz |

Statuses: 0 success, 1 unsupported command/length, 2 unsupported schema, 3 invalid
settings, 4 busy, 5 save failed. Apply requires every ID exactly once; validation
is transactional. Realtime MIDI bytes are ignored within SysEx. Oversized or
malformed frames are discarded. Editor timeout: 1.5 s, or 3 s for Save.

| ID | Setting | Range / default |
|---|---|---|
| 1 | Separation | 0–4095 maps a turn / 2048 |
| 2 | Room | 0–4095 / 1200 |
| 3 | Source A level | 0–4095 / 2048 |
| 4 | Source B level | 0–4095 / 2048 |
| 5 | Spatial strength | 0–4095 / 4095 |
| 6 | Clock pulses/turn | 1, 2, 4, 8, 16 / 4 |

Angles are 0–4095: 0 front, 1024 right, 2048 back, 3072 left. Flags: bit 0 linked,
bit 1 clock locked, bit 2 latched callback timing warning, bit 3 saving. Telemetry
is observational; separately read fields can differ by one control update.

Flash record: little-endian uint32 magic 0x314f4453, uint32 schema 1, uint32 FNV-1a
checksum of six uint16 values, then those values (24 bytes total). Stored in the
first page of the final 4 KB sector of actual flash. No erase at boot, no autosave.
Audio core fades to zero and allows DAC pipeline flushing before acknowledging;
USB core locks out audio core and disables its local interrupts for erase/program.
RAM execution and a firmware-overlap check are required. A failed/torn save uses
defaults next boot; this prototype does not promise power-loss recovery of old settings.

## Alpha4 timing diagnostics (compatible extension)

Command 44, sequence 0, four payload bytes: callback peak low/high 7 bits, DSP
block peak low/high 7 bits, in microseconds, saturated at 16383. Sent alongside
telemetry at approximately 20 Hz when the bounded TX queue is empty. Peaks reset
on reboot and ignore intentional flash-save pauses. Callback measurement excludes
the framework's surrounding ISR; block measurement includes core-1 interruption.

Additional 43 flags: bit 4 block render >=1200 us, bit 5 audio queue miss, bit 6
callback >=18 us. Bit 2 remains the combined latched warning. These fields do not
alter parameter IDs, config schema, existing commands or flash records. USB TX is
non-blocking: if the host does not consume, queued bytes stay bounded; telemetry
is skipped and an overloaded command client may time out and retry. Commands
should remain one at a time.

## Alpha5 tap identity

Timing command 44 may append a fifth payload byte: active FIR taps, 32 or 64.
The updated editor accepts both alpha4's four-byte and alpha5's five-byte form.
The config schema and parameter IDs remain unchanged. Tap count is a compile-time
comparison profile, not a saved editor setting.

## Alpha8 separate USB roles

The editor protocol/configuration schema remains version 1. A computer-connected
reset selects device/editor mode on Rev1.1 hardware; an accessory/empty port
selects host/8mu mode. Reset after switching roles. Direct 8mu runtime controls
are volatile overrides and do not modify this protocol's saved configuration.
Clock division remains its saved value. Offline 8mu diagnostics use LEDs rather
than SysEx; no simultaneous editor connection is required. Implementation/source
credit: Adrian Vos (soveda), 2026, MIT; Chris Johnson's WaveSeq/EightMU and the
rppicomidi driver are credited in ../vendor/EightMU/SOURCE.md.

## Preset file format v1 (editor, 2026-10-09)

Original format and implementation: Adrian Vos (soveda), 2026, MIT. This is a
local editor file, independent of the unchanged version-1 SysEx/storage protocol.

```json
{
  "format": "spatial-disorientation-preset",
  "version": 1,
  "function": "twin-orbits",
  "name": "Twin Orbits",
  "parameters": {
    "1": 2048,
    "2": 1200,
    "3": 2048,
    "4": 2048,
    "5": 4095,
    "6": 4
  }
}
```

Parameter IDs retain the table above: 1 separation, 2 room, 3 A level, 4 B level,
5 spatial strength (integer 0–4095); 6 clock division (1, 2, 4, 8 or 16). Exactly
these six IDs are required. Name is nonblank, at most 80 characters, with no control
characters. Function and format/version must match; future functions require their
own validation. Additional top-level metadata may be ignored; unknown parameter
IDs are rejected. Import limit is 16 KB. Bad files change nothing.

Export writes displayed values, even if they have not been applied. Import stages
values only and marks them dirty. Connecting/reconnecting reads the card but
preserves staged edits; explicit Read discards them. Apply must succeed before
Save can persist imported values. Neither import nor export sends MIDI by itself.
Name, physical controls, CV, phase, live 8mu overrides and motion calibration are
not part of saved card configuration. That initial file-format checkpoint used alpha8; the current alpha9 extension
is specified below.

## Alpha9 mode telemetry and Mixer presets

Original extension: Adrian Vos (soveda), 2026, MIT. Command 43 appends four bytes
for an 11-byte payload: index 7 mode (0 Twin Orbits, 1 Spatial Mixer, 2 reserved
Disorientation), indices 8/9 B distance low/high, index 10 Mixer state. State bit 0
selects B (clear A); bits 1/2/3 indicate Main/X/Y pickup. Earlier seven-byte telemetry
is accepted as Twin Orbits; the interim eight-byte mode form is also accepted.
Separately read snapshot fields can differ by one control update. Mode is observation
only: no MIDI command selects it. Down-at-startup selection is fixed until reset.

Preset v1 now accepts `twin-orbits` or `spatial-mixer` function tags. All six fields
retain their ranges; separation and clock division are unused in Mixer but retained.
Import can stage the other mode; Apply is disabled until its startup mode matches.
Reserved Disorientation presets are rejected, and export is disabled while that
mode runs. Flash remains one shared six-field v1 record. Mode, source selection,
panel position/distance/level and pickup are volatile, excluded from JSON/flash.
This supersedes the alpha8-only format status above, not existing parameter IDs.

## Alpha10 Mixer 8mu feedback (internal)

No SysEx or flash schema change. Internal shared.mu_feedback bits 0–6 are Mixer
fader pickup, bit 9 selected B (clear A), bit 10 Mixer mapping. Bit 8 remains
Twin Orbits motion enabled, but is never set by Mixer. LED 8 in Mixer overrides
its unused fader: steady A / blinking B; D-held diagnostics retain priority.
A/B buttons select A/B; motion, C and fader 8 are ignored. Source selection from
panel uses settled switch transitions so a held panel Up cannot veto remote B.
Original extension: Adrian Vos (soveda), 2026, MIT; existing dependency credits apply.

## Alpha11 current protocol and storage v2

Original contract/integration: Adrian Vos (soveda), 2026, MIT. This supersedes
v1 SysEx/flash sections above; existing source/dependency licenses remain applicable.

Frame: `F0 7D 53 44 02 command sequence payload F7`. Commands/ack status values
retain their IDs. Read (01) has empty payload; Snapshot (41) begins with mode byte
0 Orbits / 1 Mixer, then six/twelve parameter triples. Apply (02) uses the same
mode-tagged payload and is rejected if it differs from the running startup mode.
Save (03) has empty payload and saves a coherent audio-core snapshot of current
base settings and live Mixer placements. Reserved mode cannot Read/Apply/Save.
Telemetry 43/44 fields are unchanged, but use frame version 2. Maximum settings
payload is 37 bytes, complete frame 45 bytes; bounded 64-byte buffers remain.
Old v1 SysEx clients are rejected; update the editor alongside firmware.

IDs 1–6 keep the original ranges. Mixer adds 7 A position, 8 A distance, 9 A panel
level, 10 B position, 11 B distance, 12 B panel level: all integers 0–4095. Position
uses Main units (2048 front, 0/4095 back, 1024 left, 3072 right). Defaults:
A [2048,0,4095], B [0,0,4095]. Distance/level are clamped knob units; CV and current
rendered angle/delay are not stored. Apply restores all displayed placements and
rearms panel/source-fader pickup. Other mode's configuration is preserved.

Core 1 requests a snapshot with an aligned flag; core 0 copies all base settings
and current raw placements, publishes with barriers and never waits. Core 1 waits
at most 500 ms while servicing blocks and USB. Read/Apply/Save use this snapshot;
Save then performs the existing fade/flash handshake. Runtime 8mu trim overrides
are excluded; no host-role Save gesture is introduced.

Flash v2: uint32 magic 0x314f4453, uint32 version 2, uint32 checksum; 36-byte payload
containing Orbits six uint16 values, Mixer six uint16 values, then six uint16 Mixer
placement values. Record is 48 bytes. Checksum is existing FNV-1a over the payload.
Last-sector/page location and lockout/verification remain unchanged. Valid v1
records migrate their six settings to BOTH mode banks with default placements;
no boot write. Invalid/truncated/unknown records use defaults. Old firmware cannot
read v2. Power-loss behavior is unchanged: a failed/torn record may use defaults.

JSON format retains `spatial-disorientation-preset` and function tags, version now
2. Parameters contain IDs 1–6 for Orbits or 1–12 for Mixer. Import accepts v1 six-ID
files: Orbits values unchanged; Mixer gets default placements. Names, limits and
atomic validation remain unchanged. Export always emits v2, displayed values only.
Function-mismatched imports can be staged but cannot Apply. Preset files and both
saved banks do not change normal startup's Twin Orbits default.

## Alpha12 Fig8 protocol and storage v3

Original extension: Adrian Vos (soveda), 2026, MIT; existing source/dependency
credits apply. SysEx frame version remains 2. Snapshot/Apply accepts mode 2
Disorientation with six IDs; ID 1 is source phase separation along the Fig8.
Modes 0/1 retain their contracts. Save retains all three banks and Mixer placements.
Telemetry 43 appends byte 11: 0 moving/stopped normally, 1 phase frozen by held
Down. The payload is 12 bytes. Angles/distances in Fig8 are resolved worker
positions, not raw path phase; they remain observational, not an atomic snapshot.
Bit 0 linked and bit 1 clock-locked retain their meanings. Clients also accept
older telemetry lengths. Internal mu_feedback bit 11 means no Fig8 performance
mapping; D-held timing feedback still works.

Flash v3 record: same magic, version 3 and checksum, then 48-byte payload:
Orbits, Mixer and Disorientation each six uint16 config values, followed by six
uint16 Mixer placement values. Total record 60 bytes; existing FNV-1a over the
payload. V2 (48-byte record) migrates both banks/placements and defaults the third;
v1 seeds Orbits/Mixer and defaults the third plus placements. No boot write.
Unknown, invalid or truncated records restore defaults. Existing flash location,
fade/lockout and power-loss behaviour remain. Older firmware cannot read v3.

Preset JSON remains version 2 and adds `disorientation` with exactly IDs 1–6.
V1 files cannot describe Fig8 and are rejected for that tag. Existing v1/v2
Orbits/Mixer imports retain their migration rules. Panel depth/excursion, traversal
rate, phase, CV and freeze are excluded from Read/Save/preset files. Startup mode
continues to be selected only physically and normal boot continues to be Orbits.

## Alpha13 Fig8 8mu feedback

No SysEx, flash or preset schema change. Internal mu_feedback bit 11 (2048)
now identifies the Fig8 mapping; bits 0–7 are fader pickup, bit 8 motion enabled,
bit 12 (4096) phase frozen by C or panel Down. LED8 bright-blinks while frozen,
otherwise steady bright while motion enabled, otherwise normal pickup/level.
D diagnostics have priority. Telemetry 43 byte 11 reports the combined freeze.
No 8mu Save gesture or forwarding editor connection is added. Original extension:
Adrian Vos (soveda), 2026, MIT; existing source/dependency credits apply.

## Alpha14 movement setting, SysEx/JSON v3 and flash v4

Original extension: Adrian Vos (soveda), 2026, MIT; existing dependency/measurement
credits apply. Frame version is now 3 (`F0 7D 53 44 03 ... F7`). Prior v1/v2
clients receive unsupported-schema status; reload the matching editor/firmware.
Commands/status values and six base setting IDs remain. Mode 2 Snapshot/Apply
contains seven triples with IDs 1–6 and **13 movement**: 0 Fig8 (default),
1 Pendulum, 2 Wander. Mixer remains IDs 1–12; Orbits IDs 1–6. Maximum payload37
and bounded buffers are unchanged. Movement validation is transactional and a
mode-2 update cannot overwrite Mixer placements or another mode’s bank.

Telemetry43 appends byte 12 movement to the prior 12-byte payload: total13 bytes.
Byte 11 remains combined phase freeze. Resolved angles/distances remain observed
worker positions. Internal mu_feedback bits 13–14 encode movement; bit 15 marks
D-held diagnostic eligibility after 500 ms. Bit 11 remains Disorientation mapping,
bit 12 freeze, bit 8 motion, bits 0–7 fader pickup. LED 1/2/3 selection notice takes
priority for one second after movement changes; D diagnostics resume afterwards.
D tap cycles on release and does not reset phase or write flash.

Flash v4 retains magic/checksum format. Payload 52 bytes: prior v3 48-byte settings,
uint16 movement, uint16 reserved (zero). Record 64 bytes. FNV-1a covers all 52 payload
bytes. V1/v2/v3 records migrate without writing at boot, preserving previous
banks/placements and initializing movement 0. Invalid/unknown/truncated records
use defaults. Location, fade/lockout and power-loss limitations remain unchanged.
Older firmware cannot read v4.

JSON format is now version3. Disorientation requires IDs 1–6 and13; other modes
retain six/twelve IDs. V1/v2 import remains supported; v2 Disorientation’s six
IDs append Fig8 movement 0. V1 cannot describe Disorientation. Export always emits
v3. Live 8mu choice is excluded from saved snapshots; explicit editor choice is
saved/exported. Startup remains physical, normal boot remains Orbits.
