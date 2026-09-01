# 92 USB MIDI Host Synth — Control Flow

USB MIDI Host Synth (release **92**, SysEx device ID **79**). This document
describes how the firmware is structured, how the two RP2040 cores interact,
and how MIDI, panel I/O, audio, and configuration flow through the system.

Firmware **0.10.x** — see also [`VOICE_MATRIX.md`](VOICE_MATRIX.md) for the
121-patch voice matrix spec.

**Clock:** firmware runs at **200 MHz** (not the AGENTS default 144 MHz) to
leave headroom for four poly voices, drum synthesis, and per-voice character FX
at 48 kHz while core 1 services TinyUSB host/device. Firmware is built
**flash-resident** (no `copy_to_ram`) — same rationale as `82_Computer_Grids`
to avoid USB/core timing surprises.

**ComputerCard:** vendored `ComputerCard.h` + `ComputerCardImpl.cpp` at the
release root (v0.2.8 from the repo framework).

---

## Architecture summary

The app runs on **two RP2040 cores** with a strict split:

| Core | Role | Main loop |
|------|------|-----------|
| **Core 0** | Real-time audio / panel I/O | `ComputerCard::Run()` → DMA ISR @ **48 kHz** → `ProcessSample()` |
| **Core 1** | USB + editor comms | `usbCore()` forever loop |

Shared state lives in **`runtime_state`** volatiles and **`g_config` / `g_ext`**.
Voice allocation uses **`g_midiCs`** on Core 1 only — Core 0 deliberately
**never** locks it during audio (blocking the ADC mux caused knob flutter).

---

## 1. System overview

```mermaid
flowchart TB
    subgraph Boot["Boot (main)"]
        M[main] --> R[usbRoleRegisterHostCallbacks]
        R --> CLK[set_sys_clock 200 MHz]
        CLK --> CTOR[UsbMidiHostCard ctor]
        CTOR --> RUN[card.Run → blocks on Core 0]
        CTOR --> C1[multicore_launch_core1]
    end

    subgraph Core0["Core 0 — Audio / Panel @ 48 kHz"]
        AW[ComputerCard::AudioWorker]
        ISR[DMA IRQ → BufferFull]
        PS[UsbMidiHostCard::ProcessSample]
        AW --> ISR --> PS
        PS --> ADC[Read knobs + switch via ADC mux]
        PS --> KNOB[applyKnobMappedSlots]
        PS --> SETUP[serviceSetupControls]
        PS --> CV[CV + Pulse outputs]
        PS --> SYNTH[Poly voice mix + drums]
        PS --> DAC[AudioOut1/2 → SPI DAC]
        PS --> LED[LED / glyph display]
        PS --> FLASH[serviceFlashSaveRequest]
    end

    subgraph Core1["Core 1 — USB loop"]
        UC[usbCore forever]
        UC --> ROLE{USB role?}
        ROLE -->|Host DFP| HOST[tuh_task]
        ROLE -->|Device UFP| DEV[tud_task]
        HOST --> HCB[tuh_midi_rx_cb]
        HCB --> PMS[parseMidiStream]
        DEV --> RX[drainDeviceMidiRx]
        RX --> PD[parseDeviceMidiBytes]
        PD --> PM[parseMidiByte / SysEx]
        DEV --> TX[flushSysExQueue + panel stream]
    end

    subgraph Shared["Shared RAM"]
        GS[g_note/gate/bend volatiles]
        GC[g_config + g_ext]
        GP[g_panelMain/X/Y/Switch]
        GV[g_poly voices + g_midiCs]
    end

    PMS --> HCM[handleChannelMessage]
    PM --> HCM
    PM --> SYSEX[sysexProcessIncoming]
    HCM --> GV
    HCM --> GS
    HCM --> GC
    SYSEX --> GC
    PS -.read volatiles.-> GS
    PS -.read params.-> GC
    PS -.read panel.-> GP
    PS --> SYNTH
    SYNTH --> GV
    FLASH -.lockout core1.-> UC
```

---

## 2. Startup sequence

