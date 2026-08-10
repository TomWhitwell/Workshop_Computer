/*
Castle Process

Fort Processor-inspired program card for the Music Thing Modular Workshop Computer.

Signal flow:
1. Distorted input / feedback stage with crude sub-octaves
2. Light/touch surrogate squarewave section
3. Variable-speed chopper between those sources
4. Triggered bass drone voice

Switch behaviour on Workshop Computer:
- Up: latched alternate mode, more oscillator-forward
- Middle: default mixed mode
- Down: momentary bend/chaos injection while held
*/

#include "ComputerCard.h"
#include "hardware/clocks.h"
#include <stdint.h>

class CastleProcess : public ComputerCard
{
public:
    static constexpr uint32_t kControlDiv = 128;
    static constexpr uint32_t kSlowDiv = 512;
    static constexpr uint32_t kDelaySize = 1024;
    static constexpr uint32_t kDelayMask = kDelaySize - 1;

    uint32_t sampleCounter = 0;
    uint32_t rng = 0x41C64E6Du;

    int32_t delayLine[kDelaySize];
    uint32_t delayWrite = 0;

    int32_t gainCtl = 1800;
    int32_t chopCtl = 1400;
    int32_t tuneCtl = 1700;
    int32_t lightCtl = 1700;
    int32_t touchCtl = 1100;

    int32_t smoothGain = 1800;
    int32_t smoothChop = 1400;
    int32_t smoothTune = 1700;
    int32_t smoothLight = 1700;
    int32_t smoothTouch = 1100;

    bool inputConnected = false;
    bool feedbackPolarity = false;

    int32_t inputLP = 0;
    int32_t inputHP = 0;
    int32_t feedbackState = 0;

    int32_t dividerLP = 0;
    bool sub1State = false;
    bool sub2State = false;
    int32_t lastDividerSign = 0;

    uint32_t oscPhaseA = 0;
    uint32_t oscPhaseB = 0;
    uint32_t oscPhaseC = 0;

    uint32_t chopPhase = 0;
    bool chopState = false;

    int32_t bassEnv = 0;
    int32_t bassPhase = 0;
    int32_t bassPitch = 36;
    int32_t lastChopSign = 0;
    uint32_t internalStrikeCounter = 0;
    uint32_t internalStrikePeriod = 12000;
    bool pulse1ExternalActive = false;
    uint32_t pulse1ExternalTimeout = 0;

    int32_t displayLevel = 0;

    inline int32_t Clamp16(int32_t x)
    {
        if(x > 2047) return 2047;
        if(x < -2048) return -2048;
        return x;
    }

    inline int32_t Clamp12(int32_t x)
    {
        if(x > 4095) return 4095;
        if(x < 0) return 0;
        return x;
    }

    inline int32_t Abs32(int32_t x)
    {
        return x < 0 ? -x : x;
    }

