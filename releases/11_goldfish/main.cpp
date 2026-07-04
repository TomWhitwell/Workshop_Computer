#include "ComputerCard.h"
#include "quantiser.h"
#include "divider.h"
#include "goldfish_stream.h"
#include "pico/multicore.h"

// Core 1 entry point: continuously service flash streaming I/O (sector
// erase-ahead + page programming) so the core 0 audio path never blocks.
static void __not_in_flash_func(goldfish_core1_entry)()
{
    while (true)
    {
        goldfish_stream_io_task();
    }
}

// 12 bit random number generator
uint32_t __not_in_flash_func(rnd12)()
{
    static uint32_t lcg_seed = 1;
    lcg_seed = 1664525 * lcg_seed + 1013904223;
    return lcg_seed >> 20;
}

// Zero crossing detector
bool __not_in_flash_func(zeroCrossing)(int16_t a, int16_t b)
{
    return (a < 0 && b >= 0) || (a >= 0 && b < 0);
};

// Highpass filter for delay
int32_t __not_in_flash_func(highpass_process)(int32_t *out, int32_t b, int32_t in)
{
    *out += (((in - *out) * b) >> 16);
    return in - *out;
}

/// Goldfish class
class Goldfish : public ComputerCard
{
public:
    Goldfish()
    {
        // constructor
        runMode = SwitchVal() == Switch::Middle ? PLAY : DELAY;
        startPosL = KnobVal(Knob::X) * bufSize >> 4;
        startPosR = KnobVal(Knob::Y) * bufSize >> 4;

        writeInd = 0;
        readIndL = 0;
        readIndR = 0;
        cvsL = 0;
        cvsR = 0;
        halftime = false;
        startupCounter = 400;
        divisor = 1;
        internalClockRate = 1;
    };

