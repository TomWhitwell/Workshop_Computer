#include "resonator.h"

// --- Hot-path methods (same TU as ProcessSample for inlining) ---

void ResonatingStrings::updateNeedsFlags() {
    needsArpMV = (cv1Mode == CVOUT_ARP || cv2Mode == CVOUT_ARP);
    needsRootMV = needsArpMV || cv1Mode == CVOUT_ROOT || cv2Mode == CVOUT_ROOT;
    needsResEnv = (cv1Mode == CVOUT_RES_ENV || cv2Mode == CVOUT_RES_ENV || ao2Mode == AO2_RES_ENV);
    needsInEnv = (cv1Mode == CVOUT_IN_ENV || cv2Mode == CVOUT_IN_ENV || ao2Mode == AO2_IN_ENV);
    needsPitchTrack = (cv1Mode == CVOUT_PITCH_TRACK || cv2Mode == CVOUT_PITCH_TRACK);
    needsAudioTrig = (p1Mode == P1_AUDIO_TRIG || p2Mode == P2_AUDIO_TRIG);
    needsOnset = (p1Mode == P1_ONSET || p2Mode == P2_ONSET);
    needsTapClock = (p1Mode == P1_TAP_CLOCK || p2Mode == P2_TAP_CLOCK);
    needsArpClock = (p1Mode == P1_ARP_CLOCK || p2Mode == P2_ARP_CLOCK);
    needsClkDiv = (p2Mode == P2_CLK_DIV);
    needsChordDetect = needsArpMV || needsArpClock || needsTapClock ||
                       p2Mode == P2_CHORD_TRIG || cv1Mode == CVOUT_RANDOM_SH || cv2Mode == CVOUT_RANDOM_SH;
    needsAudioAbs = needsAudioTrig || needsOnset || needsInEnv;
}

// One-pole lowpass filter for damping
int32_t ResonatingStrings::dampingFilter(int32_t input, int32_t& state, int32_t coefficient) {
    state += (((input - state) * coefficient + 32768) >> 16);
    return state;
}

// Map arpRotation to a 0-3 string index based on arpPattern
int ResonatingStrings::arpStringIndex() {
    switch (arpPattern) {
        default:
        case 0: // up: 0,1,2,3
            return arpRotation & 3;
        case 1: // down: 3,2,1,0
            return 3 - (arpRotation & 3);
        case 2: { // up-down: 0,1,2,3,2,1 (period 6, extended to 8 for & mask)
            static const int updown[] = {0, 1, 2, 3, 2, 1, 0, 1};
            return updown[arpRotation & 7];
        }
        case 3: // random: cached, updated on arp step
            return arpRandomString;
        case 4: { // pedal: root alternates with each chord tone (0,1,0,2,0,3,...)
            static const int pedal[] = {0, 1, 0, 2, 0, 3, 0, 2};
            return pedal[arpRotation & 7];
        }
        case 5: // random walk: cached (steps +-1), updated on arp step
            return arpRandomString;
    }
}

// Update cached random string index: pattern 5 walks +-1 (reflecting at the edges),
// otherwise it's a fresh random tone. Called on each arp step / chord change.
void ResonatingStrings::stepArpRandom() {
    noiseState = noiseState * 1103515245 + 12345;
    if (arpPattern == 5) {
        if (noiseState & 0x10000) arpRandomString = (arpRandomString < 3) ? arpRandomString + 1 : 2;
        else                      arpRandomString = (arpRandomString > 0) ? arpRandomString - 1 : 1;
    } else {
        arpRandomString = (noiseState >> 16) & 3;
    }
}

// Process one string with linear interpolation for fractional delay
int32_t ResonatingStrings::processString(int16_t* delayLine, int& writeIndex, int delayLength,
                     int32_t& filterState, int32_t& dcState, int32_t excitation,
                     int32_t dampingCoeff, int32_t frac) {
    // Read two adjacent samples from delay line
    int readIndex1 = (writeIndex - delayLength) & (MAX_DELAY_SIZE - 1);
    int readIndex2 = (readIndex1 - 1) & (MAX_DELAY_SIZE - 1);

    int32_t sample1 = delayLine[readIndex1];
    int32_t sample2 = delayLine[readIndex2];

    // Linear interpolation: blend based on fractional part (frac is 0-255)
    int32_t delayedSample = ((sample1 * (256 - frac)) + (sample2 * frac)) >> 8;

    int32_t dampedSample = dampingFilter(delayedSample, filterState, dampingCoeff);

    // DC blocker: remove DC offset to prevent accumulation
    dcState += (dampedSample - dcState) >> 8;
    dampedSample -= dcState;

    // Add excitation (input signal)
    int32_t newSample = dampedSample + excitation;

    // Soft clipping to prevent overflow
    if (newSample > 2047) newSample = 2047;
    if (newSample < -2047) newSample = -2047;

    // Write back to delay line
    delayLine[writeIndex] = (int16_t)newSample;

    // Advance write index
    writeIndex = (writeIndex + 1) & (MAX_DELAY_SIZE - 1);

    return delayedSample;
}

// --- Constructor ---

