//
//  WildPebble.cpp
//
//
//  Created by Adrian Vos/LLM on 16/05/2026.
//


// Wild Pebble
// A playable generative rhythm + melody organism
// for the Music Thing Modular Workshop Computer.
//
// Inspired by the spirit of Jonah Senzel's Pet Rock.
//
// This card creates:
// - two related trigger streams
// - a quantised melodic CV line
// - an evolving modulation/energy output
// - constrained musical mutation
//
// Design goals:
// - musically useful
// - low CPU usage
// - easy to understand
// - stable on Workshop Computer hardware
//
// All sequencing logic is integer-only and lightweight.
// Expensive work happens at control rate instead of every sample.

#include "ComputerCard.h"
#include "hardware/clocks.h"
#include "bsp/board.h"
#include "pico/multicore.h"
#include "tusb.h"
#include <stdint.h>
#include "pico/unique_id.h"

static constexpr uint8_t kMidiClock = 0xF8;
static constexpr uint8_t kMidiStart = 0xFA;
static constexpr uint8_t kMidiContinue = 0xFB;
static constexpr uint8_t kMidiStop = 0xFC;
static constexpr uint8_t kMidiNoteOff = 0x80;
static constexpr uint8_t kMidiNoteOn = 0x90;
static constexpr uint8_t kMidiChannel = 0;
static constexpr uint8_t kMidiClocksPerStep = 12;

volatile uint8_t gMidiClockTicksPending = 0;
volatile uint8_t gMidiStartPending = 0;
volatile uint8_t gMidiContinuePending = 0;
volatile uint8_t gMidiStopPending = 0;

struct MidiMessage
{
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

static constexpr uint8_t kMidiQueueSize = 32;
volatile MidiMessage gMidiOutputQueue[kMidiQueueSize];
volatile uint8_t gMidiQueueWrite = 0;
volatile uint8_t gMidiQueueRead = 0;

void QueueMidiMessage(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t nextWrite = (uint8_t)((gMidiQueueWrite + 1) & (kMidiQueueSize - 1));

    if(nextWrite == gMidiQueueRead)
    {
        return;
    }

    gMidiOutputQueue[gMidiQueueWrite].status = status;
    gMidiOutputQueue[gMidiQueueWrite].data1 = data1;
    gMidiOutputQueue[gMidiQueueWrite].data2 = data2;
    gMidiQueueWrite = nextWrite;
}

class WildPebble : public ComputerCard
{
public:
    static constexpr int32_t kSteps = 16;

    static constexpr int32_t kScaleCount = 4;

    static constexpr int32_t kScales[kScaleCount][5] =
    {
        {0, 3, 5, 7, 10},
        {0, 2, 5, 7, 10},
        {0, 2, 4, 7, 9},
        {0, 5, 7, 10, 12}
    };

    int32_t trigProb[kSteps];
    int32_t noteIndex[kSteps];
    int32_t accent[kSteps];
    int32_t energy[kSteps];

    int32_t currentStep = -1;

    int32_t currentMIDINote = 48;
    int32_t currentEnergy = 0;
    int32_t kickPhase = 0;
    int32_t kickEnv = 0;
    int32_t kickPitch = 0;
    int32_t snareEnv = 0;
    int32_t snarePhase = 0;
    int32_t snarePitch = 0;
    

    bool pulse1 = false;
    bool pulse2 = false;

    int32_t pulseTimer1 = 0;
    int32_t pulseTimer2 = 0;

    int32_t pulseLength1 = 700;
    int32_t pulseLength2 = 400;

    uint32_t sampleCounter = 0;
    uint32_t clockCounter = 0;

    uint32_t internalClockPeriod = 4000;

    bool externalClockActive = false;
    uint32_t pulseClockTimeout = 0;
    uint32_t midiClockTimeout = 0;
    uint32_t lastPulseClockSample = 0;
    uint32_t lastMidiStepSample = 0;
    uint32_t pulseClockPeriod = 0xFFFFFFFF;
    uint32_t midiClockPeriod = 0xFFFFFFFF;
    bool midiClockRunning = false;
    uint8_t midiClockDivider = 0;
    bool midiNoteGateActive = false;
    uint8_t midiLastNote = 48;
    uint32_t midiNoteOffSample = 0;
    uint32_t midiNoteOnSample = 0;
    uint32_t selectedClockPeriod = 4000;