```mermaid
sequenceDiagram
    participant main
    participant Card as UsbMidiHostCard
    participant Flash as config_store
    participant C1 as Core 1 usbCore

    main->>main: usbRoleRegisterHostCallbacks()
    main->>main: set_sys_clock_khz(200000)
    main->>Card: constructor
    Card->>Card: voicesInit() — init g_midiCs
    Card->>Card: initLuts() — synth wavetables
    Card->>Card: drumsInit()
    Card->>Flash: loadConfigFromFlash()
    Flash->>Flash: applyDefaults() then overlay flash if valid
    Flash->>Flash: sanitizeExtConfig() + legacy engine migrate
    Card->>Card: loadConfigFromFlash()
    Card->>C1: multicore_launch_core1(core1Entry)
    Card->>Card: Run() — never returns

    C1->>C1: multicore_lockout_victim_init()
    C1->>C1: sleep 150 ms
    C1->>C1: chooseUsbRole() via USBPowerState()
    alt USB Host (DFP)
        C1->>C1: tuh_init()
    else USB Device (UFP)
        C1->>C1: tud_init()
    end
    loop forever
        C1->>C1: tuh_task / tud_task + MIDI + SysEx
    end
```

**Source files:** `main.cpp`, `config_store.cpp`, `voices.cpp`, `synth.cpp`,
`drums.cpp`, `usb_role.cpp`.

---

## 3. USB role selection

```mermaid
flowchart LR
    PS[USBPowerState pin] --> CHK{Power state?}
    CHK -->|UFP or Unsupported| DEV[g_isUsbHost = false<br/>TinyUSB Device]
    CHK -->|DFP| HOST[g_isUsbHost = true<br/>TinyUSB Host]

    DEV --> ED[Editor / panel over USB MIDI IN]
    HOST --> EM[External MIDI controller plugged in]

    EM --> MOUNT[tuh_midi_mount_cb<br/>g_midiConnected = true]
    EM --> RX[tuh_midi_rx_cb → parseMidiStream]
    EM --> UNMOUNT[tuh_midi_umount_cb<br/>forcePlayModeCleanup]

    ED --> DRAIN[drainDeviceMidiRx]
    DRAIN --> PARSE[parseDeviceMidiBytes]
    PARSE --> STD[parseMidiByte]
    PARSE --> SX[SysEx F0…F7 → sysexProcessIncoming]
```

In **host mode**, MIDI arrives from a connected controller via TinyUSB host
callbacks (`usb_role.cpp`, `usb_midi_host.c`).

In **device mode**, the Workshop Computer presents as a USB MIDI device to a
host PC running `web/index.html`. Incoming bytes are drained in the Core 1 loop
and never parsed re-entrantly during TX (`main.cpp`).

---

## 4. MIDI ingress and routing

```mermaid
flowchart TD
    BYTE[parseMidiByte] --> ASM[Assemble running-status message]
    ASM --> HCM[handleChannelMessage]

    HCM --> MODE{g_appMode?}

    MODE -->|SETUP + Note On| LEARN_N[learnToSlot kSrcNote<br/>applySlotValue]
    MODE -->|SETUP + CC| LEARN_C[learnToSlot kSrcCc<br/>applySlotValue]

    MODE -->|PLAY + Note| NOTE{Note type?}
    NOTE -->|Ch10 pad 36–43| DRUM[drumNoteOn/Off<br/>under g_midiCs]
    NOTE -->|Ch A or B| VOICE[MonoVoice stack<br/>→ g_note/gate A/B<br/>→ polyNoteOn/Off]
    NOTE -->|Note matches slot map| SLOT_N[applySlotValue for slot]

    MODE -->|PLAY + CC| CC{Special CC?}
    CC -->|120/123 All off| SILENCE[silence voices + drums]
    CC -->|mapped slot| SLOT_C[applySlotValue<br/>voice/ADSR/cutoff/PWM]

    MODE -->|PLAY + Pitch bend| BEND[g_pitchBendA/B]

    SLOT_C --> VOICE_MAP[mapCcVoice → mapCcToVoiceId<br/>g_ext.audioVoice 0–120]
    SLOT_C --> PARAMS[g_ext attack/decay/sustain/…]
```

**13 parameter slots** (`protocol.h`): Ch A, Ch B, bend range, voice (CC 24
default), ADSR, cutoff, PWM — each mapped to CC,
Note, Knob X, or Knob Y via `ExtConfig.slots[]`.

**Source files:** `midi_parser.cpp`, `param_maps.cpp`, `voices.cpp`,
`drums.cpp`.

---

## 5. Audio ISR — `ProcessSample()` pipeline

