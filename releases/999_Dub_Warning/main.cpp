// Dub Warning
// A lightweight playable siren, PT2399-style echo, and modulation source for the
// Music Thing Modular Workshop Computer.

#include "ComputerCard.h"
#include "hardware/clocks.h"

#include <stdint.h>

class DubWarning : public ComputerCard
{
public:
    static constexpr int32_t kEchoSize = 16384;
    static constexpr int32_t kMaxAudio = 2047;
    static constexpr int32_t kMinAudio = -2048;
    static constexpr int32_t kPulseLength = 1600;
    static constexpr int32_t kWheelLength = 24576;

    int16_t echo[kEchoSize] = {};

    uint32_t oscPhase = 0;
    uint32_t lfoPhase = 0;
    uint32_t wobblePhase = 0;
    uint32_t wheelPhase = 0;
    uint32_t noise = 0x12345678;

    int32_t echoWrite = 0;
    int32_t delayFilter = 0;
    int32_t smearFilter = 0;
    int32_t gateEnv = 0;
    int32_t hitEnv = 0;
    int32_t hitDecay = 8;
    int32_t gateDecayClock = 0;
    int32_t wheelTimer = 0;
    int32_t pulseTimer1 = 0;
    int32_t pulseTimer2 = 0;
    int32_t lastTone = 0;

    bool latched = true;

