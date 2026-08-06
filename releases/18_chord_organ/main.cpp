/**
 * Chord Organ - Workshop Computer program card
 * Replicates Music Thing Modular Chord-Organ: 16 chords, 8 voices, 1V/oct root.
 * Pico SDK + ComputerCard, 48 kHz.
 *
 * Controls:
 * - Main Knob: Chord selection (16 chords)
 * - Knob X: Root note transpose (+4 octaves)
 * - Knob Y: Progression pattern selection (9 patterns)
 * - CV In 1: Chord selection CV (summed with Main Knob)
 * - CV In 2: Root note 1V/oct tracking (0V = middle C)
 * - Audio In 1: VCA control (0V to +5V for volume 0% to 100%, full volume when disconnected)
 * - Pulse In 1: Trigger - advances progression step & retriggers chord
 * - Pulse In 2: Waveform cycle trigger
 *
 * Outputs:
 * - Audio Out 1 & 2: Mixed chord output
 * - CV Out 1: Highest note in current chord (1V/oct)
 * - CV Out 2: Sequenced note from progression, based off middle C (1V/oct)
 * - Pulse Out 1: Trigger on chord/root change
 */

#include "ComputerCard.h"
#include "chords.h"
#include "lookup_tables.h"
#include "progressions.h"
#include <cstdint>
#include <cstdlib>

#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

// Hysteresis for chord/root to avoid jitter (knob 0-4095)
constexpr int32_t kChangeTolerance = 64;

// CV input: -2048..2047 maps to voltage. 1V/oct: ~341 counts per octave.
// 0V (CVIn2 = 0) corresponds to middle C (MIDI note 60).
constexpr int32_t kCountsPerOctave = 341;

// Reset pulse length in samples (48 kHz): ~10 ms
constexpr uint32_t kResetPulseSamples = 480;

// Glide: 50 ms default = 2400 samples. Linear ramp of phase increment.
constexpr uint32_t kGlideSamples = 2400;

// Stack detune: voices 5-8 = 1-4 at 1.001x (fixed-point 1001/1000)
constexpr uint32_t kStackScaleNum = 1001;
constexpr uint32_t kStackScaleDen = 1000;