    int32_t tension = 64;
    bool tensionRising = true;

    int32_t swingOffset = 0;

    int32_t smoothedEnergy = 0;

    int32_t currentScale = 0;

    uint32_t rng = 0xCAFEBABE;

    bool rngSeeded = false;

    static constexpr uint32_t kLedDivider = 96;

    static constexpr int32_t mutationResistance[kSteps] =
    {
        1,4,3,5,
        1,5,3,4,
        1,4,3,5,
        1,5,3,4
    };

    inline int32_t SoftClip(int32_t x)
    {
        if(x > 1536)
        {
            x = 1536 + ((x - 1536) >> 2);
        }

        if(x < -1536)
        {
            x = -1536 + ((x + 1536) >> 2);
        }

        return x;
    }

    inline uint32_t Random()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    WildPebble()
    {
        for(int32_t i = 0; i < kSteps; ++i)
        {
            trigProb[i] = 140;
            noteIndex[i] = i % 5;
            accent[i] = 128;
            energy[i] = 96;
        }

        trigProb[0] = 255;
        trigProb[4] = 220;
        trigProb[8] = 220;
        trigProb[12] = 220;
    }
    
    void SeedRNGFromFlashID()
    {
        pico_unique_board_id_t id;

        pico_get_unique_board_id(&id);

        rng = 0;

        for(int32_t i = 0;
            i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
            ++i)
        {
            rng ^= ((uint32_t)id.id[i])
                   << ((i & 3) * 8);

            // xorshift-style mixing
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
        }

        // Optional extra entropy from live controls
        rng ^= (uint32_t)(KnobVal(Knob::Main) << 1);
        rng ^= (uint32_t)(CVIn1() << 3);

        // Prevent illegal zero RNG state
        if(rng == 0)
        {
            rng = 0xCAFEBABE;
        }
    }

    void UpdateTension()
    {
        if(tensionRising)
        {
            tension++;

            if(tension > 220)
            {
                tensionRising = false;
            }
        }
        else
        {
            tension--;

            if(tension < 40)
            {
                tensionRising = true;
            }
        }
    }

    void RotateScaleSlowly(int32_t mutationAmount, int32_t mode)
    {
        int32_t shiftChance;

        if(mode == Switch::Up)
        {
            shiftChance = 255 - (mutationAmount >> 2);
        }
        else if(mode == Switch::Middle)
        {
            shiftChance = 255 - (mutationAmount >> 1);
        }
        else
        {
            shiftChance = 255 - mutationAmount;
        }

        if(shiftChance < 4)
        {
            shiftChance = 4;
        }

        if((Random() & 255) > (uint32_t)shiftChance)
        {
            if((Random() & 3) == 0)
            {
                currentScale = Random() % kScaleCount;
            }
            else
            {
                currentScale += (Random() & 1) ? 1 : -1;
            }

            if(currentScale < 0)
            {
                currentScale = kScaleCount - 1;
            }

            if(currentScale >= kScaleCount)
            {
                currentScale = 0;
            }
        }
    }

    void CopyPhrase()
    {
        int32_t src = (Random() % 4) * 4;
        int32_t dst = (Random() % 4) * 4;

        if(src == dst)
        {
            return;
        }

        for(int32_t i = 0; i < 4; ++i)
        {
            trigProb[dst + i] = trigProb[src + i];
            noteIndex[dst + i] = noteIndex[src + i];
            accent[dst + i] = accent[src + i];
            energy[dst + i] = energy[src + i];
        }

        int32_t mutateStep = dst + (Random() & 3);

        noteIndex[mutateStep] += ((Random() % 3) - 1);

        if(noteIndex[mutateStep] < 0)
        {
            noteIndex[mutateStep] = 0;
        }

        if(noteIndex[mutateStep] > 14)
        {
            noteIndex[mutateStep] = 14;
        }
    }

