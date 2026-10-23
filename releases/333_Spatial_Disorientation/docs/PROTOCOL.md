# Beta3 editor and persistence contract

© 2026 Adrian Vos(soveda), MIT. Transport adapts Chris Johnson's ComputerCard
web_interface example; [attribution](../THIRD_PARTY_NOTICES.md). Firmware source
`src/settings.h` and `src/usb_editor.h` are authoritative.

**SysEx7 / JSON7 / flash8**, matching0.4.0-beta3 (also beta2,0.3.0alpha1–3 and0.4.0pitch/roll alphas).
Frame: `F0 7D 53 44 07 command sequence payload F7`. All interior bytes are7-bit.
Sequences1–127 correlate requests;0 is telemetry. One request at a time; fixed
buffers/queues and bounded parsing. Unsupported versions are rejected.

| Command hex | Request / response |
|---|---|
| 01 | Read, empty;41 snapshot |
| 02 | Apply mode-tagged settings;42 ack after core0 consumes |
| 03 | Save, empty;42 ack after snapshot, fade/flash handshake and verification |
| 04 | Mixer-only heights, A/B lo7/hi7 pairs;42 ack |
| 05 | Mixer-only height read;45 A/B height pairs |
| 41 | Snapshot: mode byte then ID/lo7/hi7 triples |
| 42 | Ack: request command,status |
| 43 | Live telemetry17bytes, sequence0 |
| 44 | Timing6bytes, sequence0 |

Ack statuses0success,1unsupported command/length,2protocol mismatch,3invalid
settings/mode,4busy,5save failure. Apply is transactional; every expected ID
exactly once. Mode0Orbits has9fields/28body bytes; mode1Mixer14fields/43bytes;
mode2Disorientation11fields/34bytes. Same-mode Apply only.

| IDs | Meaning / limits |
|---|---|
| 1–5 | Separation,room,A trim,B trim,strength;0..4095 |
| 6 | Clock division1,2,4,8,16 |
| 7–12 | Mixer A then B position,distance,level;0..4095 |
| 13 | Disorientation movement0Fig8,1Pendulum,2Wander |
| 14/15 | Mixer A/B height0..4096 (2048level,±40° extremes) |
| 16 | Orbits height or Disorientation centre0..4096 (mode-scoped) |
| 17 | Vertical excursion0..2048 (40°half-range) |
| 18 | Orbits/Disorientation uphill bearing0..4095 full turn,0front |
| 19 | Orbits/Disorientation tilt0..2048 (40°) |

43body: A angle lo/hi,B angle lo/hi,A distance lo/hi,flags,mode,B distance lo/hi,
Mixer state,freeze,movement,A height lo/hi,B height lo/hi. Direction angle0front,
1024right,2048back,3072left. Heights0below/2048level/4096above. Flags bits0linked,
1clock,2combined latched timing/queue/save warning,3save,4slow block,5queue fault,6slow callback.
Mixer state bit0sourceB;bits1–3picked-up Main/X/Y. Telemetry is observational;
fields can differ by one control update. 44body: callback peak lo/hi,block peak
lo/hi,64tap identity,solo flag0 for beta. Saturates at16383µs; peaks reset at reboot
and exclude deliberate Save pauses. Callback timing excludes framework ISR work.

JSON: format `spatial-disorientation-preset`, version7, function `twin-orbits`,
`spatial-mixer` or `disorientation`, nonblank name≤80characters, `parameters` keyed
by required IDs. Maximum file16KB. Imports1–6 migrate absent fields to defaults;
unknown/missing/invalid IDs fail atomically. Import stages without sending; Export
uses displayed settings. Names/live motion/phase/CV/8mu overrides are not flash.

Flash record84bytes: uint32magic0x314f4453,version8,FNV1a checksum,Settings72bytes.
Settings: three Config banks, Mixer placements, movement,reserved0,A/Bheight,
verticalcentre/depth,slopebearing/tilt,Orbitsheight/bearing/tilt,reserved0. Explicit Save writes final4KB flash sector
with fade, actual ADC/DMA pause, core0/core1 ownership and lockout handshake, followed by verified sampler restart. Unchanged saves skip flash and mute. Reads1–7 migrate inRAM;
no boot erase/autosave. Corrupt/torn records default; there is no redundant recovery
record. Beta1 cannot read flash8/JSON7. Export old presets before saving beta settings.
Historical contracts and source are preserved in the archive.


8mu Orbits page and live height/slope state are not persisted. Page-feedback bits
in the internal core0→core1 mailbox are separate from SysEx telemetry. Editor
randomise/reset tools stage legal per-mode elevation values and do not transmit
until Apply; they introduce no new parameter IDs, wire commands or flash fields.
Orbits Apply/init readback mismatch leaves desired edits staged with an error.