class ChordOrganCard : public ComputerCard {
public:
    ChordOrganCard() : chordQuant(0), rootQuant(60),
                       chordRawSmoothed(2048), rootPotSmoothed(2048), rootCVSmoothed(0),
                       resetPulseCount(0), glideSamplesLeft(0), waveformIndex(0),
                       stackedMode(false), glideEnabled(true), changed(false),
                       progressionQuant(0), progressionStep(0), knobYSmoothed(2048) {
        EnableNormalisationProbe();  // Enable jack detection
        for (int i = 0; i < kMaxVoices; i++) {
            phase[i] = 0;
            phaseInc[i] = MIDI_PHASE_INC[60];
            phaseIncTarget[i] = MIDI_PHASE_INC[60];
            active[i] = false;
        }
        updateTargetsFromChord();
        for (int i = 0; i < kMaxVoices; i++)
            phaseInc[i] = phaseIncTarget[i];
    }

protected:
    void ProcessSample() override {
        // --- Read controls (with hysteresis) ---
        int32_t mainKnob = KnobVal(Knob::Main);
        int32_t knobX = KnobVal(Knob::X);
        int32_t knobY = KnobVal(Knob::Y);
        int32_t cv1 = CVIn1();
        int32_t cv2 = CVIn2();
        int32_t audio1 = AudioIn1();

        // Progression selection: Knob Y (0..4095 -> 0..8 for 9 patterns)
        knobYSmoothed += (knobY - knobYSmoothed) >> 5;
        int32_t newProgressionQuant = (knobYSmoothed * kProgressionCount) >> 12;
        if (newProgressionQuant >= kProgressionCount) newProgressionQuant = kProgressionCount - 1;

        // Only change progression if knob moved significantly (hysteresis to prevent jitter)
        int32_t progressionRaw = (progressionQuant << 12) / kProgressionCount;
        if (abs(knobYSmoothed - progressionRaw) > kChangeTolerance) {
            if (newProgressionQuant != progressionQuant) {
                progressionQuant = newProgressionQuant;
                progressionStep = 0;  // Reset to start when changing progression
            }
        }

        // Chord selection: Main knob (0..4095) with optional CV1 (0V to +5V)
        int32_t chordRaw;
        if (Disconnected(Input::CV1)) {
            // Knob only: full range 0-4095 → chords 0-15
            chordRaw = mainKnob;
        } else {
            // CV1 connected: both knob and CV can independently access all chords
            // Scale CV1 (0 to +2047 for 0V to +5V) to match knob range (0-4095)
            int32_t cv1Scaled = cv1;
            if (cv1Scaled < 0) cv1Scaled = 0;        // Ignore negative voltages
            cv1Scaled = (cv1Scaled * 4095) / 2047;   // Scale 0-2047 to 0-4095

            // Add knob + CV, clamp to max
            chordRaw = mainKnob + cv1Scaled;
            if (chordRaw > 4095) chordRaw = 4095;
        }

        // VCA control: Audio In 1 (0V to +5V) controls overall volume
        // If no cable plugged in, bypass VCA (full volume)
        // audio1 range: -2048 to +2047 (-5V to +5V approx)
        // Map 0V to +5V (audio1: 0 to +2047) to volume 0.0 to 1.0
        int32_t volumeCV;
        if (Disconnected(Input::Audio1)) {
            volumeCV = 2047;  // Full volume when nothing connected
        } else {
            volumeCV = audio1;
            if (volumeCV < 0) volumeCV = 0;      // Below 0V = silence
            if (volumeCV > 2047) volumeCV = 2047; // Clamp at +5V
        }

        chordRawSmoothed += (chordRaw - chordRawSmoothed) >> 5;
        int32_t newChordQuant = (chordRawSmoothed * kChordCount) >> 12;
        if (newChordQuant >= kChordCount) newChordQuant = kChordCount - 1;
        if (newChordQuant != chordQuant) {
            chordQuant = newChordQuant;
            changed = true;
        }

        int32_t rootPot = (knobX < 0) ? 0 : (knobX > 4095 ? 4095 : knobX);

        rootPotSmoothed += (rootPot - rootPotSmoothed) >> 5;
        int32_t cvDiff = cv2 - rootCVSmoothed;
        int32_t cvStep = cvDiff >> 5;
        if (cvStep == 0 && cvDiff != 0) cvStep = (cvDiff > 0) ? 1 : -1;
        rootCVSmoothed += cvStep;

        // 1V/oct: 0V (cv2=0) = middle C (MIDI 60), ~341 counts per octave
        // Round to nearest semitone (341/12 ≈ 28.4 counts per semitone)
        int32_t semitones = rootCVSmoothed * 12;
        int32_t rootCVQuant = 60 + (semitones + (semitones >= 0 ? kCountsPerOctave / 2 : -kCountsPerOctave / 2)) / kCountsPerOctave;
        int32_t rootPotQuant = (rootPotSmoothed * 48) >> 12;
        int32_t newRootQuant = rootCVQuant + rootPotQuant;
        if (newRootQuant > 127) newRootQuant = 127;
        if (newRootQuant < 0) newRootQuant = 0;
        if (newRootQuant != rootQuant) {
            rootQuant = newRootQuant;
            changed = true;
        }

        if (PulseIn1RisingEdge()) {
            changed = true;  // Retrigger chord

            // Advance sequencer (all progressions are 8 steps)
            progressionStep = (progressionStep + 1) & 7;
        }

        // Switch controls glide mode and waveform cycling
        if (SwitchChanged()) {
            Switch sw = SwitchVal();
            if (sw == Switch::Up)
                glideEnabled = true;
            else if (sw == Switch::Middle)
                glideEnabled = false;
            else if (sw == Switch::Down)
                waveformIndex = (waveformIndex + 1) & 3;
        }

        if (PulseIn2RisingEdge())
            waveformIndex = (waveformIndex + 1) & 3;

        if (changed) {
            updateTargetsFromChord();
            glideSamplesLeft = glideEnabled ? kGlideSamples : 0;
            resetPulseCount = kResetPulseSamples;
            changed = false;
        }

        // --- Glide phase increments toward target ---
        if (glideSamplesLeft > 0) {
            for (int i = 0; i < kMaxVoices; i++) {
                if (!active[i]) continue;
                int32_t delta = (int32_t)(phaseIncTarget[i] - phaseInc[i]);
                phaseInc[i] += delta / (int32_t)glideSamplesLeft;
            }
            glideSamplesLeft--;
        } else {
            for (int i = 0; i < kMaxVoices; i++)
                phaseInc[i] = phaseIncTarget[i];
        }

        // CV Out 2: Sequenced note from progression (based off middle C)
        constexpr int kMiddleC = 60;
        int8_t rootOffset = kProgressions[progressionQuant][progressionStep];
        int sequencedRootNote = kMiddleC + rootOffset;

        // Clamp to MIDI range
        if (sequencedRootNote > 127) sequencedRootNote = 127;
        if (sequencedRootNote < 0) sequencedRootNote = 0;

        CVOut2MIDINote(sequencedRootNote);

        // --- Oscillators: phase += inc, lookup sine, scale by amp, sum ---
        int32_t mix = 0;
        int voiceCount = 0;
        for (int i = 0; i < kMaxVoices; i++) {
            if (!active[i]) continue;
            voiceCount++;
            phase[i] += (uint32_t)phaseInc[i];
            int32_t sample = waveformSample(phase[i]);
            int32_t amp = (voiceCount <= 8) ? kAmpPerVoice[voiceCount - 1] : kAmpPerVoice[7];
            mix += (sample * amp) >> 8;
        }

        if (voiceCount == 0)
            voiceCount = 1;
        mix = mix >> 2;
        if (mix > 2047) mix = 2047;
        if (mix < -2048) mix = -2048;

        // Apply VCA: volumeCV is 0..2047, scale mix proportionally
        mix = (mix * volumeCV) >> 11;  // Divide by 2048 (shift by 11 bits)

        AudioOut1((int16_t)mix);
        AudioOut2((int16_t)mix);

        // --- CV Outputs ---
        // CV Out 1: Highest note in current chord
        int highestNote = rootQuant;
        for (int i = 0; i < kMaxVoices; i++) {
            if (active[i]) {
                const int8_t* chord = kChordNotes[chordQuant];
                int note = rootQuant + chord[i];
                if (note > highestNote) highestNote = note;
            }
        }
        if (highestNote > 127) highestNote = 127;

        CVOut1MIDINote(highestNote);

        if (resetPulseCount > 0) {
            PulseOut1(true);
            LedOn(4, true);
            resetPulseCount--;
        } else {
            PulseOut1(false);
            LedOn(4, false);
        }

        for (int i = 0; i < 4; i++)
            LedOn(i, (chordQuant >> i) & 1);

        LedBrightness(5, waveformIndex * 1024);
    }

private:
    int32_t chordQuant;
    int32_t rootQuant;
    int32_t chordRawSmoothed, rootPotSmoothed, rootCVSmoothed;
    uint32_t phase[kMaxVoices];
    int32_t phaseInc[kMaxVoices];
    int32_t phaseIncTarget[kMaxVoices];
    bool active[kMaxVoices];
    uint32_t resetPulseCount;
    uint32_t glideSamplesLeft;
    int waveformIndex;
    bool stackedMode;
    bool glideEnabled;
    bool changed;