    int32_t WeightedMelodicMove(int32_t mode)
    {
        uint32_t r = Random() & 255;

        if(mode == Switch::Up)
        {
            if(r < 120) return 0;
            if(r < 220) return (Random() & 1) ? 1 : -1;
            return 0;
        }

        if(mode == Switch::Middle)
        {
            if(r < 80) return 0;
            if(r < 200) return (Random() & 1) ? 1 : -1;
            if(r < 240) return (Random() & 1) ? 2 : -2;
            return (Random() & 1) ? 5 : -5;
        }

        if(r < 40) return 0;
        if(r < 150) return (Random() & 1) ? 1 : -1;
        if(r < 220) return (Random() & 1) ? 2 : -2;
        return (Random() & 1) ? 5 : -5;
    }

    void Mutate(int32_t amount, int32_t mode)
    {
        int32_t step = Random() % kSteps;

        if((Random() % mutationResistance[step]) != 0)
        {
            return;
        }

        if((Random() & 255) > (uint32_t)amount)
        {
            return;
        }

        trigProb[step] += ((int32_t)(Random() & 31)) - 15;

        if(trigProb[step] < 20)
        {
            trigProb[step] = 20;
        }

        if(trigProb[step] > 255)
        {
            trigProb[step] = 255;
        }

        noteIndex[step] += WeightedMelodicMove(mode);
        
        // occasional octave mutation

        int32_t octaveChance = tension >> 3;

        if((Random() & 255) < (uint32_t)octaveChance)
        {
            noteIndex[step] +=
                ((Random() & 1) ? 5 : -5);
        }

        if(noteIndex[step] < 0)
        {
            noteIndex[step] = 0;
        }

        if(noteIndex[step] > 24)
        {
            noteIndex[step] = 24;
        }

        energy[step] += ((int32_t)(Random() & 63)) - 31;

        if(energy[step] < 0)
        {
            energy[step] = 0;
        }

        if(energy[step] > 255)
        {
            energy[step] = 255;
        }

        accent[step] += ((int32_t)(Random() & 31)) - 15;

        if(accent[step] < 32)
        {
            accent[step] = 32;
        }

        if(accent[step] > 255)
        {
            accent[step] = 255;
        }
    }

    void AdvanceStep(int32_t density, int32_t mode)
    {
        currentStep++;

        if(currentStep >= kSteps)
        {
            currentStep = 0;
        }

        int32_t threshold =
            trigProb[currentStep] +
            ((density - 2048) >> 4);

        if(threshold < 0)
        {
            threshold = 0;
        }

        if(threshold > 255)
        {
            threshold = 255;
        }

        pulse1 =
            ((Random() & 255) < (uint32_t)threshold);

        int32_t companionThreshold = threshold >> 1;

        if((currentStep & 3) == 2)
        {
            companionThreshold += 40;
        }

        if((currentStep & 1) && mode == Switch::Down)
        {
            companionThreshold += 30;
        }

        if(companionThreshold > 255)
        {
            companionThreshold = 255;
        }

        pulse2 =
            pulse1 &&
            ((Random() & 255) <
            (uint32_t)companionThreshold);

        int32_t octave = noteIndex[currentStep] / 5;
        int32_t degree = noteIndex[currentStep] % 5;

        currentMIDINote =
            48 +
            (octave * 12) +
            kScales[currentScale][degree];

        if(currentMIDINote < 0)
        {
            currentMIDINote = 0;
        }

        if(currentMIDINote > 127)
        {
            currentMIDINote = 127;
        }

        currentEnergy =
            energy[currentStep] +
            (accent[currentStep] >> 1) +
            (tension >> 1);

        if(currentEnergy < 0)
        {
            currentEnergy = 0;
        }

        if(currentEnergy > 255)
        {
            currentEnergy = 255;
        }

        pulseTimer1 = pulse1 ? pulseLength1 : 0;
        pulseTimer2 = pulse2 ? pulseLength2 : 0;
        
        if(pulse1)
        {
            kickEnv = 4095;

            kickPitch =
                14 +
                (accent[currentStep] >> 5);
        }
    }

    uint32_t ActiveStepPeriod() const
    {
        if(selectedClockPeriod > 0)
        {
            return selectedClockPeriod;
        }

        return internalClockPeriod;
    }

    uint8_t MidiVelocity() const
    {
        int32_t velocity = 28 + (currentEnergy >> 1) + (tension >> 2);

        if(velocity > 127)
        {
            velocity = 127;
        }

        if(velocity < 24)
        {
            velocity = 24;
        }

        return (uint8_t)velocity;
    }