ResonatingStrings::ResonatingStrings() : writeIndex1(0), writeIndex2(0), writeIndex3(0), writeIndex4(0),
                      delayLength1(100), delayLength2(150), delayLength3(200), delayLength4(400),
                      filterState1(0), filterState2(0), filterState3(0), filterState4(0),
                      currentMode(HARMONIC), activeBuffer(0), progressionChanged(false), pendingFlashSave(false),
                      flashDirty(false), flashDirtyTime(0),
                      progressionIndex(0), lastSwitchDown(true), ledRR(0),
                      pulseExciteEnvelope(0), noiseState(12345),
                      dcState1(0), dcState2(0), dcState3(0), dcState4(0),
                      smoothDelay1(0), smoothDelay2(0), smoothDelay3(0), smoothDelay4(0),
                      switchDownCounter(0), resetTriggered(false),
                      arpRotation(0), envFollower(0), triggerArmed(true),
                      trigPulseCounter(0), prevProgressionIndex(0), chordPulseCounter(0),
                      chordPeriod(0), chordTimer(0), arpStepCounter(0), arpDivision(4), arpPattern(0), arpSettingsChanged(false), arpRandomString(0), arpLoop(false), rootString(0),
                      cv1Mode(CVOUT_ARP), cv2Mode(CVOUT_IN_ENV), p1Mode(P1_AUDIO_TRIG), p2Mode(P2_CHORD_TRIG),
                      pi1Mode(PI1_PLUCK), pi2Mode(PI2_ADVANCE), ao2Mode(AO2_AUDIO), ci1Mode(CI1_VOCT), ci2Mode(CI2_DAMPING), outputModesChanged(false),
                      clockCounter(0), tapClockPulseCounter(0),
                      randomSHValue(0), arpClockPulseCounter(0),
                      pitchSHValue(0), clockDivCount(0), clockDivRatio(2), clockDivPulseCounter(0),
                      inputEnvFollower(0),
                      onsetPeakEnv(0), onsetEnvelope(0), onsetPulseCounter(0) {
    // Try to load progression from flash, fall back to defaults
    if (!loadProgressionFromFlash()) {
        // Default progression: all chords (card works standalone without browser UI)
        const ChordMode allChords[] = {
            HARMONIC, FIFTH, MAJOR7, MINOR7, DIM, SUS4, ADD9, MAJOR10,
            SUS2, MAJOR, MINOR, MAJOR6, DOM7, MIN9,
            TANPURA_PA, TANPURA_MA, TANPURA_NI, TANPURA_NI_KOMAL
        };
        for (int i = 0; i < NUM_MODES; i++) {
            progressionBuffers[0].chords[i] = allChords[i];
            progressionBuffers[1].chords[i] = allChords[i];
        }
        progressionBuffers[0].length = NUM_MODES;
        progressionBuffers[1].length = NUM_MODES;
    }

    // Initialize delay lines with silence
    for (int i = 0; i < MAX_DELAY_SIZE; i++) {
        delayLine1[i] = 0;
        delayLine2[i] = 0;
        delayLine3[i] = 0;
        delayLine4[i] = 0;
    }

    updateNeedsFlags();
}

// --- ProcessSample (audio hot path) ---