    // Sequencer state
    int32_t progressionQuant;    // Current progression pattern (0-9)
    int32_t progressionStep;     // Current step in progression (0-7)
    int32_t knobYSmoothed;       // Smoothed Knob Y value

    void updateTargetsFromChord() {
        const int8_t* chord = kChordNotes[chordQuant];
        for (int i = 0; i < kMaxVoices; i++) {
            if (chord[i] < 0) {
                active[i] = false;
                continue;
            }
            active[i] = true;
            int note = rootQuant + chord[i];
            if (note < 0) note = 0;
            if (note > 127) note = 127;
            phaseIncTarget[i] = (int32_t)MIDI_PHASE_INC[note];
        }
        if (stackedMode) {
            for (int i = 0; i < 4; i++) {
                if (active[i]) {
                    active[i + 4] = true;
                    phaseIncTarget[i + 4] = (phaseIncTarget[i] * (int32_t)kStackScaleNum) / (int32_t)kStackScaleDen;
                } else {
                    active[i + 4] = false;
                }
            }
        }
    }

    inline int32_t __not_in_flash_func(sineLookup)(uint32_t ph) {
        uint32_t index = ph >> 23;
        uint32_t frac = (ph & 0x7FFFFF) >> 7;
        int32_t s1 = SINE_TABLE[index & 0x1FF];
        int32_t s2 = SINE_TABLE[(index + 1) & 0x1FF];
        return (s2 * (int32_t)frac + s1 * (int32_t)(65536 - frac)) >> 16;
    }

    // Triangle: ±32000, same scale as sine. Phase 0..2^32 = one period, 4 linear segments.
    inline int32_t __not_in_flash_func(triangleLookup)(uint32_t ph) {
        uint32_t q = ph >> 30;
        uint32_t frac = (ph & 0x3FFFFFFFU) * 32000U >> 30;
        if (q == 0) return (int32_t)frac;
        if (q == 1) return (int32_t)(32000 - frac);
        if (q == 2) return -(int32_t)frac;
        return (int32_t)(frac - 32000);
    }

    // Square: +32000 first half cycle, -32000 second half.
    inline int32_t __not_in_flash_func(squareLookup)(uint32_t ph) {
        return (ph < 0x80000000U) ? 32000 : -32000;
    }

    // Saw: ramp -32000 to +32000 over one period (phase 0..2^32).
    inline int32_t __not_in_flash_func(sawLookup)(uint32_t ph) {
        int32_t x = (int32_t)((ph >> 16) - 32768);
        return (x * 1000) >> 10;
    }

    inline int32_t __not_in_flash_func(waveformSample)(uint32_t ph) {
        switch (waveformIndex) {
            case 1: return triangleLookup(ph);
            case 2: return squareLookup(ph);
            case 3: return sawLookup(ph);
            default: return sineLookup(ph);
        }
    }
};

int main() {
    ChordOrganCard card;
    card.Run();
    return 0;
}
