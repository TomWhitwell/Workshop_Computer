//
// BitPhase.cpp
//

#include "ComputerCard.h"

class BitPhase : public ComputerCard
{
public:
    virtual void ProcessSample() override
    {
        int32_t in = AudioIn1();
        
        int32_t rateKnob  = KnobVal(Knob::Main);
        int32_t depthKnob = KnobVal(Knob::X);
        int32_t yKnob     = KnobVal(Knob::Y);
        
        // CV inputs

        int32_t rateCV = CVIn1();
        int32_t resCV  = CVIn2();
        // Clamp
        
        if (rateKnob < 0 ) rateKnob = 0;
        if (rateKnob > 4095) rateKnob = 4095;
        if (depthKnob < 0) depthKnob = 0;
        if (depthKnob > 4095) depthKnob = 4095;

        if (yKnob < 0) yKnob = 0;
        if (yKnob > 4095) yKnob = 4095;
        
       
        
        //----------------------------------------
        // Mode select

        enum Mode { PHASER_ONLY, MIX, BURST };

        Mode mode;
        switch (SwitchVal())
        {
            case Switch::Up:    mode = PHASER_ONLY; break;
            case Switch::Middle:mode = MIX; break;
            default:            mode = BURST; break;
        }
        
        //-----------------------------------------
        //Pulse sync
        
        
        bool pulse = PulseIn1();

        if (pulse && !lastPulse)
        {
            phaserPhase = 0;
        }

        lastPulse = pulse;
        
        
        //----------------------------------------
        // LFO clocks
        
        int32_t modRate =
            rateKnob + (rateCV >> 1);

        if (modRate < 0) modRate = 0;
        if (modRate > 4095) modRate = 4095;

        float ratenorm =
            (float)modRate / 4095.0f;

        float slowAmt = ratenorm * ratenorm;
        float fastAmt = ratenorm;   // from earlier knob normalization

        float phaserHz = 0.05f + slowAmt * (6.0f - 0.05f);

        uint32_t phaserInc =
            (uint32_t)((phaserHz * PHASE_SCALE) / FS);


        float tremHz = 0.5f + fastAmt * (20.0f - 0.5f);

        uint32_t tremInc =
            (uint32_t)((tremHz * PHASE_SCALE) / FS);

        
        if (mode != BURST)
        {
            phaserPhase += phaserInc;
            tremPhase   += tremInc;
            }
        else
        {
            float burstHz = 12.0f;

            phaserPhase +=
                (uint32_t)((burstHz * PHASE_SCALE) / FS);
            tremPhase   += 50000;
        }
        
        //----------------------------------------
        // Triangle LFO generator

        auto triLFO = [](uint32_t p) -> int32_t
        {
            uint32_t v = (p >> 20) & 4095;
            int32_t x = (v < 2048) ? v : (4095 - v);
            return (x - 1024) << 1;
        };

        int32_t phaserLfo = triLFO(phaserPhase);
        int32_t tremLfo   = triLFO(tremPhase);

        //----------------------------------------
        // Depth control (X = phaser intensity)

        int32_t center = 2048;

        int32_t mod =
            (phaserLfo * depthKnob) >> 12;

        int32_t sweep = center + mod;
        
        if (sweep < 512)  sweep = 512;
        if (sweep > 3584) sweep = 3584;
        
        //----------------------------------------
        // Tremolo

        int32_t tremGain =
            2048 + ((tremLfo * yKnob) >> 11);

        if (tremGain < 0)
            tremGain = 0;


        //int32_t tremolo = in;
        
        //----------------------------------------
        // Grand Orbiter style phaser

        int32_t input = in + fb;

        // sweep -> allpass coefficient
        int32_t coeff =
            100 + ((sweep * 2200) >> 12);

        if (coeff > 2200) coeff = 2200;
        if (coeff < -2200) coeff = -2200;

        // spread stages slightly
        // spread stages like Grand Orbiter

        int32_t c1 = coeff - 300;
        int32_t c2 = coeff - 100;
        int32_t c3 = coeff + 100;
        int32_t c4 = coeff + 300;

        // keep all stages stable
        if (c1 > 1800) c1 = 1800;
        if (c1 < -1800) c1 = -1800;
        
        if (c2 > 1800) c2 = 1800;
        if (c2 < -1800) c2 = -1800;

        if (c3 > 1800) c3 = 1800;
        if (c3 < -1800) c3 = -1800;

        if (c4 > 1800) c4 = 1800;
        if (c4 < -1800) c4 = -1800;

        auto allpass = [](int32_t x,
                          int32_t a,
                          int32_t &x1,
                          int32_t &y1)
        {
            int32_t y =
                (((-a * x) >> 11) +
                  x1 +
                 ((a * y1) >> 11));

            x1 = x;
            y1 = y;

            return y;
        };

        int32_t x = input;
        //input += (input - fb) >> 1;

        x = allpass(x, c1, ap1_x1, ap1_y1);
        x = allpass(x, c2, ap2_x1, ap2_y1);
        x = allpass(x, c3, ap3_x1, ap3_y1);
        x = allpass(x, c4, ap4_x1, ap4_y1);

        // wet path
        int32_t wet = x;

        // classic phaser mix
        int32_t phaser =
            ((input * 4096) -
             (wet   * 3072)) >> 12;
        
        int32_t tremPhaser =
            (phaser * tremGain) >> 11;
        //makeup gain
        phaser = (phaser * 3) >> 1;   // 1.5x
        
        // DC blocker
        dc += (phaser - dc) >> 10;
        phaser -= dc;
        
        
        //feedback
       
        int32_t resonance =
            yKnob + (resCV >> 1);

        if (resonance < 0)
            resonance = 0;

        if (resonance > 4095)
            resonance = 4095;

        // 25% -> 115% feedback
        int32_t feedbackAmt;

        if (resonance < 3600)
        {
            feedbackAmt =
                512 + ((resonance * 1300) >> 12);
        }
        else
        {
            int32_t extra = resonance - 3600;

            feedbackAmt =
                1650 + ((extra * 450) >> 9);
        }
        fb = (wet * feedbackAmt) >> 11;

        if (fb > 25000) fb = 25000;
        if (fb < -25000) fb = -25000;
        
        //----------------------------------------
        // Corruption amount

        int32_t corruption = (yKnob * 5) >> 3;

        if (mode == BURST)
            corruption += 1500;

        if (corruption > 3000)
            corruption = 3000;

        //----------------------------------------
        // Mode routing

        int32_t output = phaser;
        
        if (mode == MIX)
        {
            output =
                ((phaser * 2048) +
                 (tremPhaser * 2048)) >> 12;
        }
        else if (mode == BURST)
        {
            int32_t dry = phaser + (phaser >> 1);
           
            //----------------------------------------
            // Sample & Hold (slightly more active)

            uint32_t holdLength = 1 + (corruption >> 8);
            if (holdLength > 24) holdLength = 24;  // slightly tighter than before

            if (++holdCounter >= holdLength)
            {
                heldSample = dry;
                holdCounter = 0;
            }

            int32_t held = heldSample;

            // blend (keeps presence)
            output = (dry * 2816 + held * 1216) >> 12;

            //----------------------------------------
            // Bit reduction (slightly stronger but still musical)

            uint32_t maskShift = corruption >> 10;
            if (maskShift > 6) maskShift = 6;

            int32_t crushed = (output >> maskShift) << maskShift;

            output = (output * 2560 + crushed * 1536) >> 12;

            //----------------------------------------
            // Repeat corruption (a bit more active)

            rng = rng * 1664525u + 1013904223u;

            uint32_t repeatChance = corruption >> 3;
            if (repeatChance > 640) repeatChance = 640;

            if ((rng & 0x3FF) < repeatChance)
            {
                output = (output * 1792 + previousOutput * 2304) >> 12;
            }

            previousOutput = output;
            
            // keep energy alive (prevents perceived collapse)
            int32_t presence = dry + (dry >> 2);  // +25% reinforcement
            output = (output * 3072 + presence * 1024) >> 12;
        }
        
        output = (output * 9) >> 3;  // +12dB-ish compensation
        
        // lo pass filtration
        
        //lp += (output - lp) >> 5;
        //output = lp;
        
        //----------------------------------------
        // Output

        AudioOut1(output);
        AudioOut2(output);

        //----------------------------------------
        // CV

        CVOut1(phaserLfo);
        CVOut2(tremLfo);

        //----------------------------------------
        // Pulses

        PulseOut1(mode == BURST);

        bool lfoPulse = (phaserPhase & 0x80000000);
        PulseOut2(lfoPulse);

        //----------------------------------------
        // LEDs (fixed scaling)

        LedBrightness(0, rateKnob);

        int32_t lfoLed = (phaserLfo + 2048);
        if (lfoLed < 0) lfoLed = 0;
        if (lfoLed > 4095) lfoLed = 4095;
        LedBrightness(1, lfoLed);

        int32_t sweepLed = sweep;
        if (sweepLed > 4095) sweepLed = 4095;
        LedBrightness(2, sweepLed);

        int32_t corrLed = corruption;
        if (corrLed > 4095) corrLed = 4095;
        LedBrightness(3, corrLed);

        LedOn(4, mode == BURST);
        LedOn(5, PulseIn1());
    }

private:
    
    static constexpr float FS = 48000.0f;
    static constexpr float PHASE_SCALE = 4294967296.0f;
    
    uint32_t phaserPhase = 0;
    uint32_t tremPhase   = 0;

    int32_t ap1_x1 = 0, ap1_y1 = 0;
    int32_t ap2_x1 = 0, ap2_y1 = 0;
    int32_t ap3_x1 = 0, ap3_y1 = 0;
    int32_t ap4_x1 = 0, ap4_y1 = 0;
    int32_t fb = 0;
    int32_t lp = 0;
    
    uint32_t rng = 1;

    uint32_t holdCounter = 0;
    int32_t heldSample = 0;

    int32_t previousOutput = 0;
    int32_t dc = 0;
    
    bool lastPulse = false;
};

int main()
{
    BitPhase card;
    card.Run();
}