    uint32_t MidiNoteLength() const
    {
        uint32_t stepPeriod = ActiveStepPeriod();
        uint32_t shape = (uint32_t)(80 + (currentEnergy >> 1) + (tension >> 2));

        if(shape > 240)
        {
            shape = 240;
        }

        uint32_t noteLength = (stepPeriod * shape) >> 8;

        if(noteLength < 120)
        {
            noteLength = 120;
        }

        return noteLength;
    }

    bool ShouldRetriggerMidiNote()
    {
        if(!midiNoteGateActive)
        {
            return true;
        }

        uint32_t minGap = ActiveStepPeriod() >> 3;

        if(minGap < 120)
        {
            minGap = 120;
        }

        if((sampleCounter - midiNoteOnSample) < minGap)
        {
            return false;
        }

        uint32_t retriggerChance = (uint32_t)((currentEnergy >> 1) + (tension >> 2));

        if(retriggerChance > 220)
        {
            retriggerChance = 220;
        }

        return (Random() & 255) < retriggerChance;
    }

    void TriggerMidiNote()
    {
        if(!ShouldRetriggerMidiNote())
        {
            return;
        }

        if(midiNoteGateActive)
        {
            QueueMidiMessage(kMidiNoteOff | kMidiChannel,
                             midiLastNote,
                             0);
        }

        midiLastNote = (uint8_t)currentMIDINote;

        QueueMidiMessage(kMidiNoteOn | kMidiChannel,
                         midiLastNote,
                         MidiVelocity());

        midiNoteGateActive = true;
        midiNoteOnSample = sampleCounter;
        midiNoteOffSample = sampleCounter + MidiNoteLength();
    }

    bool ConsumeMidiClock()
    {
        bool clockEvent = false;

        if(gMidiStartPending > 0)
        {
            gMidiStartPending--;
            midiClockRunning = true;
            midiClockDivider = 0;
            currentStep = -1;
            clockCounter = 0;
            lastMidiStepSample = sampleCounter;
        }

        if(gMidiContinuePending > 0)
        {
            gMidiContinuePending--;
            midiClockRunning = true;
        }

        if(gMidiStopPending > 0)
        {
            gMidiStopPending--;
            midiClockRunning = false;
            midiClockDivider = 0;
            midiClockTimeout = 0;
            midiClockPeriod = 0xFFFFFFFF;

            if(midiNoteGateActive)
            {
                QueueMidiMessage(kMidiNoteOff | kMidiChannel,
                                 midiLastNote,
                                 0);
                midiNoteGateActive = false;
            }
        }

        while(gMidiClockTicksPending > 0)
        {
            gMidiClockTicksPending--;

            if(!midiClockRunning)
            {
                continue;
            }

            midiClockTimeout = 48000;

            midiClockDivider++;

            if(midiClockDivider >= kMidiClocksPerStep)
            {
                midiClockDivider = 0;
                midiClockPeriod = sampleCounter - lastMidiStepSample;
                lastMidiStepSample = sampleCounter;
                clockEvent = true;
                break;
            }
        }

        return clockEvent;
    }

    void UpdateMidiNoteOutput()
    {
        if(midiNoteGateActive &&
           ((int32_t)(sampleCounter - midiNoteOffSample) >= 0))
        {
            QueueMidiMessage(kMidiNoteOff | kMidiChannel,
                             midiLastNote,
                             0);

            midiNoteGateActive = false;
        }
    }