    /// Main audio processing function called at 48kHz
    virtual void __not_in_flash_func(ProcessSample)()
    {
        halftime = !halftime;

        // simple startup counter to allow time for initialisation
        if (startupCounter)
            startupCounter--;

        if (startupCounter == 0)
        {
            bool risingEdge1 = PulseIn1RisingEdge();
            bool risingEdge2 = PulseIn2RisingEdge();

            // Read knobs
            main = KnobVal(Knob::Main);
            x = KnobVal(Knob::X);
            y = KnobVal(Knob::Y);

            // Virtual detent the knob values
            main = virtualDetentedKnob(main);
            x = virtualDetentedKnob(x);
            y = virtualDetentedKnob(y);

            // Calculate big knob + audio2 attenuversion parameter. -2048 to 2047.
            if (Connected(Input::Audio2))
            {
                bigKnob_CV = (AudioIn2() * (2048 - main) >> 12) + 1;
            }
            else
            {
                bigKnob_CV = 2048 - main + 1;
            }

            // Hainbach says half time is the best time (no really I'm just buying more delay time)
            if (halftime)
            {
                // 12 bit noise scaled appropriately
                int16_t noise = rnd12() - 2048;

                // Read switch
                Switch s = SwitchVal();

                bool pulseL = false;
                bool pulseR = false;

                // Read inputs
                cv1 = CVIn1();               // -2048 to 2047
                cv2 = CVIn2();               // -2048 to 2047
                int16_t audioL = AudioIn1(); // -2048 to 2047
                int16_t audioR = AudioIn2(); // -2048 to 2047

                // 12kHz notch filter, to remove interference from mux lines (only left channel)
                //Thank you to Chris Johnson for the notch filter code

                audioL <<= 2;

               int32_t ooa0 = 8192, a2oa0 = 8092; // Q=100;
                //	int32_t ooa0=7801, a2oa0=7411;//Q=10;
                int32_t audioLf = (ooa0 * (audioL + audioL2) - a2oa0 * audioLf2) >> 14;
                audioL2 = audioL1;
                audioL1 = audioL;
                audioLf2 = audioLf1;
                audioLf1 = audioLf;

                audioL >>= 2;
                audioLf >>= 2;

                int16_t lastSampleL = 0;
                int16_t lastSampleR = 0;

                int16_t fromBufferL = 0;
                int16_t fromBufferR = 0;


                // internal clock rate and divisor
                internalClockRate = cabs(x - 2048) * 50 >> 12 + 1;
                divisor = (cabs(y - 2048) * 16 >> 12) + 1;


                //here we decide the read/write state based on the switch position and set the mode accordingly
                if ((s == Switch::Down) && (lastSwitchVal != Switch::Down))
                {
                    runMode = RECORD;
                    loopLength = 0;
                    writeInd = 0;
                    goldfish_stream_record_start();
                    goldfish_stream_set_heads(NULL, NULL);
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                }
                else if ((s == Switch::Up) && (lastSwitchVal != Switch::Up))
                {
                    runMode = DELAY;
                    goldfish_stream_delay_start();
                    goldfish_stream_head_init(&playHeadL);
                    goldfish_stream_head_init(&playHeadR);
                    goldfish_stream_set_heads(&playHeadL, &playHeadR);
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                }
                else if ((s == Switch::Middle) && (lastSwitchVal != Switch::Middle))
                {
                    runMode = PLAY;
                    goldfish_stream_record_stop();
                    loopLength = goldfish_stream_recorded_samples();
                    if (loopLength < 1) loopLength = 1;
                    goldfish_stream_head_init(&playHeadL);
                    goldfish_stream_head_init(&playHeadR);
                    goldfish_stream_set_heads(&playHeadL, &playHeadR);
                    calculateStartPos();
                    phaseL = startPosL;
                    phaseR = startPosR;
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                };

                cvMix = calcCVMix(noise);

                // Internal clock
                internalClockCounter += internalClockRate;

                if (internalClockCounter >= bufSize >> 2)
                {
                    internalClockCounter = 0;

                    if (!Connected(Input::Pulse1))
                    {
                        pulseL = true;
                        pulseR = clockDivider.Step(true);
                        if (pulseR)
                        {
                            clockDivider.SetResetPhase(divisor);
                        }
                    }
                }

                lastSwitchVal = s;


                // Main audio processing depending on mode (record, delay, play)
                switch (runMode)
                {
                case DELAY:
                {
                    
                    //Delay code is a mutated version of Chris Johnson's Utility Pair delay code
                    //In delay mode the audio is written to the delay buffer and read back with a delay time set by the big knob
                    //each output (left and right) is read back with a different delay time, set by the big knob and the CV input
                    
                    //CVout2 is set to the quantised CV mix, the quantiser is also from Chris Johnson's Utility Pair project
                    qSample = quantSample(cvMix);

                    int32_t k = (bigKnob_CV + 2048) >> 1; //2048 to 0

                    int32_t kL = 2 * k + 1024;
                    int32_t kR = -2 * k + 5120;

                    int64_t cvL = kL;
                    int64_t cvR = kR;

                    if (cvL > 4095)
                        cvL = 4095;
                    if (cvL < 0)
                        cvL = 0;

                    if (cvR > 4095)
                        cvR = 4095;
                    if (cvR < 0)
                        cvR = 0;

                    int64_t cvtargL = cvL * cvL / 50;
                    int64_t cvtargR = cvR * cvR / 50;
                    cvsL = (cvsL * 255 + (cvtargL << 4)) >> 8;
                    cvsR = (cvsR * 255 + (cvtargR << 4)) >> 8;

                    int64_t cvs1L = cvsL >> 7;
                    int64_t cvs1R = cvsR >> 7;

                    int64_t rL = cvsL & 0x7F;
                    int64_t rR = cvsR & 0x7F;

                    // Record the high-passed input into the wrapping flash delay line.
                    int32_t buf_write = highpass_process(&hpf, 200, audioLf);
                    goldfish_stream_record_sample((int16_t)buf_write, cvMix);

                    // Clamp the delay so reads stay safely behind the write head
                    // (data already flushed to flash) and within the region. The
                    // floor also gives the read head enough runway (delay - backlog)
                    // to coast through a flash sector erase (~tens of ms) without
                    // the ring underrunning.
                    const uint32_t MIN_DELAY = 3600u;
                    uint32_t capSamp = goldfish_stream_capacity_samples();
                    uint32_t maxDelay = (capSamp > 8192u) ? (capSamp - 8192u) : MIN_DELAY;
                    if (cvs1L < (int64_t)MIN_DELAY) cvs1L = MIN_DELAY;
                    if (cvs1R < (int64_t)MIN_DELAY) cvs1R = MIN_DELAY;
                    if (cvs1L > (int64_t)maxDelay) cvs1L = maxDelay;
                    if (cvs1R > (int64_t)maxDelay) cvs1R = maxDelay;

                    // Read back at the delay offset behind the write head, via the
                    // core-1 ring heads (flash is being erased, so no direct reads).
                    uint32_t wi = goldfish_stream_write_index();
                    int32_t fromBufferL = 0;
                    int32_t fromBufferR = 0;
                    if (wi > (uint32_t)cvs1L + 1u)
                    {
                        uint32_t readL1 = wi - (uint32_t)cvs1L - 1u;
                        int32_t b1 = goldfish_stream_head_read(&playHeadL, readL1);
                        int32_t b2 = goldfish_stream_head_read(&playHeadL, readL1 - 1u);
                        fromBufferL = (b2 * rL + b1 * (128 - rL)) >> 7;
                    }
                    if (wi > (uint32_t)cvs1R + 1u)
                    {
                        uint32_t readR1 = wi - (uint32_t)cvs1R - 1u;
                        int32_t b1 = goldfish_stream_head_read(&playHeadR, readR1);
                        int32_t b2 = goldfish_stream_head_read(&playHeadR, readR1 - 1u);
                        fromBufferR = (b2 * rR + b1 * (128 - rR)) >> 7;
                    }

                    outL = fromBufferL;
                    outR = fromBufferR;
                    outCV = cvMix;

                    break;
                }
                case RECORD:
                {

                    // in record mode the audio is written to the delay buffer and the CV input is written to the CV buffer
                    qSample = quantSample(cvMix);

                    goldfish_stream_record_sample((int16_t)audioLf, cvMix);

                    outL = audioLf;
                    outR = audioLf;

                    outCV = cvMix;

                    writeInd++;
                    if (writeInd >= bufSize)
                        writeInd = 0;

                    loopLength++;
                    if (loopLength > bufSize)
                    {
                        loopLength = bufSize;
                    }

                    break;
                }
                case PLAY:
                {
                    // Playback reads the recording from flash. Wait until any
                    // just-finished recording has flushed (no core-1 erase in
                    // flight) before touching the flash bus.
                    if (!goldfish_stream_io_idle())
                    {
                        outL = 0;
                        outR = 0;
                        break;
                    }

                    //Play code is a mutated version of Chris Johnson's Utility Pair Looper
                    //In play mode the audio is read back from the delay buffer with a playback speed set by the big knob

                    int32_t k = (2048 - bigKnob_CV) >> 1;
                    int32_t dphaseL;
                    int32_t dphaseR;

                    if (Connected(Input::Audio2))
                    {
                        dphaseL = k + (bigKnob_CV + 1024);
                        dphaseR = k + (bigKnob_CV - 1024);
                    }
                    else
                    {
                        dphaseL = k;
                        dphaseR = k;
                    }

                    dphaseL -= 1024;
                    dphaseR -= 1024;

                    phaseL += dphaseL >> 1;
                    phaseR += dphaseR >> 1;

                    if (loopLength < 1)
                    {
                        loopLength = bufSize;
                    }

                    calculateStartPos();

                    //This is really only a best effort to reduce discontinuities in the audio output that cause clicks on reset
                    //It's not perfect and could be improved
                    checkZero = zeroCrossing(outL, lastSampleL);

                    lastSampleL = fromBufferL;
                    lastSampleR = fromBufferR;

                    if (reset && ((checkZero && Connected(Input::Audio1)) || !Connected(Input::Audio1)))
                    {
                        reset = false;
                        phaseL = startPosL;
                        phaseR = startPosR;
                        pulseL = true;
                        pulseR = true;
                        clockDivider.SetResetPhase(divisor);
                        internalClockCounter = 0;
                    };

                    if (phaseL < 0)
                    {
                        phaseL += loopLength << 8;
                        clockDivider.SetResetPhase(divisor);
                        pulseL = true;
                        pulseR = clockDivider.Step(true);
                    }
                    if (phaseL > (loopLength << 8))
                    {
                        phaseL -= loopLength << 8;
                        pulseL = true;
                        clockDivider.SetResetPhase(divisor);
                        pulseR = clockDivider.Step(true);
                    }

                    if (phaseR < 0)
                    {
                        phaseR += loopLength << 8;
                    }
                    if (phaseR > (loopLength << 8))
                    {
                        phaseR -= loopLength << 8;
                    }

                    int32_t rL = phaseL & 0xFF;
                    int32_t readIndL = phaseL >> 8;
                    int32_t rR = phaseR & 0xFF;
                    int32_t readIndR = phaseR >> 8;

                    // Decode audio from flash via the per-head streaming readers.
                    int32_t sL0 = goldfish_stream_head_read(&playHeadL, readIndL);
                    int32_t sL1 = goldfish_stream_head_read(&playHeadL, (readIndL + 1) % loopLength);
                    int32_t sR0 = goldfish_stream_head_read(&playHeadR, readIndR);
                    int32_t sR1 = goldfish_stream_head_read(&playHeadR, (readIndR + 1) % loopLength);

                    int32_t fadeLength = loopLength; // Adjust this value as needed for the fade length

                    // Apply fade-out at the end of the loop
                    if (phaseL >= (loopLength << 8) - fadeLength)
                    {
                        int32_t fadeOutFactor = ((loopLength << 8) - phaseL) * 256 / fadeLength;
                        outL = ((sL0 << 3) * (256 - rL) + (sL1 << 3) * (rL)) * fadeOutFactor >> 8;
                    }
                    else
                    {
                        outL = (sL0 << 3) * (256 - rL) + (sL1 << 3) * (rL);
                    }

                    // Apply fade-in at the beginning of the loop
                    if (phaseL < fadeLength)
                    {
                        int32_t fadeInFactor = phaseL * 256 / fadeLength;
                        outL = outL * fadeInFactor >> 8;
                    }

                    if (phaseR >= (loopLength << 8) - fadeLength)
                    {
                        int32_t fadeOutFactor = ((loopLength << 8) - phaseR) * 256 / fadeLength;
                        outR = ((sR0 << 3) * (256 - rR) + (sR1 << 3) * (rR)) * fadeOutFactor >> 8;
                    }
                    else
                    {
                        outR = (sR0 << 3) * (256 - rR) + (sR1 << 3) * (rR);
                    }

                    if (phaseR < fadeLength)
                    {
                        int32_t fadeInFactor = phaseR * 256 / fadeLength;
                        outR = outR * fadeInFactor >> 8;
                    }

                    if (loopLength > 0)
                    {
                        outCV = (goldfish_stream_read_cv(readIndL) * (256 - rL) + goldfish_stream_read_cv((readIndL + 1) % loopLength) * rL) >> 8;
                        qSample = quantSample(outCV);
                    }

                    outL >>= 11;
                    outR >>= 11;

                    break;
                }
                };

                clip(outL);
                clip(outR);
                AudioOut1(outL);
                AudioOut2(outR);
                CVOut1(outCV);

                LedBrightness(0, cabs(outL));
                LedBrightness(1, cabs(outR));

                if (risingEdge2 || lastRisingEdge2)
                {
                    reset = true;
                };

                if (risingEdge1 || lastRisingEdge1)
                {
                    pulseL = true;
                    pulseR = clockDivider.Step(true);
                    if (pulseR)
                    {
                        clockDivider.SetResetPhase(divisor);
                    }
                }

                if (pulseL)
                {
                    pulseL = false;
                    pulseTimer1 = 200;
                    PulseOut1(true);
                    LedOn(4);
                    CVOut2MIDINote(qSample);
                };

                if (pulseR)
                {
                    pulseR = false;
                    pulseTimer2 = 200;
                    PulseOut2(true);
                    LedOn(5);
                };

                // If a pulse1 is ongoing, keep counting until it ends
                if (pulseTimer1)
                {
                    pulseTimer1--;
                    if (pulseTimer1 == 0) // pulse ends
                    {
                        PulseOut1(false);
                        LedOff(4);
                    }
                };

                // If a pulse2 is ongoing, keep counting until it ends
                if (pulseTimer2)
                {
                    pulseTimer2--;
                    if (pulseTimer2 == 0) // pulse ends
                    {
                        PulseOut2(false);
                        LedOff(5);
                    }
                };
            }

            lastRisingEdge1 = risingEdge1;
            lastRisingEdge2 = risingEdge2;
        }
    };

private:
    int pulseTimer1 = 200;
    int pulseTimer2;
    bool clockPulse = false;
    uint32_t startPosL;
    uint32_t startPosR;
    int lowPassMain = 0;
    int lastLowPassMain = 0;
    int16_t bigKnob_CV;
    int loopLength = 0;
    bool reset = false;
    int32_t outL;
    int32_t outR;
    int32_t outCV;
    int startupCounter;
    int lastCV;