void ResonatingStrings::ProcessSample() {
    int16_t audioIn1 = AudioIn1();
    int16_t audioIn2 = AudioIn2();
    int32_t audioIn = ((int32_t)audioIn1 + (int32_t)audioIn2 + 1) >> 1;

    // Check for serial progression update from Core 1
    if (progressionChanged) {
        __dmb();  // Ensure we see updated buffer contents after flag
        progressionIndex = 0;
        currentMode = progressionBuffers[activeBuffer].chords[0];
        progressionChanged = false;
    }

    // Recompute which output blocks to run when I/O modes change
    if (outputModesChanged) {
        __dmb();
        outputModesChanged = false;
        bool hadResEnv = needsResEnv;
        bool hadInEnv = needsInEnv;
        updateNeedsFlags();
        if (needsResEnv && !hadResEnv) envFollower = 0;
        if (needsInEnv && !hadInEnv) inputEnvFollower = 0;
    }

    // Mode switching (switch down or pulse in 2)
    // Long press (3 sec) resets to factory defaults
    Switch switchPos = SwitchVal();
    bool switchDown = (switchPos == Down);

    if (switchDown) {
        switchDownCounter++;

        // Long press detected - reset to defaults
        if (switchDownCounter >= RESET_HOLD_SAMPLES && !resetTriggered) {
            resetToDefaults();
            resetTriggered = true;
        }
    } else {
        // Switch released - advance chord only if it was a short press
        if (lastSwitchDown && !resetTriggered && switchDownCounter > 0) {
            int bufIdx = activeBuffer;
            progressionIndex = (progressionIndex + 1) % progressionBuffers[bufIdx].length;
            currentMode = progressionBuffers[bufIdx].chords[progressionIndex];
        }
        switchDownCounter = 0;
        resetTriggered = false;
    }

    // Pulse input handling (mode-based)
    bool pulseIn1Rising = PulseIn1RisingEdge();
    bool pulseIn2Rising = PulseIn2RisingEdge();

    // Pulse In 1 dispatch
    if (pulseIn1Rising) {
        switch (pi1Mode) {
            case PI1_PLUCK:
                pulseExciteEnvelope = 2048;
                break;
            case PI1_ADVANCE: {
                int bufIdx = activeBuffer;
                progressionIndex = (progressionIndex + 1) % progressionBuffers[bufIdx].length;
                currentMode = progressionBuffers[bufIdx].chords[progressionIndex];
                break;
            }
            case PI1_RESET:
                progressionIndex = 0;
                currentMode = progressionBuffers[activeBuffer].chords[0];
                break;
        }
    }

    // Pulse In 2 dispatch
    if (pulseIn2Rising) {
        switch (pi2Mode) {
            case PI2_ADVANCE: {
                int bufIdx = activeBuffer;
                progressionIndex = (progressionIndex + 1) % progressionBuffers[bufIdx].length;
                currentMode = progressionBuffers[bufIdx].chords[progressionIndex];
                break;
            }
            case PI2_ARP_STEP:
                arpRotation++;
                stepArpRandom();
                break;
            case PI2_PLUCK:
                pulseExciteEnvelope = 2048;
                break;
            case PI2_RESET:
                progressionIndex = 0;
                currentMode = progressionBuffers[activeBuffer].chords[0];
                break;
        }
    }

    lastSwitchDown = switchDown;

    // FREQUENCY CONTROL - 1V/oct
    // CV1: ±6V maps to -2048 to 2047
    int32_t pitchCV;
    int32_t stringPitch;

    if (Disconnected(Input::CV1)) {
        // No CV connected: X knob controls C1-C7 range
        // Map knob 0-4095 to pitchCV 2048-4095 (6 octaves)
        pitchCV = 2048 + (KnobVal(X) / 2);
        stringPitch = pitchCV;
    } else if (ci1Mode == CI1_VOCT) {
        // CV controls strings + outputs. 0V = middle C (pitchCV 3069), matching the
        // pitch CV outputs. X knob is fine tune (±1 octave), 1 octave = 341 steps.
        int32_t fineTune = ((KnobVal(X) - 2048) * 341) / 2048;
        int32_t scaledCV = CVIn1();
        pitchCV = 3069 + scaledCV + fineTune;
        stringPitch = pitchCV;
    } else {
        // CI1_ARP_ONLY: knob sets string pitch, CV sets output pitch (0V = middle C)
        stringPitch = 2048 + (KnobVal(X) / 2);
        pitchCV = 3069 + CVIn1();
    }

    if (stringPitch > 4095) stringPitch = 4095;
    if (stringPitch < 0) stringPitch = 0;
    if (pitchCV > 4095) pitchCV = 4095;
    if (pitchCV < 0) pitchCV = 0;

    // Get delay from exponential lookup table (1V/oct)
    int32_t baseDelay = ExpDelay(stringPitch);

    // Clamp to usable range
    const int MIN_DELAY = 15;
    const int MAX_DELAY = 1468;  // C1 at 32.7Hz
    if (baseDelay < MIN_DELAY) baseDelay = MIN_DELAY;
    if (baseDelay > MAX_DELAY) baseDelay = MAX_DELAY;

    // Get frequency ratios based on current chord mode
    int num1 = 1, den1 = 1, num2 = 2, den2 = 1, num3 = 3, den3 = 1, num4 = 4, den4 = 1;
    getFrequencyRatios(currentMode, num1, den1, num2, den2, num3, den3, num4, den4);

    // Calculate target delay lengths for each string using fixed-point math
    // delay = baseDelay * denominator / numerator
    // Use 8 extra bits of precision to extract fractional part for interpolation
    int32_t targetDelay1 = ((baseDelay * den1) << 8) / num1;
    int32_t targetDelay2 = ((baseDelay * den2) << 8) / num2;
    int32_t targetDelay3 = ((baseDelay * den3) << 8) / num3;
    int32_t targetDelay4 = ((baseDelay * den4) << 8) / num4;

    // Smooth delay transitions to avoid harsh plucks on chord changes
    // Initialize smoothDelay on first sample (when it's 0)
    if (smoothDelay1 == 0) smoothDelay1 = targetDelay1;
    if (smoothDelay2 == 0) smoothDelay2 = targetDelay2;
    if (smoothDelay3 == 0) smoothDelay3 = targetDelay3;
    if (smoothDelay4 == 0) smoothDelay4 = targetDelay4;

    // One-pole lowpass: smoothDelay approaches target
    const int32_t GLIDE_COEFF = 2;  // Lower = slower glide (~2 seconds)
    smoothDelay1 += ((targetDelay1 - smoothDelay1) * GLIDE_COEFF) >> 8;
    smoothDelay2 += ((targetDelay2 - smoothDelay2) * GLIDE_COEFF) >> 8;
    smoothDelay3 += ((targetDelay3 - smoothDelay3) * GLIDE_COEFF) >> 8;
    smoothDelay4 += ((targetDelay4 - smoothDelay4) * GLIDE_COEFF) >> 8;

    delayLength1 = smoothDelay1 >> 8;  // Integer part
    delayLength2 = smoothDelay2 >> 8;
    delayLength3 = smoothDelay3 >> 8;
    delayLength4 = smoothDelay4 >> 8;

    int32_t frac1 = smoothDelay1 & 0xFF;  // Fractional part (0-255)
    int32_t frac2 = smoothDelay2 & 0xFF;
    int32_t frac3 = smoothDelay3 & 0xFF;
    int32_t frac4 = smoothDelay4 & 0xFF;

    // Clamp to valid range
    if (delayLength1 < MIN_DELAY) delayLength1 = MIN_DELAY;
    if (delayLength2 < MIN_DELAY) delayLength2 = MIN_DELAY;
    if (delayLength3 < MIN_DELAY) delayLength3 = MIN_DELAY;
    if (delayLength4 < MIN_DELAY) delayLength4 = MIN_DELAY;
    if (delayLength1 > MAX_DELAY_SIZE - 1) delayLength1 = MAX_DELAY_SIZE - 1;
    if (delayLength2 > MAX_DELAY_SIZE - 1) delayLength2 = MAX_DELAY_SIZE - 1;
    if (delayLength3 > MAX_DELAY_SIZE - 1) delayLength3 = MAX_DELAY_SIZE - 1;
    if (delayLength4 > MAX_DELAY_SIZE - 1) delayLength4 = MAX_DELAY_SIZE - 1;

    // DAMPING CONTROL (Y Knob + optional CV2)
    int32_t cv2val = CVIn2();
    int32_t dampingKnob = KnobVal(Y) + (ci2Mode == CI2_DAMPING ? cv2val : 0);
    if (dampingKnob > 4095) dampingKnob = 4095;
    if (dampingKnob < 0) dampingKnob = 0;

    // Map to filter coefficient (more damping = lower coefficient, longer decay = higher coefficient)
    int32_t dampingCoeff = 32000 + ((dampingKnob * 33300) >> 12);

    // Excitation amounts for each string
    // String 1 gets full input, others get scaled versions (sympathetic response)
    int32_t excitation1 = audioIn >> 2;  // Direct excitation
    int32_t excitation2 = audioIn >> 4;  // Sympathetic response
    int32_t excitation3 = audioIn >> 4;  // Sympathetic response
    int32_t excitation4 = audioIn >> 3;  // 4th string

    // Apply decaying noise burst while envelope is active
    if (pulseExciteEnvelope > 10) {
        noiseState = noiseState * 1103515245 + 12345;
        int32_t noise = (int32_t)((noiseState >> 16) & 0xFFF) - 2048;
        int32_t scaledNoise = (noise * pulseExciteEnvelope) >> 11;
        excitation1 += scaledNoise;
        excitation2 += scaledNoise >> 1;
        excitation3 += scaledNoise >> 1;
        excitation4 += scaledNoise >> 1;
        // Fast decay for short pluck burst
        pulseExciteEnvelope = (pulseExciteEnvelope * 250) >> 8;
    }

    // Process each string with fractional delay interpolation
    int32_t out1 = processString(delayLine1, writeIndex1, delayLength1,
                                 filterState1, dcState1, excitation1, dampingCoeff, frac1);
    int32_t out2 = processString(delayLine2, writeIndex2, delayLength2,
                                 filterState2, dcState2, excitation2, dampingCoeff, frac2);
    int32_t out3 = processString(delayLine3, writeIndex3, delayLength3,
                                 filterState3, dcState3, excitation3, dampingCoeff, frac3);
    int32_t out4 = processString(delayLine4, writeIndex4, delayLength4,
                                 filterState4, dcState4, excitation4, dampingCoeff, frac4);

    // Mix strings together - stereo mid/side
    // Out1 (mid): all strings summed - mono compatible
    // Out2 (side): strings 1&3 center, strings 2&4 wide/diffuse
    int32_t resonatorOut1, resonatorOut2;
    if (SwitchVal() == Switch::Up) {
        // TUNING MODE: first string only
        resonatorOut1 = out1 / 4;
        resonatorOut2 = out1 / 4;
    } else {
        resonatorOut1 = (out1 + out2 + out3 + out4) / 4;
        resonatorOut2 = (out1 - out2 + out3 - out4) / 4;
    }

    resonatorOut1 *= 2;
    resonatorOut2 *= 2;

    // WET/DRY MIX (Main Knob + optional CV2)
    int32_t mixKnob = KnobVal(Main) + (ci2Mode == CI2_MIX ? cv2val : 0);
    if (mixKnob > 4095) mixKnob = 4095;
    if (mixKnob < 0) mixKnob = 0;

    int32_t dryGain = 4095 - mixKnob;
    int32_t wetGain = mixKnob;

    int32_t mixedOutput1 = ((audioIn * dryGain) + (resonatorOut1 * wetGain) + 2048) >> 12;
    int32_t mixedOutput2 = ((audioIn * dryGain) + (resonatorOut2 * wetGain) + 2048) >> 12;

    // Clipping
    if (mixedOutput1 > 2047) mixedOutput1 = 2047;
    if (mixedOutput1 < -2047) mixedOutput1 = -2047;
    if (mixedOutput2 > 2047) mixedOutput2 = 2047;
    if (mixedOutput2 < -2047) mixedOutput2 = -2047;

    // Audio output (stereo when AO2 is audio, mono when AO2 is CV)
    AudioOut1((int16_t)mixedOutput1);
    if (ao2Mode == AO2_AUDIO) {
        AudioOut2((int16_t)mixedOutput2);
    }

    // --- CV and Pulse outputs (only compute what the current I/O config needs) ---

    bool chordChanged = false;
    bool arpStepped = false;

    // Chord detection, arp stepping, and related state
    if (needsChordDetect) {
        chordTimer++;
        if (arpSettingsChanged) {
            arpSettingsChanged = false;
            arpRotation = 0;
            if (chordPeriod > 0) {
                arpStepCounter = chordPeriod / arpDivision;
            }
        }

        chordChanged = (progressionIndex != prevProgressionIndex);

        if (chordChanged) {
            chordPulseCounter = 240;  // 5ms at 48kHz
            chordPeriod = chordTimer;
            chordTimer = 0;
            prevProgressionIndex = progressionIndex;
            arpRotation = 0;
            stepArpRandom();
            arpStepCounter = chordPeriod / arpDivision;
            noiseState = noiseState * 1103515245 + 12345;
            randomSHValue = ((int32_t)((noiseState >> 16) & 0xFFF) - 2048) * 12014 >> 12;
            clockCounter = 0;
        }

        // Subdivide chord period into arpDivision steps. In loop mode keep stepping
        // (wrapping) instead of holding on the last tone after one sweep.
        if (chordPeriod > 0 && arpStepCounter > 0) {
            arpStepCounter--;
            if (arpStepCounter == 0 && (arpLoop || arpRotation < (arpDivision - 1))) {
                arpRotation = (arpRotation + 1) & 7;  // wrap within pattern period
                arpStepped = true;
                stepArpRandom();
                arpStepCounter = chordPeriod / arpDivision;
            }
        }
    }

    // Pitch S&H: capture CV In 1 on Pulse In 1 rising edge
    if (pulseIn1Rising && (cv1Mode == CVOUT_PITCH_SH || cv2Mode == CVOUT_PITCH_SH)) {
        pitchSHValue = (pitchCV - 3069) * 12014 >> 12;
    }

    // Audio amplitude (shared by schmitt trigger, onset detector, input envelope)
    int32_t audioAbs = 0;
    if (needsAudioAbs) {
        int32_t abs1 = audioIn1 < 0 ? -audioIn1 : audioIn1;
        int32_t abs2 = audioIn2 < 0 ? -audioIn2 : audioIn2;
        audioAbs = abs1 > abs2 ? abs1 : abs2;
    }

    // Schmitt trigger
    bool audioTrigOut = false;
    if (needsAudioTrig) {
        if (trigPulseCounter > 0) {
            trigPulseCounter--;
        } else {
            if (triggerArmed && audioAbs > 200) {
                triggerArmed = false;
                trigPulseCounter = 2400;
            }
            if (!triggerArmed && audioAbs < 80) {
                triggerArmed = true;
            }
        }
        audioTrigOut = (trigPulseCounter > 2400 - 240);
    }

    // Onset detector
    bool onsetTrigOut = false;
    if (needsOnset) {
        const int32_t ONSET_LOCKOUT = 4800;  // 100ms refractory — one trigger per pluck
        const int32_t ONSET_PULSE = 240;     // 5ms output pulse
        // Stage 1: Peak-hold envelope — instant attack, slow release
        // Smooths out waveform zero-crossing dips without adding onset latency
        int32_t target = audioAbs << 16;
        if (target > onsetPeakEnv) {
            onsetPeakEnv = target;                           // instant attack
        } else {
            onsetPeakEnv -= (onsetPeakEnv - target) >> 13;  // release τ ≈ 170ms
        }

        // Stage 2: Baseline tracker — adapts to sustained level
        if (onsetPeakEnv > onsetEnvelope) {
            onsetEnvelope += (onsetPeakEnv - onsetEnvelope) >> 10;  // attack τ ≈ 21ms
        } else {
            // release τ ≈ 170ms — must be ≥ peak release so the baseline never falls
            // out from under a decaying note and re-arms a second (false) trigger
            onsetEnvelope -= (onsetEnvelope - onsetPeakEnv) >> 13;
        }

        // Hybrid threshold: 12.5% of baseline or absolute minimum
        int32_t threshold = onsetEnvelope >> 3;
        if (threshold < (80 << 16)) threshold = (80 << 16);

        if (onsetPulseCounter > 0) {
            onsetPulseCounter--;
        } else if (onsetPeakEnv > onsetEnvelope + threshold) {
            onsetPulseCounter = ONSET_LOCKOUT;
            onsetEnvelope = onsetPeakEnv;     // snap baseline to current peak
        }
        onsetTrigOut = (onsetPulseCounter > ONSET_LOCKOUT - ONSET_PULSE);
    }

    // Chord change pulse
    if (chordPulseCounter > 0) chordPulseCounter--;
    bool chordTrigOut = (chordPulseCounter > 0);

    // Resonator envelope follower
    if (needsResEnv) {
        int32_t resAbs = resonatorOut1 < 0 ? -resonatorOut1 : resonatorOut1;
        int32_t target = resAbs << 16;
        if (target > envFollower) {
            envFollower += (target - envFollower) >> 5;
        } else {
            envFollower -= (envFollower - target) >> 12;
        }
    }

    // Input envelope follower
    if (needsInEnv) {
        int32_t target = audioAbs << 16;
        if (target > inputEnvFollower) {
            inputEnvFollower += (target - inputEnvFollower) >> 5;
        } else {
            inputEnvFollower -= (inputEnvFollower - target) >> 12;
        }
    }

    // Feed audio to Core 1 for YIN pitch detection
    if (needsPitchTrack) {
        yinRing[yinRingHead & (YIN_RING_SIZE - 1)] = audioIn1;
        yinRingHead++;
    }

    // Arp / root pitch (millivolts)
    int32_t rootMV = 0, rootPitchMV = 0, arpMV = 0;
    if (needsRootMV) {
        rootMV = (pitchCV - 3069) * 12014 >> 12;
        // Root-pitch output can be any chord tone (string 0-3), selected by rootString.
        int rootRatio;
        switch (rootString) {
            case 1: rootRatio = ratioToMillivolts(num2, den2); break;
            case 2: rootRatio = ratioToMillivolts(num3, den3); break;
            case 3: rootRatio = ratioToMillivolts(num4, den4); break;
            default: rootRatio = 0; break;  // string 0 = root (1:1)
        }
        rootPitchMV = rootMV + rootRatio;
    }
    if (needsArpMV) {
        int ratioMV;
        switch (arpStringIndex()) {
            case 0: ratioMV = ratioToMillivolts(num1, den1); break;
            case 1: ratioMV = ratioToMillivolts(num2, den2); break;
            case 2: ratioMV = ratioToMillivolts(num3, den3); break;
            case 3: ratioMV = ratioToMillivolts(num4, den4); break;
            default: ratioMV = 0; break;
        }
        arpMV = rootMV + ratioMV;
    }

    // Tap tempo clock
    bool tapClockOut = false;
    if (needsTapClock) {
        if (chordPeriod > 0) {
            clockCounter++;
            if (clockCounter >= chordPeriod) {
                clockCounter = 0;
                tapClockPulseCounter = 240;
            }
        }
        if (tapClockPulseCounter > 0) tapClockPulseCounter--;
        tapClockOut = (tapClockPulseCounter > 0);
    }

    // Arp clock
    bool arpClockOut = false;
    if (needsArpClock) {
        if (arpStepped || chordChanged) {
            arpClockPulseCounter = 240;
        }
        if (arpClockPulseCounter > 0) arpClockPulseCounter--;
        arpClockOut = (arpClockPulseCounter > 0);
    }

    // Clock divider
    bool clockDivOut = false;
    if (needsClkDiv) {
        if (pulseIn2Rising) {
            clockDivCount++;
            if ((int)clockDivCount >= clockDivRatio) {
                clockDivCount = 0;
                clockDivPulseCounter = 240;
            }
        }
        if (clockDivPulseCounter > 0) clockDivPulseCounter--;
        clockDivOut = (clockDivPulseCounter > 0);
    }

    // --- Dispatch outputs ---

    // CV Out 1
    switch (cv1Mode) {
        default:
        case CVOUT_ARP:        CVOut1Millivolts(arpMV); break;
        case CVOUT_ROOT:       CVOut1Millivolts(rootPitchMV); break;
        case CVOUT_RES_ENV:    CVOut1((int16_t)(envFollower >> 16)); break;
        case CVOUT_IN_ENV:     CVOut1((int16_t)(inputEnvFollower >> 16)); break;
        case CVOUT_RANDOM_SH:  CVOut1Millivolts(randomSHValue); break;
        case CVOUT_PITCH_SH:   CVOut1Millivolts(pitchSHValue); break;
        case CVOUT_PITCH_TRACK: CVOut1Millivolts(yinSharedPitchMV); break;
    }

    // CV Out 2
    switch (cv2Mode) {
        default:
        case CVOUT_ARP:        CVOut2Millivolts(arpMV); break;
        case CVOUT_ROOT:       CVOut2Millivolts(rootPitchMV); break;
        case CVOUT_RES_ENV:    CVOut2((int16_t)(envFollower >> 16)); break;
        case CVOUT_IN_ENV:     CVOut2((int16_t)(inputEnvFollower >> 16)); break;
        case CVOUT_RANDOM_SH:  CVOut2Millivolts(randomSHValue); break;
        case CVOUT_PITCH_SH:   CVOut2Millivolts(pitchSHValue); break;
        case CVOUT_PITCH_TRACK: CVOut2Millivolts(yinSharedPitchMV); break;
    }

    // Audio Out 2 (as CV)
    if (ao2Mode != AO2_AUDIO) {
        int16_t ao2val = 0;
        switch (ao2Mode) {
            case AO2_RES_ENV: ao2val = (int16_t)(envFollower >> 16); break;
            case AO2_IN_ENV:  ao2val = (int16_t)(inputEnvFollower >> 16); break;
        }
        AudioOut2(ao2val);
    }

    // Pulse Out 1
    switch (p1Mode) {
        default:
        case P1_AUDIO_TRIG: PulseOut1(audioTrigOut); break;
        case P1_TAP_CLOCK:  PulseOut1(tapClockOut); break;
        case P1_ARP_CLOCK:  PulseOut1(arpClockOut); break;
        case P1_ONSET:      PulseOut1(onsetTrigOut); break;
    }

    // Pulse Out 2
    switch (p2Mode) {
        default:
        case P2_CHORD_TRIG: PulseOut2(chordTrigOut); break;
        case P2_TAP_CLOCK:  PulseOut2(tapClockOut); break;
        case P2_AUDIO_TRIG: PulseOut2(audioTrigOut); break;
        case P2_ARP_CLOCK:  PulseOut2(arpClockOut); break;
        case P2_CLK_DIV:    PulseOut2(clockDivOut); break;
        case P2_ONSET:      PulseOut2(onsetTrigOut); break;
    }

    // LED indicators - show position in progression (0-17)
    // Single LED: positions 0-5
    // Pairs for positions 6-17:
    // 6: 0+5, 7: 1+3, 8: 0+2, 9: 1+2
    // 10: 3+4, 11: 2+4, 12: 0+4, 13: 3+5
    // 14: 1+4, 15: 2+3, 16: 0+3, 17: 2+5
    // LED indicators
    // During long press: progressive fill (0->5) showing reset countdown
    // After reset: brief flash of all LEDs
    // Normal: show position in progression
    // Compute the 6 LED target states (cheap comparisons), then update only ONE LED per
    // sample (round-robin) instead of calling LedOn six times every sample. The full set
    // still refreshes every 6 samples (~125us, far faster than the eye), which keeps the
    // per-sample cost low so ProcessSample stays well under the 20.8us audio budget.
    bool ledTargets[6];
    if (switchDown && switchDownCounter > 0 && !resetTriggered) {
        // Progressive LED fill during hold (6 LEDs over 3 seconds)
        int ledsLit = (switchDownCounter * 6) / RESET_HOLD_SAMPLES;
        for (int i = 0; i < 6; i++) ledTargets[i] = (ledsLit >= i + 1);
    } else if (resetTriggered && switchDownCounter < RESET_HOLD_SAMPLES + 24000) {
        // Flash all LEDs for 0.5 sec after reset
        bool flash = ((switchDownCounter / 4800) % 2) == 0;  // 10Hz blink
        for (int i = 0; i < 6; i++) ledTargets[i] = flash;
    } else {
        // Normal: show position in progression (0-17)
        ledTargets[0] = (progressionIndex == 0 || progressionIndex == 6 || progressionIndex == 8 || progressionIndex == 12 || progressionIndex == 16);
        ledTargets[1] = (progressionIndex == 1 || progressionIndex == 7 || progressionIndex == 9 || progressionIndex == 14);
        ledTargets[2] = (progressionIndex == 2 || progressionIndex == 8 || progressionIndex == 9 || progressionIndex == 11 || progressionIndex == 15 || progressionIndex == 17);
        ledTargets[3] = (progressionIndex == 3 || progressionIndex == 7 || progressionIndex == 10 || progressionIndex == 13 || progressionIndex == 15 || progressionIndex == 16);
        ledTargets[4] = (progressionIndex == 4 || progressionIndex == 10 || progressionIndex == 11 || progressionIndex == 12 || progressionIndex == 14);
        ledTargets[5] = (progressionIndex == 5 || progressionIndex == 6 || progressionIndex == 13 || progressionIndex == 17);
    }
    LedOn(ledRR, ledTargets[ledRR]);
    ledRR = (ledRR + 1) % 6;
}