    virtual void ProcessSample()
    {
        sampleCounter++;

        if(!rngSeeded)
        {
            SeedRNGFromFlashID();
            rngSeeded = true;
        }

        int32_t tempo = KnobVal(Knob::Main);
        int32_t density = KnobVal(Knob::X);
        int32_t mutation = KnobVal(Knob::Y);

       
        density += (CVIn1() >> 1);
        mutation += (CVIn2() >> 1);
        
        if(density < 0) density = 0;
        if(density > 4095) density = 4095;

        if(mutation < 0) mutation = 0;
        if(mutation > 4095) mutation = 4095;

        int32_t mode = SwitchVal();

        bool freeze = PulseIn2();

        bool midiClockEvent = ConsumeMidiClock();
        bool pulseClockEvent = false;

        if(PulseIn1RisingEdge())
        {
            pulseClockPeriod = sampleCounter - lastPulseClockSample;
            lastPulseClockSample = sampleCounter;
            pulseClockTimeout = 48000;
            pulseClockEvent = true;
        }

        if(pulseClockTimeout > 0)
        {
            pulseClockTimeout--;
        }

        if(midiClockTimeout > 0)
        {
            midiClockTimeout--;
        }

        bool pulseClockActive = pulseClockTimeout > 0;
        bool midiClockActive =
            midiClockRunning &&
            (midiClockTimeout > 0);

        bool preferPulseClock =
            pulseClockActive &&
            (!midiClockActive ||
             (pulseClockPeriod < midiClockPeriod));

        bool clockEvent = false;

        if(preferPulseClock)
        {
            clockEvent = pulseClockEvent;
            selectedClockPeriod = pulseClockPeriod;
        }
        else if(midiClockActive)
        {
            clockEvent = midiClockEvent;
            selectedClockPeriod = midiClockPeriod;
        }
        else
        {
            selectedClockPeriod = internalClockPeriod;
        }

        externalClockActive =
            pulseClockActive ||
            midiClockActive;

        if(!externalClockActive)
        {
            swingOffset = 0;

            if(mode == Switch::Middle)
            {
                swingOffset = 120;
            }
            else if(mode == Switch::Down)
            {
                swingOffset = 260;
            }

            static constexpr uint32_t kMaxPeriod = 32000; // ~90 BPM
            static constexpr uint32_t kMinPeriod = 3000;  // fast end

            internalClockPeriod =
                kMaxPeriod -
                ((tempo * (kMaxPeriod - kMinPeriod)) >> 12);
            if((currentStep >= 0) && (currentStep & 1))
            {
                internalClockPeriod += swingOffset;
            }

            clockCounter++;

            if(clockCounter >= internalClockPeriod)
            {
                clockCounter = 0;
                clockEvent = true;
            }
        }

        if(clockEvent)
        {
            AdvanceStep(density, mode);

            if(pulse1)
            {
                TriggerMidiNote();
            }
            
        // Update S+H only when Pulse2 fires
            
            if(pulse2)
            {
                snareEnv = 4095;

                snarePitch =
                    28 +
                    (accent[currentStep] >> 3);

                
            }

            if(!freeze)
            {
                int32_t mutationAmount =
                    (mutation >> 4) +
                    (tension >> 3);

                if(mutationAmount > 255)
                {
                    mutationAmount = 255;
                }

                Mutate(mutationAmount, mode);
            }

            if((currentStep & 3) == 0)
            {
                UpdateTension();
            }

            if(currentStep == 0)
            {
                if((Random() & 7) == 0)
                {
                    CopyPhrase();
                }

                int32_t mutationAmount =
                    (mutation >> 4) +
                    (tension >> 3);

                if(mutationAmount > 255)
                {
                    mutationAmount = 255;
                }

                RotateScaleSlowly(mutationAmount, mode);
            }
        }

        if(pulseTimer1 > 0)
        {
            pulseTimer1--;
        }
        else
        {
            pulse1 = false;
        }

        if(pulseTimer2 > 0)
        {
            pulseTimer2--;
        }
        else
        {
            pulse2 = false;
        }

        PulseOut1(pulse1);
        PulseOut2(pulse2);
        UpdateMidiNoteOutput();

        CVOut1MIDINote(currentMIDINote);

        smoothedEnergy +=
            (currentEnergy - smoothedEnergy) >> 5;

        int32_t energyCV =
            (smoothedEnergy << 4) - 2048;

        energyCV = SoftClip(energyCV);

        if(energyCV > 2047)
        {
            energyCV = 2047;
        }

        if(energyCV < -2048)
        {
            energyCV = -2048;
        }
        
        CVOut2(energyCV);

        kickPhase += kickPitch;
        kickPhase &= 4095;

        if(kickEnv > 0)
        {
            if((sampleCounter & 1) == 0)
            {
                kickPitch -= 1;
            }

            if(kickPitch < 4)
            {
                kickPitch = 4;
            }
        }
        // triangle oscillator
        int32_t osc;

        if(kickPhase & 2048)
        {
            osc = 2048 - (kickPhase & 2047);
        }
        else
        {
            osc = kickPhase & 2047;
        }

        osc -= 1024;
        osc <<= 1;

        // exponential-ish decay
        if(kickEnv > 0)
        {
            kickEnv -= (kickEnv >> 12) + 1;

            if(kickEnv < 0)
            {
                kickEnv = 0;
            }
        }

        int32_t kick =
            (osc * kickEnv) >> 8;

        // subtle saturation
        kick = SoftClip(kick);

        AudioOut1(kick);


        // =====================================================
        // Snare synth
        // =====================================================

        // tonal body
        snarePhase += snarePitch;
        snarePhase &= 4095;

        if(snareEnv > 0)
        {
            snarePitch -= 1;

            if(snarePitch < 24)
            {
                snarePitch = 24;
            }
        }

        // triangle oscillator
        int32_t body;

        if(snarePhase & 2048)
        {
            body = 2048 - (snarePhase & 2047);
        }
        else
        {
            body = snarePhase & 2047;
        }

        body -= 1024;
        body <<= 1;

        // fresh noise every sample while envelope active
        int32_t noise = 0;

        if(snareEnv > 8)
        {
            noise =
                (((int32_t)(Random() & 255) - 128) << 3 );
        }

        // envelope decay
        if(snareEnv > 0)
        {
            snareEnv -= (snareEnv >> 9) + 1;

            if(snareEnv < 0)
            {
                snareEnv = 0;
            }
        }

        // mix tonal + noise
        int32_t snare =
            ((body * snareEnv) >> 10) +
            ((noise * snareEnv) >> 10);

        // transient click
        if(pulse2)
        {
            snare += 120;
        }

        snare = SoftClip(snare);

        if(snare > 2047)
        {
            snare = 2047;
        }

        if(snare < -2048)
        {
            snare = -2048;
        }

        AudioOut2(snare);

        if((sampleCounter % kLedDivider) == 0)
        {
            int32_t ledEnergy = smoothedEnergy << 3;

            if(ledEnergy > 4095)
            {
                ledEnergy = 4095;
            }

            if(ledEnergy < 0)
            {
                ledEnergy = 0;
            }

            int32_t tensionLED = tension << 3;

            if(tensionLED > 4095)
            {
                tensionLED = 4095;
            }

            LedBrightness(0, pulse1 ? 4095 : 0);
            LedBrightness(1, density);
            LedBrightness(2, mutation);
            LedBrightness(3, ledEnergy);
            LedBrightness(4, externalClockActive ? 4095 : 0);
            LedBrightness(5, tensionLED);
        }
    }
};

WildPebble card;

void AudioCore()
{
    card.Run();
}

void HandleMidiInputByte(uint8_t byte)
{
    if(byte == kMidiClock)
    {
        if(gMidiClockTicksPending < 255)
        {
            gMidiClockTicksPending++;
        }
    }
    else if(byte == kMidiStart)
    {
        gMidiStartPending++;
    }
    else if(byte == kMidiContinue)
    {
        gMidiContinuePending++;
    }
    else if(byte == kMidiStop)
    {
        gMidiStopPending++;
    }
}

void MidiDeviceTask()
{
    uint8_t packet[64];

    while(tud_midi_available())
    {
        uint32_t bytesRead =
            tud_midi_stream_read(packet, sizeof(packet));

        for(uint32_t i = 0; i < bytesRead; ++i)
        {
            HandleMidiInputByte(packet[i]);
        }
    }

    while(gMidiQueueRead != gMidiQueueWrite)
    {
        MidiMessage message;
        message.status = gMidiOutputQueue[gMidiQueueRead].status;
        message.data1 = gMidiOutputQueue[gMidiQueueRead].data1;
        message.data2 = gMidiOutputQueue[gMidiQueueRead].data2;

        uint8_t out[3] =
        {
            message.status,
            message.data1,
            message.data2
        };

        uint32_t written = tud_midi_stream_write(0, out, sizeof(out));

        if(written == 0)
        {
            break;
        }

        gMidiQueueRead =
            (uint8_t)((gMidiQueueRead + 1) & (kMidiQueueSize - 1));
    }
}

int main()
{
    set_sys_clock_khz(144000, true);

    board_init();
    tusb_init();

    multicore_launch_core1(AudioCore);

    while(true)
    {
        tud_task();
        MidiDeviceTask();
    }
}
