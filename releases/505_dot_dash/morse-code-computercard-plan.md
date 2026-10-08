# Morse Code ComputerCard — Build Plan

A Eurorack ComputerCard that converts USB keyboard input into morse code via CV and pulse outputs.

---

## Overview

Plug a USB keyboard into the Workshop Computer, type letters, and the card converts them to morse code:
- **CV Out 1**: Audio-rate square wave tone (beeps for dots/dashes)
- **Pulse Out 1**: Gate high during transmission, low during gaps
- **LEDs**: Visual feedback showing current symbol
- **Knob X**: Speed control (5–40 WPM)
- **Switch**: Mode select (Up = Audio beep, Down = Pitch CV)

---

## Architecture

### Dual-Core Design

Following the `hid_keyboard_mouse` example pattern:
- **Core 0**: USB host stack (TinyUSB) — handles keyboard HID reports
- **Core 1**: Audio processing (`ComputerCard::Run()`) — runs at 48kHz, generates morse output

### Thread-Safe Communication

Static volatile variables (written by Core 0, read by Core 1):
- `key_queue[]` — ring buffer of ASCII characters to transmit
- `queue_head`, `queue_tail` — queue pointers
- `new_key_pressed` — flag for new character added

---

## Morse Code Timing

### Standard Timing (based on "Paris" reference word)

```
Dot duration    = 1 unit
Dash duration   = 3 units
Symbol gap      = 1 unit (between dots/dashes in same letter)
Letter gap      = 3 units (between letters)
Word gap        = 7 units (between words — triggered by spacebar)
```

### Speed Calculation

WPM (words per minute) → dot duration in milliseconds:
- `dot_ms = 1200 / WPM`
- 5 WPM  → 240ms/dot
- 20 WPM → 60ms/dot
- 40 WPM → 30ms/dot

At 48kHz sample rate:
- `dot_samples = (1200 / WPM) * 48` (approximate, fine-tuned via knob)

---

## Output Design

### CV Out 1 — Audio Tone

- **Switch Up**: Square wave at ~800Hz during dots/dashes, silent during gaps
- **Switch Down**: Static pitch CV (0V = nothing, +5V or calibrated note = active)
- Amplitude: Full scale (-2048 to 2047), or maybe Knob Y controls volume?

### Pulse Out 1 — Gate

- High (+5V gate) during dot and dash transmission
- Low during symbol gaps, letter gaps, word gaps
- Use for triggering envelopes, flashing external LEDs, clocking sequencers

### LEDs — Visual Feedback

| LED | Function |
|-----|----------|
| LED 0 | "Transmitting" — lit when any morse is active |
| LED 1 | Dot indicator — blinks during dots |
| LED 2 | Dash indicator — lit during dashes |
| LED 3 | Queue status — shows buffer fill level |
| LED 4 | Preset mode active (SOS, CQ, etc.) |
| LED 5 | Speed indicator — brightness = WPM (dim=slow, bright=fast) |

---

## Morse Code Table

```cpp
// Letters
'A': ".-"      'N': "-."
'B': "-..."    'O': "---"
'C': "-.-."    'P': ".--."
'D': "-.."     'Q': "--.-"
'E': "."       'R': ".-."
'F': "..-."    'S': "..."
'G': "--."     'T': "-"
'H': "...."    'U': "..-"
'I': ".."      'V': "...-"
'J': ".---"    'W': ".--"
'K': "-.-"     'X': "-..-"
'L': ".-.."    'Y': "-.--"
'M': "--"      'Z': "--.."

// Numbers
'0': "-----"   '5': "....."
'1': ".----"   '6': "-...."
'2': "..---"   '7': "--..."
'3': "...--"   '8': "---.."
'4': "....-"   '9': "----."

// Prosigns / Common
' ': Word gap (7 units)
```

---

## Presets (Triggered by Switch + Knob combinations)

### SOS — Emergency (Knob X full left + Switch Up)

```
"... --- ..."
```

### CQ — Calling All Stations (Knob X full left + Switch Down)

```
"-.-. --.-"
```