```mermaid
flowchart TD
    START[ProcessSample @ 48 kHz] --> SF[serviceFlashSaveRequest]
    SF --> BOOT[Boot hold Z Down → factoryResetLatch]
    BOOT --> PANEL[Sample knobs → g_panelMain/X/Y<br/>g_panelSwitch]
    PANEL --> KM{SETUP mode?}
    KM -->|No| KMAP[applyKnobMappedSlots<br/>X/Y → mapped slots]
    KM -->|Yes| SKIP_K[skip knob maps]
    KMAP --> SC[serviceSetupControls]
    SKIP_K --> SC

    SC --> READ[Read g_note/gate/bend<br/>NO g_midiCs lock]
    READ --> SETUP_GATE{SETUP?}
    SETUP_GATE -->|Yes| MUTE[gateA/B forced false]
    SETUP_GATE -->|No| LIVE[use live gates]

    MUTE --> CV[writeMidiCv + PulseOut1/2]
    LIVE --> CV

    CV --> ADSR_CHK{ADSR enabled?<br/>A=D=R=0 S=127 → clickless}
    ADSR_CHK --> PLAY{PLAY mode?}
    PLAY -->|No| ZERO[Audio = 0]
    PLAY -->|Yes| MIX[For each poly voice]

    MIX --> ENV[envTick or clickless amp fade]
    ENV --> RENDER[renderPolyVoiceAudio]
    RENDER --> DECODE[decodeVoiceMatrix voiceId<br/>row = id/11 col = id%11]
    DECODE --> VM[renderVoiceMatrix<br/>waveform rows R0–R10 × cols C0–C10]
    VM --> SUM[Mix L/R ÷ kPolyMax]
    SUM --> DRUMS[drumsRenderMix]
    DRUMS --> CLIP[Clamp ±2048]
    CLIP --> OUT[AudioOut1/2]

    ZERO --> OUT
    OUT --> LEDS[Mode flash / glyph / status LEDs]
```

**Voice matrix render path:**

```mermaid
flowchart LR
    VID[g_ext.audioVoice 0–120] --> DEC[decodeVoiceMatrix]
    DEC --> ROW[row 0–10]
    DEC --> COL[col 0–11]
    ROW --> R8{R8–R10 special?}
    R8 -->|R8| FM[renderFmBell]
    R8 -->|R9| ORG[renderOrgan]
    R8 -->|R10| NS[renderNoiseHybrid]
    R8 -->|R0–R7| WAVE[renderWaveMatrix<br/>osc stacks + FX columns]
    FM --> OUT[L/R sample]
    ORG --> OUT
    NS --> OUT
    WAVE --> OUT
```

**Source files:** `main.cpp`, `adsr.cpp`, `synth.cpp`, `voice_matrix.cpp`,
`drums.cpp`.

---

## 6. SysEx editor protocol (device mode)

```mermaid
flowchart TD
    RX[Host sends F0 7D 79 … F7] --> SP[sysexProcessIncoming]
    SP --> CMD{Command byte}

    CMD -->|01 Preview| PRE[applyConfigBytes → RAM]
    CMD -->|02 SaveFlash| SAV[applyConfigBytes + requestSaveToFlash]
    CMD -->|03 ReadConfig| RC[sysexSendConfig]
    CMD -->|04 CardId| ID[fw 0.10.0 + device 79]
    CMD -->|05 PanelState| PS[sendPanelSnapshot]
    CMD -->|06 PanelStream| PST[enable g_panelStream<br/>periodic sendPanelState]
    CMD -->|07 ReadMaps| RM[sysexSendMapsReply ExtConfig]
    CMD -->|08 WriteMaps| WM[sanitize + save]
    CMD -->|09 SetPerf| PERF[voice/ADSR/cutoff/PWM RAM]

    SP --> TX[sendSysEx → midiStreamWriteMessage<br/>or enqueue txq if busy]
    TX --> FL[flushSysExQueue in usbCore loop]
```

**Panel streaming:** Core 0 updates `g_panel*` every sample; Core 1 sends
`kCmdPanelState` SysEx (throttled / on change) back to `web/index.html`.

**Source files:** `sysex_editor.cpp`, `main.cpp`, `protocol.h`.

---

## 7. SETUP mode state machine

