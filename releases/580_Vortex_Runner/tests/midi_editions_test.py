#!/usr/bin/env python3
"""Compile the actual MIDI dispatch methods in both firmware configurations."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'CS80.cpp').read_text()


def method(name):
    match = re.search(r'^    void ' + name + r'\([^;{}]*\)\s*\n    \{', source, re.M)
    assert match, name
    i = source.index('{', match.start())
    j, depth = i + 1, 1
    while depth:
        depth += (source[j] == '{') - (source[j] == '}')
        j += 1
    return source[match.start():j]


constants = '\n'.join(re.findall(r'^static constexpr uint8_t Midi\w+ = .*$', source, re.M))
button_check = re.search(r'static bool isEightMuButtonNote\([^}]+\}', source).group(0)
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
''' + constants + '\n#if VORTEX_ENABLE_8MU\n' + button_check + r'''
#endif
struct Card {
    enum class MidiEventType { NoteOn, NoteOff, PitchBend };
    struct MidiEvent { MidiEventType type; uint8_t note; int16_t bend; };
    struct Queue {
        std::vector<MidiEvent> events;
        void tryPush(MidiEvent e) { events.push_back(e); }
    } midiEvents;
#if VORTEX_ENABLE_8MU
    struct Controller { bool connected = false; bool Connected() { return connected; } } eightMu;
#endif
    uint8_t midiRunningStatus = 0, midiData[2] = {}, midiDataCount = 0;
    int standardCc = -1, fader = -1, bends = 0;
    void handleStandardMidiControlChange(uint8_t cc, uint8_t) { standardCc = cc; }
#if VORTEX_ENABLE_8MU
    void handleEightMuFader(uint8_t index, uint8_t) { fader = index; }
#endif
    void handleMidiPitchBend(uint8_t, uint8_t) { ++bends; }
    void message(uint8_t status, uint8_t a, uint8_t b) {
        processMidiVoiceByte(status); processMidiVoiceByte(a); processMidiVoiceByte(b);
    }
''' + method('processMidiVoiceByte') + '\n' + method('handleMidiControlChange') + r'''
};
int main() {
    for (bool connected : {false, true}) {
        Card c;
#if VORTEX_ENABLE_8MU
        c.eightMu.connected = connected;
#endif
        for (uint8_t note : {36, 48, 60, 72}) {
            auto before = c.midiEvents.events.size();
            c.message(0x90, note, 127); c.message(0x80, note, 0);
            c.message(0x90, note, 0);
#if VORTEX_ENABLE_8MU
            if (connected) { assert(c.midiEvents.events.size() == before); continue; }
#else
            (void)connected;
#endif
            assert(c.midiEvents.events.size() == before + 3);
            assert(c.midiEvents.events[before].type == Card::MidiEventType::NoteOn);
            assert(c.midiEvents.events[before + 1].type == Card::MidiEventType::NoteOff);
            assert(c.midiEvents.events[before + 2].type == Card::MidiEventType::NoteOff);
        }
        auto before = c.midiEvents.events.size();
        c.message(0x90, 61, 100); c.processMidiVoiceByte(62); c.processMidiVoiceByte(100);
        assert(c.midiEvents.events.size() == before + 2); // Ordinary notes and running status.
        for (uint8_t cc = 34; cc < 42; ++cc) {
            c.standardCc = c.fader = -1; c.message(0xB0, cc, 64);
#if VORTEX_ENABLE_8MU
            if (connected) { assert(c.fader == cc - 34 && c.standardCc == -1); continue; }
#endif
            assert(c.standardCc == cc && c.fader == -1);
        }
        c.message(0xB0, 20, 64); assert(c.standardCc == 20);
        c.message(0xE0, 0, 64); assert(c.bends == 1);
    }
    std::puts(VORTEX_ENABLE_8MU ? "PASS: 8mu note protection and fader routing; ordinary MIDI retained."
                              : "PASS: standard notes and CCs remain ordinary MIDI without 8mu takeover.");
}
'''
with tempfile.TemporaryDirectory(prefix='vortex-midi-test-') as directory:
    cpp = Path(directory) / 'midi_test.cpp'
    cpp.write_text(code)
    for enabled in [0, 1]:
        binary = Path(directory) / f'midi_test_{enabled}'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        f'-DVORTEX_ENABLE_8MU={enabled}', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