    Switch lastSwitchVal = Switch::Down;
    int x;
    int y;
    int main;
    int16_t cv1;
    int16_t cv2;
    int16_t cvMix;

    // Timing constant only (internal clock rate / knob-position scaling). No
    // longer backs a RAM audio buffer now that all audio lives in flash.
    static constexpr uint32_t bufSize = 64000;
    goldfish_head_t playHeadL;
    goldfish_head_t playHeadR;
    unsigned writeInd, readIndL, readIndR, cvsL, cvsR;
    int32_t ledtimer = 0;
    int32_t hpf = 0;
    bool checkZero = false;
    int phaseL = 0;
    int phaseR = 0;
    bool halftime;
    Divider clockDivider;
    int divisor;
    int internalClockCounter = 0;
    int internalClockRate;
    bool lastRisingEdge1 = false;
    bool lastRisingEdge2 = false;
    int audioL1 = 0;
    int audioL2 = 0;
    int audioR1 = 0;
    int audioR2 = 0;
    int audioLf1 = 0;
    int audioLf2 = 0;
    int audioRf1 = 0;
    int audioRf2 = 0;

    int16_t qSample;

    enum RunMode
    {
        RECORD,
        DELAY,
        PLAY
    } runMode;


    // Calculate the mix of the CV inputs based on which inputs are connected
    int16_t __not_in_flash_func(calcCVMix)(int16_t noise)
    {
        int16_t result = 0;
        int16_t thing1 = 0;
        int16_t thing2 = 0;

        bool noiseLed = false;

        if (Connected(Input::CV1) && Connected(Input::CV2))
        {
            thing1 = cv1 * (x - 2048) >> 11;
            thing2 = cv2 * (y - 2048) >> 11;
        }
        else if (Connected(Input::CV1))
        {
            thing1 = cv1 * (x - 2048) >> 11;
            thing2 = y - 2048;
        }
        else if (Connected(Input::CV2))
        {
            thing1 = noise * (x - 2048) >> 11;
            thing2 = cv2 * (y - 2048) >> 11;
            noiseLed = true;
        }
        else
        {
            thing1 = noise * (x - 2048) >> 11;
            thing2 = y - 2048;
            noiseLed = true;
        };

        if (noiseLed)
        {
            if (cabs(noise) > 1300)
            {
                LedBrightness(2, cabs(x - 2048));
            }
            else
            {
                LedOff(2);
            }
        }
        else
        {
            LedBrightness(2, cabs(thing1));
        }

        LedBrightness(3, cabs(thing2));

        // simple crossfade
        result = (thing1 * (bigKnob_CV + 2047) >> 12) + (thing2 * (4095 - (bigKnob_CV + 2047)) >> 12);

        return result;
    };