// --- Globals and Core 1 ---

// Global pointer for Core 1 to access shared state
static ResonatingStrings* g_resonator = nullptr;

void core1_handler() {
    sleep_ms(500);  // Wait for USB to settle

    // Serial state
    char lineBuf[128];
    int linePos = 0;

    // YIN pitch detector state (all local to Core 1)
    const int YIN_W = 300;
    const int YIN_MIN_LAG = 8;
    const int YIN_MAX_LAG = 300;

    int32_t hp_state = 0, lp1_state = 0, lp2_state = 0;
    int bufIdx = 0, decCount = 0;
    int decSampleCount = 0;     // total decimated samples produced
    int scanLag = 0;
    int scanWaitUntil = 0;      // don't rescan until decSampleCount reaches this
    int64_t runningSum = 0, prevNorm = 0, prevPrevNorm = 0;
    bool foundDip = false;
    int32_t pitchMV = 0;
    int32_t yinAmplitude = 0;   // fast-tracking envelope of decimated signal
    int32_t yinPeakAmp = 0;     // peak amplitude for current note (resets on snap)
    int32_t yinSlowAmp = 0;     // slow amplitude baseline for onset/attack detection
    int yinAttackHold = 0;      // counts down (decimated samples) after an onset transient
    uint32_t ringTail = 0;

    for (int i = 0; i < 1024; i++) yinBuf[i] = 0;

    while (true) {
        // Process all pending audio samples from Core 0
        uint32_t head = yinRingHead;
        while (ringTail != head) {
            int16_t sample = yinRing[ringTail & (YIN_RING_SIZE - 1)];
            ringTail++;

            // Bandpass + 4x decimation into circular buffer
            hp_state += (sample - hp_state) >> 9;
            int32_t hp = sample - hp_state;
            lp1_state += (hp - lp1_state) >> 3;
            lp2_state += (lp1_state - lp2_state) >> 3;
            if (++decCount >= 4) {
                decCount = 0;
                yinBuf[bufIdx] = (int16_t)lp2_state;
                bufIdx = (bufIdx + 1) & 1023;
                decSampleCount++;
                // Fast-tracking amplitude envelope
                int32_t absVal = lp2_state < 0 ? -lp2_state : lp2_state;
                if (absVal > yinAmplitude) {
                    yinAmplitude = absVal;                    // instant attack
                } else {
                    yinAmplitude -= yinAmplitude >> 9;        // release τ ≈ 42ms at 12kHz
                }
                // Track peak for current note (slow decay so quieter notes can be tracked)
                if (yinAmplitude > yinPeakAmp) yinPeakAmp = yinAmplitude;
                else yinPeakAmp -= yinPeakAmp >> 13;  // release τ ≈ 680ms at 12kHz
                // Slow baseline + attack (onset) detection: a fresh pluck rises well above
                // the recent average; hold the flag ~40ms so a new-note detection can use it.
                yinSlowAmp += (yinAmplitude - yinSlowAmp) >> 8;  // τ ≈ 21ms at 12kHz
                // Require a strong (2x) rise to count as an onset, so a sustained note's
                // amplitude wobble doesn't false-trigger an attack and bypass the octave guard.
                if (yinAmplitude > (yinSlowAmp << 1)) yinAttackHold = 480;
                else if (yinAttackHold > 0) yinAttackHold--;
            }
        }

        // Compute up to 4 YIN lags per iteration (full window each, no chunking)
        // Rate-limit: wait for half a window of fresh samples before rescanning
        if (scanLag <= YIN_MAX_LAG && decSampleCount >= scanWaitUntil) {
            int base = (bufIdx - 1) & 1023;
            for (int n = 0; n < 4 && scanLag <= YIN_MAX_LAG; n++) {
                uint64_t sum = 0;
                int lag = scanLag;
                for (int i = 0; i < YIN_W; i++) {
                    int idx1 = (base - i) & 1023;
                    int idx2 = (idx1 - lag) & 1023;
                    int32_t diff = (int32_t)yinBuf[idx1] - yinBuf[idx2];
                    sum += (uint32_t)diff * (uint32_t)diff;
                }

                bool detected = false;
                int64_t d = (int64_t)sum;
                if (scanLag > 0) {
                    runningSum += d;
                    if (scanLag >= YIN_MIN_LAG) {
                        // CMND: d'(τ) = d(τ)*τ / runningSum
                        int64_t dTimesLag = d * (int64_t)scanLag;
                        // CMND < 0.125: keeps the true-period dip qualifying further into
                        // a sustain (raised from 0.10 to reduce octave-down lock-on).
                        bool belowThreshold = ((dTimesLag << 3) < runningSum);
                        // Detect first local minimum after crossing below threshold
                        if (foundDip && dTimesLag > prevNorm) {
                            // Parabolic interpolation for sub-sample lag precision
                            // Fit parabola through CMND at lags (scanLag-2, scanLag-1, scanLag)
                            // Minimum offset = (a-c) / (2*(a - 2b + c)) where a,b,c are the three values
                            int32_t period_fp8;
                            int64_t denom = prevPrevNorm - 2 * prevNorm + dTimesLag;
                            if (denom > 0 && scanLag >= YIN_MIN_LAG + 2) {
                                int32_t offset_fp8 = (int32_t)(((prevPrevNorm - dTimesLag) << 7) / denom);
                                if (offset_fp8 < -128) offset_fp8 = -128;
                                if (offset_fp8 > 128) offset_fp8 = 128;
                                period_fp8 = (((scanLag - 1) << 8) + offset_fp8) << 2;
                            } else {
                                period_fp8 = ((scanLag - 1) * 4) << 8;
                            }
                            int32_t newMV = periodToMillivoltsFrac(period_fp8);
                            int32_t diff = newMV - pitchMV;
                            int32_t absDiff = diff < 0 ? -diff : diff;

                            // Octave guard: a detection ~1 octave away (±1000mV, ±60mV) mid-sustain
                            // is almost always a YIN octave error (2nd-harmonic lock or subharmonic
                            // dip), not a real note. Reject both directions; allow a genuine octave
                            // leap only right after an onset transient (a fresh pluck/new note).
                            bool spuriousOctaveDown = (diff < -910 && diff > -1090 && yinAttackHold == 0);
                            bool spuriousOctaveUp   = (diff >  910 && diff <  1090 && yinAttackHold == 0);

                            if (spuriousOctaveDown || spuriousOctaveUp) {
                                // Reject: hold current pitch
                            } else if (absDiff > 80 && yinAmplitude > (yinPeakAmp >> 1)) {
                                // New note: large change + still strong → snap
                                pitchMV = newMV;
                                yinPeakAmp = yinAmplitude;  // reset peak for new note
                                yinSharedPitchMV = pitchMV;
                            } else if (yinAmplitude > (yinPeakAmp >> 1)) {
                                // Near peak: smooth toward detection
                                pitchMV += (diff + 8) >> 4;
                                yinSharedPitchMV = pitchMV;
                            }
                            // else: signal has decayed below half peak — hold last pitch
                            detected = true;
                        }
                        foundDip = foundDip || belowThreshold;
                        prevPrevNorm = prevNorm;
                        prevNorm = dTimesLag;
                    }
                }
                if (detected) {
                    scanLag = 0;
                    runningSum = 0;
                    prevNorm = 0;
                    prevPrevNorm = 0;
                    foundDip = false;
                    scanWaitUntil = decSampleCount + (YIN_W / 2); // wait for fresh audio
                    break;
                }
                scanLag++;
            }
            if (scanLag > YIN_MAX_LAG) {
                scanLag = 0;
                runningSum = 0;
                prevNorm = 0;
                prevPrevNorm = 0;
                foundDip = false;
                scanWaitUntil = decSampleCount + (YIN_W / 2);
            }
        }

        // Non-blocking serial check
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            g_resonator->checkPendingFlashSave();
        } else if (c == '\n' || c == '\r') {
            if (linePos > 0) {
                lineBuf[linePos] = '\0';
                g_resonator->handleSerialCommand(lineBuf);
                linePos = 0;
            }
        } else if (linePos < 127) {
            lineBuf[linePos++] = (char)c;
        }
    }
}

int main() {
    stdio_init_all();  // Initialize USB CDC

    static ResonatingStrings resonator;
    g_resonator = &resonator;
    resonator.EnableNormalisationProbe();

    // Launch Core 1 first: the launch handshake uses the inter-core FIFO that the
    // lockout mechanism also relies on, so set up the flash-safe handler afterwards.
    multicore_launch_core1(core1_handler);

    // Register Core 0 as a flash_safe_execute victim so Core 1 can safely lock it out
    // during flash writes (see saveProgressionToFlash). This replaces the hand-rolled
    // multicore_lockout, which could deadlock under heavy Core 1 load (pitch tracking).
    flash_safe_execute_core_init();

    // Raise the flash-lockout FIFO IRQ above the audio DMA IRQ (0x80) so Core 0 answers a
    // lockout request immediately, before/preempting the audio ISR. Otherwise, under heavy
    // dual-core load the handshake times out and leaves stale entries in the 8-deep inter-core
    // FIFO; after ~8-10 saves the FIFO fills and the lockout breaks permanently (crash on save).
    irq_set_priority(SIO_IRQ_PROC0, 0x40);
    resonator.Run();
    return 0;
}