    virtual void ProcessSample()
    {
        const int32_t mainKnob = KnobVal(Knob::Main);
        const int32_t xKnob = KnobVal(Knob::X);
        const int32_t yKnob = KnobVal(Knob::Y);
        const int32_t mainControl = Connected(Input::Audio2) ? clampInt(AudioIn2() + 2048, 0, 4095) : mainKnob;
        const int32_t xPlay = tameUpperTravel(xKnob);
        const int32_t yPlay = tameUpperTravel(yKnob);
        const int32_t cvPitch = clamp12(AudioIn1()) + clamp12(CVIn1());
        const int32_t cvColor = clamp12(CVIn2());
        const int32_t switchPosition = SwitchVal();

        if(PulseIn1RisingEdge())
        {
            oscPhase = 0;
            gateEnv = 4095;
            hitEnv = 4095;
            hitDecay = 8;
            latched = true;
        }

        if(PulseIn2RisingEdge())
        {
            oscPhase = 0;
            lfoPhase = 0;
            hitEnv = 4095;
            hitDecay = 4;
            latched = true;
        }

        if(SwitchChanged() && switchPosition == Switch::Down)
        {
            oscPhase = 0;
            lfoPhase = 0;
            wheelPhase = 0;
            wheelTimer = kWheelLength;
            gateEnv = 4095;
            hitEnv = 4095;
            hitDecay = 8;
            latched = true;
        }

        if(switchPosition == Switch::Down && wheelTimer > 0)
        {
            latched = true;
        }
        else if(switchPosition == Switch::Middle)
        {
            latched = false;
        }

        if(!latched && gateEnv > 0)
        {
            gateDecayClock += 1;

            if(gateDecayClock >= 6)
            {
                gateEnv -= 1;
                gateDecayClock = 0;
            }
        }
        else
        {
            gateDecayClock = 0;
        }

        if(hitEnv > 0)
        {
            hitEnv -= hitDecay;
        }

        if(gateEnv < 0)
        {
            gateEnv = 0;
        }

        if(hitEnv < 0)
        {
            hitEnv = 0;
        }

        const bool wheelActive = wheelTimer > 0;
        const int32_t wheelEnv = wheelActive ? clampInt(wheelTimer >> 2, 0, 4095) : 0;

        if(wheelTimer > 0)
        {
            wheelTimer -= 1;
        }

        const int32_t lfoRate = 120 + ((xPlay * xPlay) >> 8);
        const int32_t wobbleRate = 55 + (yPlay >> 5);
        const int32_t wheelRate = 90000000 + (xPlay << 14);
        lfoPhase += (uint32_t)lfoRate;
        wobblePhase += (uint32_t)wobbleRate;
        wheelPhase += (uint32_t)wheelRate;

        const int32_t lfo = triangle(lfoPhase);
        const int32_t wobble = triangle(wobblePhase);
        const int32_t wheelTrill = triangle(wheelPhase);
        const int32_t bend = wheelActive ? wheelTrill : (switchPosition == Switch::Up ? lfo : -lfo);
        const int32_t baseStep = 2500000 + ((mainControl * mainControl) << 2);
        const int32_t wheelDrop = wheelActive ? ((kWheelLength - wheelTimer) << 10) : 0;
        const int32_t sweepStep = wheelActive ? ((xPlay + 2048) * bend) << 3 : (xPlay * bend) << 2;
        const int32_t cvStep = cvPitch << 12;
        const uint32_t phaseStep = (uint32_t)clampInt(baseStep + sweepStep + cvStep + wheelDrop, 250000, 140000000);
        oscPhase += phaseStep;

        const int32_t tri = triangle(oscPhase);
        const int32_t square = (oscPhase & 0x80000000u) ? 2047 : -2048;
        const int32_t shape = clampInt(1900 + (cvColor >> 2), 800, 3300);
        int32_t tone = ((tri * (4095 - shape)) + (square * shape)) >> 12;

        noise = (noise * 1664525u) + 1013904223u;
        tone += (((int32_t)(noise >> 21) - 1024) * (hitEnv >> 7)) >> 6;

        const int32_t drive = 4300 + ((cvColor + 2048) >> 3);
        tone = softClip((tone * drive) >> 12);

        const int32_t gatedAmp = gateEnv > hitEnv ? gateEnv : hitEnv;
        const int32_t amp = wheelActive ? wheelEnv : (switchPosition == Switch::Middle ? gatedAmp : 4095);
        tone = (tone * amp) >> 12;
        tone = (tone + lastTone) >> 1;
        lastTone = tone;

        const int32_t delayBase = 760 + ((yPlay * yPlay) >> 10);
        const int32_t delayMod = (wobble * (768 + (yPlay >> 3))) >> 12;
        const int32_t delaySamples = clampInt(delayBase + delayMod, 420, kEchoSize - 2);
        const int32_t readIndex = (echoWrite - delaySamples) & (kEchoSize - 1);
        const int32_t delayed = echo[readIndex];
        const int32_t darken = 2 + (yPlay >> 11);
        delayFilter += (delayed - delayFilter) >> darken;
        smearFilter += (delayFilter - smearFilter) >> 3;

        noise = (noise * 1664525u) + 1013904223u;
        const int32_t clockHash = ((int32_t)(noise >> 22) - 512) * (yPlay >> 9);
        const int32_t clockDirt = switchPosition == Switch::Middle ? ((clockHash * amp) >> 12) : clockHash;
        const int32_t repeats = wheelActive ? 3000 + (yPlay >> 3) : 820 + ((yPlay * 7) >> 3);
        int32_t echoIn = tone + ((smearFilter * repeats) >> 12) + (clockDirt >> 5);
        echoIn = softClip(echoIn);

        const int32_t bitLoss = yPlay >> 11;
        echoIn = (echoIn >> bitLoss) << bitLoss;

        echo[echoWrite] = (int16_t)echoIn;
        echoWrite = (echoWrite + 1) & (kEchoSize - 1);

        const int32_t wet = wheelActive ? clampInt(1800 + (yPlay >> 1), 0, 3600) : clampInt(800 + ((yPlay * 3) >> 2), 0, 3400);
        const int32_t mixed = softClip(((tone * (4095 - wet)) + (smearFilter * wet)) >> 12);
        const int32_t out2 = softClip(tone + ((delayFilter * (1400 + (yPlay >> 2))) >> 12));

        AudioOut1(mixed);
        AudioOut2(out2);
        CVOut1(clamp12(lfo));
        CVOut2(clamp12((gateEnv - 2048) + (hitEnv >> 2)));

        pulseTimer1 = gateEnv > 160 ? kPulseLength : pulseTimer1 - 1;
        pulseTimer2 = hitEnv > 120 ? kPulseLength : pulseTimer2 - 1;
        PulseOut1(pulseTimer1 > 0);
        PulseOut2(pulseTimer2 > 0);

        LedBrightness(0, amp);
        LedBrightness(1, mainControl);
        LedBrightness(2, xKnob);
        LedBrightness(3, yKnob);
        LedBrightness(4, clampInt(lfo + 2048, 0, 4095));
        LedBrightness(5, wheelActive ? wheelEnv : ((hitEnv > 0) ? hitEnv : clampInt(smearFilter + 2048, 0, 4095)));
    }

private:
    static int32_t clampInt(int32_t value, int32_t lo, int32_t hi)
    {
        if(value < lo)
        {
            return lo;
        }

        if(value > hi)
        {
            return hi;
        }

        return value;
    }

    static int32_t clamp12(int32_t value)
    {
        return clampInt(value, kMinAudio, kMaxAudio);
    }

    static int32_t tameUpperTravel(int32_t value)
    {
        if(value <= 3072)
        {
            return value;
        }

        return 3072 + ((value - 3072) >> 2);
    }

    static int32_t triangle(uint32_t phase)
    {
        int32_t value = (int32_t)((phase >> 20) & 4095);

        if(value >= 2048)
        {
            value = 4095 - value;
        }

        return (value << 1) - 2048;
    }

    static int32_t softClip(int32_t value)
    {
        if(value > 2300)
        {
            return kMaxAudio;
        }

        if(value < -2300)
        {
            return kMinAudio;
        }

        return clamp12(value);
    }
};

DubWarning card;

int main()
{
    set_sys_clock_khz(144000, true);
    card.EnableNormalisationProbe();
    card.Run();
}