    void __not_in_flash_func(clip)(int32_t &a)
    {
        if (a < -2047)
            a = -2047;
        if (a > 2047)
            a = 2047;
    }

    int32_t __not_in_flash_func(cabs)(int32_t a)
    {
        return (a > 0) ? a : -a;
    }

    void __not_in_flash_func(calculateStartPos)()
    {
        if (Connected(Input::CV1) && Connected(Input::CV2))
        {
            startPosL = (x * (cv1 + 2048) >> 12) * loopLength >> 4;
            startPosR = (y * (cv2 + 2048) >> 12) * loopLength >> 4;
        }
        else if (Connected(Input::CV1))
        {
            startPosL = (x * (cv1 + 2048) >> 12) * loopLength >> 4;
            startPosR = y * loopLength >> 4;
        }
        else if (Connected(Input::CV2))
        {
            startPosL = x * loopLength >> 4;
            startPosR = (y * (cv2 + 2048) >> 12) * loopLength >> 4;
        }
        else
        {
            startPosL = x * loopLength >> 4;
            startPosR = y * loopLength >> 4;
        }
    }

    int16_t __not_in_flash_func(virtualDetentedKnob)(int16_t val)
    {
        if (val > 4079)
        {
            val = 4095;
        }
        else if (val < 16)
        {
            val = 0;
        }

        if (cabs(val - 2048) < 16)
        {
            val = 2048;
        }

        return val;
    }
};

int main()
{
    // Overclock to 200 MHz (default voltage) for core-1 headroom on flash refill
    // and future DSP. Must run before the ComputerCard object configures its PWM.
    // Proven safe with ComputerCard 0.3.0 by Sheep (22_sheep).
    set_sys_clock_khz(200000, true);

    // Create an instance of the Goldfish class.
    // static: keep this large object in .bss, not on main()'s stack, so its
    // 100+ KB of buffers don't collide with core 1's stack near the top of RAM.
    static Goldfish gf;

    // Enable the normalisation probe for the Goldfish instance
    gf.EnableNormalisationProbe();

    // Detect flash size / compute partition before the audio ISR or core 1
    // start (the JEDEC probe must run single-core).
    goldfish_stream_init();

    // Core 1 owns all flash erase/program so the core 0 audio path never blocks.
    multicore_launch_core1(goldfish_core1_entry);

    // Run the main processing loop of the Goldfish instance
    gf.Run();

    // Return 0 to indicate successful execution
    return 0;
}