    inline int32_t SoftClip(int32_t x)
    {
        if(x > 1536)
        {
            x = 1536 + ((x - 1536) >> 2);
        }
        else if(x < -1536)
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

    inline int32_t ReadDelay(uint32_t offset)
    {
        return delayLine[offset & kDelayMask];
    }

    inline void WriteDelay(uint32_t offset, int32_t value)
    {
        delayLine[offset & kDelayMask] = value;
    }

    inline int32_t SquareFromPhase(uint32_t phase)
    {
        return (phase & 0x80000000u) ? 1536 : -1536;
    }

    void SeedRng()
    {
        uint64_t uid = UniqueCardID();
        rng ^= (uint32_t)uid;
        rng ^= (uint32_t)(uid >> 32);
        if(rng == 0)
        {
            rng = 0x41C64E6Du;
        }
    }

    void UpdateControls()
    {
        int32_t main = KnobVal(Knob::Main);
        int32_t x = KnobVal(Knob::X);
        int32_t y = KnobVal(Knob::Y);

        int32_t cv1 = CVIn1();
        int32_t cv2 = CVIn2();

        gainCtl = Clamp12(main + 768);
        chopCtl = Clamp12(x + 1024);
        tuneCtl = Clamp12(y + 1024);
        lightCtl = Clamp12(main + (cv1 << 1));
        touchCtl = Clamp12(x + y + cv2);

        if(SwitchVal() == Switch::Up)
        {
            lightCtl = Clamp12(lightCtl + 768);
            touchCtl = Clamp12(touchCtl + 384);
        }

        if(SwitchVal() == Switch::Down)
        {
            chopCtl = Clamp12(chopCtl + 1400);
            touchCtl = Clamp12(touchCtl + 1200);
        }

        smoothGain += (gainCtl - smoothGain) >> 4;
        smoothChop += (chopCtl - smoothChop) >> 4;
        smoothTune += (tuneCtl - smoothTune) >> 4;
        smoothLight += (lightCtl - smoothLight) >> 4;
        smoothTouch += (touchCtl - smoothTouch) >> 4;
    }

    int32_t ProcessInputStage(bool bendHeld)
    {
        int32_t raw = AudioIn1();
        inputConnected = Connected(Input::Audio1);

        inputLP += (raw - inputLP) >> 2;
        int32_t highPassed = raw - inputLP;
        inputHP += (highPassed - inputHP) >> 3;

        int32_t driven = inputHP * ((smoothGain >> 4) + 24);
        driven >>= 5;

        if(!inputConnected)
        {
            int32_t fbTap = ReadDelay(delayWrite - (128 + (smoothChop >> 3)));
            int32_t fbGain = bendHeld ? 1792 : 1280;
            feedbackState += (((fbTap * fbGain) >> 10) - feedbackState) >> 3;
            driven += feedbackState;
        }

        if(bendHeld)
        {
            driven += ((int32_t)(Random() & 255) - 128) << 2;
        }

        return SoftClip(driven);
    }

    int32_t ProcessDividerStage(int32_t driven)
    {
        dividerLP += (driven - dividerLP) >> 2;
        int32_t sign = dividerLP >= 0 ? 1 : -1;

        if(sign != lastDividerSign)
        {
            sub1State = !sub1State;
            if(!sub1State)
            {
                sub2State = !sub2State;
            }
            lastDividerSign = sign;
        }

        int32_t subMix = 0;
        subMix += sub1State ? 640 : -640;
        subMix += sub2State ? 384 : -384;
        return subMix;
    }

    int32_t ProcessSquareStage(bool bendHeld)
    {
        uint32_t baseA = 3000000u + ((uint32_t)smoothLight << 10);
        uint32_t baseB = 1700000u + ((uint32_t)smoothLight << 9);
        uint32_t baseC = 900000u + ((uint32_t)smoothTouch << 8);

        if(bendHeld)
        {
            baseA += 1400000u;
            baseB += 700000u;
            baseC += 350000u;
        }

        oscPhaseA += baseA;
        oscPhaseB += baseB + ((smoothTouch & 255) << 13);
        oscPhaseC += baseC + ((smoothGain & 127) << 12);

        int32_t sqA = SquareFromPhase(oscPhaseA);
        int32_t sqB = SquareFromPhase(oscPhaseB);
        int32_t sqC = SquareFromPhase(oscPhaseC);

        // Use control interaction as the touch-pad surrogate.
        int32_t touchMix = ((smoothTouch >> 4) + 32);
        int32_t mixed = sqA + ((sqB * touchMix) >> 6) - (sqC >> 1);
        return SoftClip(mixed);
    }

    int32_t ProcessChopperStage(int32_t inputVoice, int32_t squareVoice, bool bendHeld)
    {
        uint32_t chopRate = 150000u + ((uint32_t)smoothChop << 10);
        if(bendHeld)
        {
            chopRate += ((uint32_t)(Random() & 255u)) << 11;
        }

        chopPhase += chopRate;
        chopState = (chopPhase & 0x80000000u) != 0;

        int32_t inputGate = (chopPhase & 0x20000000u) ? 2047 : 0;
        int32_t choppyInput = (inputVoice * inputGate) >> 11;

        if(SwitchVal() == Switch::Up)
        {
            choppyInput += (inputVoice * 128) >> 11;
        }

        int32_t chopped = chopState ? squareVoice : choppyInput;
        if(SwitchVal() == Switch::Middle)
        {
            chopped += (chopState ? (choppyInput >> 2) : (squareVoice >> 3));
        }

        return SoftClip(chopped);
    }

    void TriggerBass(int32_t exciter)
    {
        bassEnv = 4095;
        bassPitch = 9 + (smoothTune >> 9) + (Abs32(exciter) >> 10);
        if(bassPitch < 6) bassPitch = 6;
        if(bassPitch > 42) bassPitch = 42;
    }

    int32_t ProcessBassStage(int32_t chopped, bool bendHeld)
    {
        int32_t sign = chopped >= 0 ? 1 : -1;
        bool inputStrike = (sign != lastChopSign) && (Abs32(chopped) > 384);
        lastChopSign = sign;

        if(PulseIn1RisingEdge())
        {
            TriggerBass(chopped + 1024);
            pulse1ExternalActive = true;
            pulse1ExternalTimeout = 48000;
        }

        if(pulse1ExternalTimeout > 0)
        {
            pulse1ExternalTimeout--;
        }
        else
        {
            pulse1ExternalActive = false;
        }

        if(!pulse1ExternalActive && inputStrike)
        {
            TriggerBass(chopped);
        }

        internalStrikePeriod = 24000u - ((uint32_t)smoothChop << 2);
        if(internalStrikePeriod < 3000u)
        {
            internalStrikePeriod = 3000u;
        }

        internalStrikeCounter++;
        if(!pulse1ExternalActive && internalStrikeCounter >= internalStrikePeriod)
        {
            internalStrikeCounter = 0;
            TriggerBass(chopped >> 2);
        }
        else if(pulse1ExternalActive && internalStrikeCounter >= internalStrikePeriod)
        {
            internalStrikeCounter = 0;
        }

        if(bassEnv > 0)
        {
            int32_t decay = 4 + ((4095 - smoothGain) >> 9);
            if(bendHeld)
            {
                decay += 2;
            }
            bassEnv -= decay;
            if(bassEnv < 0)
            {
                bassEnv = 0;
            }
        }

        bassPhase += bassPitch;
        bassPhase &= 4095;

        int32_t tri = (bassPhase & 2048) ? (2048 - (bassPhase & 2047)) : (bassPhase & 2047);
        tri -= 1024;
        tri <<= 1;

        int32_t bass = (tri * bassEnv) >> 9;
        bass += ((chopped * bassEnv) >> 13);
        return SoftClip(bass);
    }

    virtual void ProcessSample()
    {
        if(sampleCounter == 0)
        {
            SeedRng();
        }

        sampleCounter++;

        if((sampleCounter & (kControlDiv - 1)) == 0)
        {
            UpdateControls();
        }

        bool bendHeld = SwitchVal() == Switch::Down;

        int32_t driven = ProcessInputStage(bendHeld);
        int32_t subMix = ProcessDividerStage(driven);
        int32_t inputVoice = SoftClip(driven + subMix);

        int32_t squareVoice = ProcessSquareStage(bendHeld);
        int32_t chopped = ProcessChopperStage(inputVoice, squareVoice, bendHeld);
        int32_t bass = ProcessBassStage(chopped, bendHeld);

        int32_t wetTap = ReadDelay(delayWrite - (96 + (smoothTouch >> 3)));
        int32_t delayInput = chopped + (bass >> 1);
        int32_t feedbackAmt = 384 + (smoothTouch >> 2);
        if(bendHeld)
        {
            feedbackAmt += 512;
        }

        int32_t delayWriteValue = delayInput + ((wetTap * feedbackAmt) >> 12);
        delayWriteValue = SoftClip(delayWriteValue);
        WriteDelay(delayWrite, delayWriteValue);
        delayWrite = (delayWrite + 1) & kDelayMask;

        int32_t outA = chopped + (inputVoice >> 2) + (wetTap >> 2);
        int32_t outB = bass + (wetTap >> 1) - (squareVoice >> 3) + (chopped >> 3);

        if(SwitchVal() == Switch::Up)
        {
            outA += squareVoice >> 3;
            outB += squareVoice >> 2;
        }

        if(bendHeld)
        {
            outA += ((int32_t)(Random() & 127) - 64) << 2;
            outB -= ((int32_t)(Random() & 127) - 64) << 1;
        }

        outA = SoftClip(outA);
        outB = SoftClip(outB);

        AudioOut1(Clamp16(outA));
        AudioOut2(Clamp16(outB));

        PulseOut1(bassEnv > 1024);
        PulseOut2(chopState);

        if((sampleCounter & (kSlowDiv - 1)) == 0)
        {
            displayLevel += ((Abs32(outA) - displayLevel) >> 3);

            int32_t ledLevel = displayLevel << 1;
            if(ledLevel > 4095) ledLevel = 4095;

            LedBrightness(0, inputConnected ? 4095 : 256);
            LedBrightness(1, smoothGain);
            LedBrightness(2, smoothChop);
            LedBrightness(3, smoothTune);
            LedBrightness(4, ledLevel);
            LedBrightness(5, bendHeld ? 4095 : (SwitchVal() == Switch::Up ? 2048 : 256));
        }
    }
};

int main()
{
    set_sys_clock_khz(144000, true);

    CastleProcess card;
    card.EnableNormalisationProbe();
    card.Run();
}