### CQD — Distress (pre-Mayday standard)

```
"-.-. --.- -.."
```

### Preset Activation

Hold **Switch Up** + turn **Knob X fully left** for 2 seconds → LED 4 flashes, then plays SOS once.

Or: Use special key combos? E.g., `Ctrl+S` = SOS, `Ctrl+C` = CQ? (Need to check if HID reports modifiers separately — yes, the `hid_keyboard_report_t` has a `modifier` field!)

---

## Buffer/Queue Design

```cpp
#define QUEUE_SIZE 64  // About 12 words worth

static volatile char key_queue[QUEUE_SIZE];
static volatile uint8_t queue_head = 0;
static volatile uint8_t queue_tail = 0;

// Core 0 (USB) writes:
bool enqueue(char c) {
    uint8_t next = (queue_head + 1) % QUEUE_SIZE;
    if (next == queue_tail) return false; // Queue full
    key_queue[queue_head] = c;
    queue_head = next;
    return true;
}

// Core 1 (Audio) reads:
bool dequeue(char *c) {
    if (queue_head == queue_tail) return false; // Queue empty
    *c = key_queue[queue_tail];
    queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    return true;
}
```

---

## State Machine (Audio Core — ProcessSample)

```
STATE_IDLE → Waiting for queue, PulseOut1 = low, CV silent
    ↓ (char available)
STATE_LOAD_CHAR → Look up morse pattern, set symbol index = 0
    ↓
STATE_SYMBOL_START → Get current symbol (dot or dash)
    ↓
STATE_DOT → CV tone on, Pulse high, count down dot_samples
    ↓ (dot done)
STATE_SYMBOL_GAP → CV silent, Pulse low, count down symbol_gap_samples
    ↓ (more symbols in letter)
STATE_DASH → CV tone on, Pulse high, count down dash_samples (3x dot)
    ↓ (dash done)
STATE_SYMBOL_GAP → (same as above)
    ↓ (letter complete)
STATE_LETTER_GAP → CV silent, Pulse low, count down letter_gap_samples (3x dot)
    ↓
STATE_IDLE → (loop)

SPACE character → triggers STATE_WORD_GAP (7x dot duration)
```

---

## Files to Create

```
morse_code/
├── main.cpp              # Main card code
├── CMakeLists.txt        # Build configuration
├── morse_table.h         # Morse code lookup table
└── README.md             # Card documentation
```

---

## Dependencies

Same as `hid_keyboard_mouse` example:
- `ComputerCard.h` ( ComputerCard library)
- `pico/multicore.h` (RP2040 dual-core)
- `tusb.h` (TinyUSB host stack)
- Standard Pico SDK build setup

---

## Build Steps

1. **Copy the `hid_keyboard_mouse` example** as starting point
2. **Replace keyboard handler** — enqueue characters instead of triggering sounds
3. **Add morse state machine** to `ProcessSample()`
4. **Add morse lookup table** (`morse_table.h`)
5. **Add queue + timing logic**
6. **Add knob/switch handling** for speed and mode
7. **Build and flash** as `.uf2` file

---

## Testing Checklist

- [ ] Plug USB keyboard, type "SOS" → hear ... --- ...
- [ ] Verify timing with metronome/app at different WPM settings
- [ ] Check Pulse Out 1 with scope or LED module
- [ ] Test queue overflow (type very fast)
- [ ] Verify switch mode changes (audio vs pitch CV)
- [ ] Test SOS preset (switch up + knob left for 2 sec)
- [ ] Test CQ preset (switch down + knob left for 2 sec)

---

## Future Enhancements (v2 ideas)

- **Variable pitch**: Knob Y sets morse tone frequency (300–2000Hz)
- **Prosign macros**: Full set of prosigns (AR, SK, BK, etc.)
- **Morse decoder mode**: Reverse — listen to incoming morse on Audio In, decode to LED display
- **Save/recall messages**: Store commonly used phrases to flash
- **USB MIDI fallback**: Accept note input from MIDI controller if no keyboard

---

*Created: 2026-10-08*
*For: Music Thing Modular Workshop Computer*