```mermaid
stateDiagram-v2
    [*] --> Play
    Play --> Setup: Hold Z Down ~1 s
    Setup --> Play: Z Down again (no save)
    Setup --> Play: Switch Up → Middle (save flash + exit)

    state Setup {
        [*] --> SelectSlot
        SelectSlot --> SelectSlot: Main knob dwell → g_setupSlot
        SelectSlot --> LearnCC: incoming CC
        SelectSlot --> LearnNote: incoming Note On
        SelectSlot --> LearnKnob: X/Y gesture > threshold
        LearnCC --> SelectSlot: slot updated + LearnNotify SysEx
        LearnNote --> SelectSlot
        LearnKnob --> SelectSlot
    }

    note right of Play
        applyKnobMappedSlots active
        MIDI → voices + drums
        Audio rendered
    end note

    note right of Setup
        MIDI notes/CCs → learn only
        Gates muted, no audio
        Main knob picks slot 0–12
    end note
```

**Source files:** `main.cpp` (`serviceSetupControls`), `param_maps.cpp`.

---

## 8. Flash persistence

```mermaid
sequenceDiagram
    participant Editor as Core 1 / Editor
    participant Req as requestSaveToFlash
    participant ISR as Core 0 ProcessSample
    participant Flash as hardware flash

    Editor->>Req: requestSaveToFlash(ackCmd)
    Req->>Req: fillFlashPageBuf(g_config + g_ext)
    Req->>Req: g_flashSaveReq = true

    loop each audio sample
        ISR->>ISR: serviceFlashSaveRequest()
        alt save pending + core1 lockout ready
            ISR->>ISR: multicore_lockout_start_blocking()
            ISR->>Flash: erase + program last sector
            ISR->>ISR: multicore_lockout_end_blocking()
            ISR->>ISR: g_flashSaveAckPending = true
        end
    end

    Core 1->>Core 1: send SysEx ack (SaveFlash / WriteMaps)
```

Flash writes run from the audio ISR but **pause Core 1** via multicore lockout
so erase/program does not race USB. Config lives in the last flash sector
(`config_store.cpp`).

---

## 9. Synchronization rules

```mermaid
flowchart LR
    subgraph Core1_writes["Core 1 — may lock g_midiCs"]
        W1[polyNoteOn/Off]
        W2[silenceAllVoices]
        W3[drumNoteOn/Off]
    end

    subgraph Core0_reads["Core 0 — never locks g_midiCs"]
        R1[Read g_note/gate/bend volatiles]
        R2[Render g_poly unlocked]
        R3[envTick on voice state]
    end

    Core1_writes --> GV[(g_poly + mono stacks)]
    Core0_reads --> GV
```

Core 1 holds `g_midiCs` only briefly while mutating voice allocation. Core 0
reads MIDI state from volatiles and renders poly voices **without** taking the
lock — spinning on the lock in `ProcessSample` would stall the ADC knob mux.

---

## Source file map

| File | Responsibility |
|------|----------------|
| `main.cpp` | Bootstrap: clock, host callbacks, construct card, `Run()` |
| `app/card.cpp` | `UsbMidiHostCard` ctor, `ProcessSample`, CV output |
| `app/panel_setup.cpp` | SETUP toggle, knob learn slot select, factory-reset latch |
| `usb/usb_core1.cpp` | Core 1 USB loop, role select, flash-save ack |
| `usb/usb_device_midi.cpp` | Device-mode SysEx TX queue, panel stream, RX parse |
| `app/runtime_state.cpp` | Cross-core volatile globals |
| `app/config_store.cpp` | Flash load/save, defaults, sanitization |
| `app/midi_parser.cpp` | Byte stream → channel messages |
| `app/param_maps.cpp` | Slot maps, knob learn, `handleChannelMessage` |
| `app/sysex_editor.cpp` | Editor SysEx command dispatch |
| `usb/usb_role.cpp` | USB host MIDI callbacks |
| `dsp/voices.cpp` | Mono/poly voice allocation, `g_midiCs` |
| `dsp/voice_matrix.cpp` | CC → row/col decode, legacy migrate |
| `dsp/synth.cpp` | Oscillator matrix renderer |
| `dsp/adsr.cpp` | Envelope per voice |
| `dsp/drums.cpp` | Ch10 drum kit |
| `app/glyph_leds.cpp` | Stroke digit LED animations |
| `protocol.h` | SysEx commands, slots, config structs |
| `sysex_spec.json` | Machine-readable editor/firmware contract |
| `web/index.html`, `web/app.js`, `web/styles.css` | Browser editor UI |
