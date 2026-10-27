# MIDI Support

Blackbird can receive and send USB MIDI. The API follows [monome iii](https://monome.org/docs/iii/code)'s MIDI functions, with the same names, arguments and message tables, placed in the `bb` table:

| Function | Description |
|---|---|
| `bb.event_midi(byte1, byte2, byte3)` | callback you define, called for each incoming MIDI message |
| `bb.midi_to_msg(data)` | decode `{byte1, byte2, byte3}` into a labelled message table |
| `bb.midi_out(msg)` | send a message table, or an array of raw bytes |
| `bb.midi_note_on(note, vel, ch)` | send a Note On |
| `bb.midi_note_off(note, vel, ch)` | send a Note Off |
| `bb.midi_cc(cc, val, ch)` | send a Control Change |
| `bb.midi_tx(byte1, byte2, byte3)` | send one raw MIDI message |

MIDI channels are always **1–16** (not 0–15).

## USB device mode vs USB host mode

Blackbird supports USB-MIDI in both directions, depending on the USB role it boots into:

- **Device mode**: Blackbird enumerates as a USB device (CDC serial + USB-MIDI). This is the mode you use with a computer + web-druid/druid for live coding and seeing `print()` output.
- **Host mode**: Blackbird acts as a USB host and talks MIDI with a class-compliant USB-MIDI device plugged into it (controller, keyboard, etc.).

Role selection is automatic at boot, based on the Workshop Computer’s USB power orientation (whether it believes it is downstream-facing power / DFP). In host mode, Blackbird waits briefly at startup for a USB-MIDI device to mount. Note you always need to power the Workshop System completely off and back on to switch between device and host mode- resetting computer alone is not enough.

Tip: during the startup “waiting” period, the LEDs animate differently:

- Device mode: “around-the-world” loop
- Host mode: top-to-bottom sweep

### Recommended workflow: develop in device mode, then switch to host mode

This lets you iterate quickly with web-druid while keeping the exact same `bb.event_midi` handler for your final host-mode setup.

1. **Develop + debug in device mode**
  - Connect Blackbird to your computer and open web-druid.
  - Load a script that defines `bb.event_midi` and uses `print()` so you can see events arriving.
  - Send MIDI to Blackbird from the computer (DAW, MIDI monitor, virtual keyboard). Your handler should fire and you should see prints in web-druid.

2. **Move to host mode (same script, same handler)**
  - Disconnect from the computer.
  - Connect a class-compliant USB-MIDI device to Blackbird (so Blackbird can be the USB host).
  - Reset/reboot Blackbird with the MIDI device already connected so it mounts during the startup window.
  - Your existing `bb.event_midi` will still be called on incoming MIDI, and `bb.midi_out` & co. send to the connected device.

Note: in host mode, you generally won’t have the web-druid serial console available, so prefer debugging by driving outputs/LEDs instead of relying on `print()`.

## Receive

### `bb.event_midi(byte1, byte2, byte3)`

Define this function to receive MIDI. It is called with the raw bytes of each incoming message; unused bytes are `0`. Decode the bytes with `bb.midi_to_msg`:

```lua
bb.event_midi = function(b1, b2, b3)
  local m = bb.midi_to_msg({b1, b2, b3})
  if m.type == 'note_on' then
    output[1].volts = (m.note - 60) / 12   -- middle C = 0V, 1V/oct
  elseif m.type == 'cc' and m.cc == 1 then
    output[2].volts = m.val / 127 * 5      -- mod wheel -> 0..5V
  end
end
```

Notes:

- Every message type is delivered, including program change, clock/start/stop/continue, song position and sysex. Active sensing (`0xFE`) is ignored.
- MIDI clock messages also drive Blackbird's `clock` when the clock source is MIDI (see below).
- To keep up with fast controllers, Blackbird passes at most one CC / pitch bend / pressure message per millisecond to Lua and drops the rest. Notes and all other messages always get through.
- If `bb.event_midi` is not defined, notes, CC, pitch bend and aftertouch are forwarded to the host as crow-style messages (e.g. `^^midi('on',60,100,1)`, `^^midi('cc',1,64,1)`, `^^midi('bend',0.5,1)`, `^^midi('aftertouch',64,1)`). These go out at most one every 4ms, so a burst (an all-notes-off is ~128 messages) can't overrun the host: norns' crow reader loses data if lines arrive faster than its Lua handles them. Up to 128 can wait their turn; beyond that, the extras are dropped.

### `bb.midi_to_msg(data)`

Decodes `data`, an array of MIDI bytes, into a table. `type` is one of:

| `type` | Fields |
|---|---|
| `note_on` | `note`, `vel`, `ch` |
| `note_off` | `note`, `vel`, `ch` (Note On with velocity 0 is decoded as `note_off`) |
| `cc` | `cc`, `val`, `ch` |
| `pitchbend` | `val` (0–16383, centre 8192), `ch` |
| `key_pressure` | `note`, `val`, `ch` |
| `channel_pressure` | `val`, `ch` |
| `program_change` | `val`, `ch` |
| `start`, `stop`, `continue`, `clock` | none |
| `song_position` | `lsb`, `msb` |
| `song_select` | `val` |
| `sysex` | `raw` (the bytes passed in) |
| `other` | `raw` (the bytes passed in, e.g. a sysex continuation) |

Active sensing decodes to an empty table.

## Transmit

Messages go to the computer in device mode, or to the connected MIDI device in host mode.

### `bb.midi_note_on(note, vel, ch)` / `bb.midi_note_off(note, vel, ch)` / `bb.midi_cc(cc, val, ch)`

Shortcuts for the most common messages. All arguments are optional: `note` defaults to 66, `cc` to 1, `vel` / `val` to 100 and `ch` to 1.

```lua
-- play a short note every beat
clock.run(function()
  while true do
    clock.sync(1)
    bb.midi_note_on(60, 100, 1)
    clock.sleep(0.1)
    bb.midi_note_off(60, 0, 1)
  end
end)
```

### `bb.midi_out(msg)`

Sends a message table in the format produced by `bb.midi_to_msg` (so incoming messages can be passed straight through), or an array of raw bytes.

```lua
bb.midi_out({type = 'pitchbend', val = 8192 + 4096, ch = 1})
bb.midi_out({type = 'program_change', val = 5, ch = 10})
bb.midi_out({type = 'clock'})
bb.midi_out({0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7})  -- raw bytes, e.g. sysex

-- MIDI thru, transposed up an octave
bb.event_midi = function(b1, b2, b3)
  local m = bb.midi_to_msg({b1, b2, b3})
  if m.note then m.note = math.min(m.note + 12, 127) end
  bb.midi_out(m)
end
```

When fields are missing, `ch` defaults to 1, note `vel` to 100 and pitch bend `val` to 8192 (centre). Everything else defaults to 0. An unknown `type` raises an error.

### `bb.midi_tx(byte1, byte2, byte3)`

Sends one raw MIDI message. If `byte1` is a status byte, exactly that message's length is sent (so `bb.midi_tx(0xC0, 5)` sends a 2-byte program change), with missing bytes sent as `0`. Data and sysex bytes are sent as given.

## Values and ranges

- Data values (`note`, `vel`, `cc`, `val`) are 7-bit and are masked to 0–127; channels outside 1–16 are clamped.
- Unlike iii, CC88 high-resolution velocity is not combined into `vel`: the CC88 arrives in `bb.event_midi` as a normal `cc` message just before the Note On. (`bb.event_mpe_note` does combine it, see below.)

## Optional: MPE routing

Blackbird can route the per-note expression of an [MPE](https://midi.org/midi-polyphonic-expression-mpe) controller into dedicated callbacks. This is a Blackbird extension with no iii equivalent.

### `bb.mpe_mode(enable)`

- `bb.mpe_mode(true)` turns MPE routing on, `bb.mpe_mode(false)` turns it off. Each new script starts with it off.
- `bb.mpe_mode()` returns the current state (`true` / `false`).

When MPE mode is on, these messages go to the callbacks below instead of `bb.event_midi`. All other messages (other CCs, program change, clock, etc.) still go to `bb.event_midi`.

| Callback | Called for |
|---|---|
| `bb.event_mpe_note(type, note, vel, ch)` | Note On/Off. `type` is `'note_on'` or `'note_off'` (Note On with velocity 0 is a `'note_off'`) |
| `bb.event_mpe_bend(val, ch)` | Pitch bend, `val` normalized to **-1.0 .. 1.0** |
| `bb.event_mpe_pressure(val, ch)` | Channel pressure (poly pressure is also mapped), `val` 0–127 |
| `bb.event_mpe_slide(val, ch)` | CC74, `val` 0–127 |

```lua
function init()
  bb.mpe_mode(true)
end

local voice_ch = nil
bb.event_mpe_note = function(type, note, vel, ch)
  if type == 'note_on' then
    voice_ch = ch
    output[1].volts = (note - 60) / 12
  elseif ch == voice_ch then
    voice_ch = nil
  end
end

bb.event_mpe_pressure = function(val, ch)
  if ch == voice_ch then output[2].volts = val / 127 * 5 end
end
```

Notes:

- Each MPE note plays on its own channel, so use `ch` to tell which note an expression message belongs to. In a standard MPE setup, channel 1 is the zone's master channel: bend/pressure there apply to all notes, and notes arrive on channels 2–16.
- Expression (bend, pressure, slide) is **coalesced, not dropped**: if a controller sends faster than Lua can keep up, intermediate values are skipped but the latest value per channel is always delivered, so a release back to 0 is never lost. A channel's pending expression is delivered before its next Note On/Off, preserving the order MPE controllers rely on.
- Callbacks you don't define are forwarded to the host as crow-style messages via `^^mpe(...)` (e.g. `^^mpe('on',note,vel,ch)`, `^^mpe('pressure',val,ch)`), paced the same way as `^^midi`.

#### High-resolution velocity (CC88) in MPE mode

If a controller sends **CC88** (High Resolution Velocity Prefix) immediately before a **Note On** on the same channel, `bb.event_mpe_note` receives `vel` as a **fractional number**:

`vel = vel7 + (fine7 / 128)`, so `vel14 = math.floor(vel * 128 + 0.5)` recovers the 14-bit velocity (0..16383).

CC88 applies to the next Note On on that channel and expires if no Note On follows soon.

## MIDI Clock integration (optional)

Blackbird also listens for MIDI realtime messages:

- Clock tick (`0xF8`)
- Start (`0xFA`)
- Continue (`0xFB`)
- Stop (`0xFC`)

When the system clock source is set to MIDI, these messages drive Blackbird’s internal `clock` engine (tempo and transport). They are also passed to `bb.event_midi`. In Lua, the clock-source helpers are exposed as global functions:

- `clock_get_source()` returns 1-based: `1=internal`, `2=MIDI`, `3=LINK`, `4=CROW`
- `clock_set_source(source)` sets the source (pass `0` for “auto”)

Example: force MIDI clock

```lua
clock_set_source(2)
```

## Coming from iii

- The functions are the same, but live in `bb`: `event_midi` → `bb.event_midi`, `midi_out` → `bb.midi_out`, and so on.
- Message tables, type names, field names, defaults and channel numbering are the same.
- Blackbird adds USB host mode, MPE routing and MIDI clock sync, and limits how fast CC / pitch bend / pressure messages reach Lua.
