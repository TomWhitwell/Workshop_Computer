#include "ComputerCard.h"
#include "quantiser.h"
#include "divider.h"
#include "goldfish_stream.h"
#include "pico/multicore.h"
#include <math.h>

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

// 4-tap Catmull-Rom cubic interpolation between x0 and x1. t is the fractional
// position in [0, 1<<F]. Coefficients are carried at x2 scale (avoids the 0.5
// factors) and halved at the end. Result clamped to int16 to bound the cubic
// overshoot that this interpolant can produce on transients.
static inline int32_t __not_in_flash_func(cubicHermite)(int32_t xm1, int32_t x0,
                                                        int32_t x1, int32_t x2,
                                                        int32_t t, int32_t F)
{
    int32_t c1 = x1 - xm1;
    int32_t c2 = 2 * xm1 - 5 * x0 + 4 * x1 - x2;
    int32_t c3 = x2 - xm1 + 3 * (x0 - x1);
    int64_t tt = t;
    int64_t acc = c3;
    acc = (acc * tt) >> F;
    acc += c2;
    acc = (acc * tt) >> F;
    acc += c1;
    acc = (acc * tt) >> F;
    int32_t res = x0 + (int32_t)(acc >> 1);
    if (res > 32767) res = 32767;
    if (res < -32768) res = -32768;
    return res;
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

            // Hysteresis (backlash) on the main delay-time knob: when the knob is
            // static, freeze against ADC noise so the squared delay mapping
            // (cvL*cvL/50) doesn't jitter the read index - the mid-knob "blip".
            // It still tracks continuously once the knob is actually turned.
            const int MAIN_HYST = 16;
            if (main > mainHeld + MAIN_HYST)      mainHeld = main - MAIN_HYST;
            else if (main < mainHeld - MAIN_HYST) mainHeld = main + MAIN_HYST;
            main = mainHeld;

            // Big-knob parameter: playback speed in PLAY (in DELAY the delay time
            // is taken from `main` directly). AudioIn2 is now the right-channel
            // audio input for stereo, so it no longer attenuverts this.
            bigKnob_CV = 2048 - main + 1;

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

                // AudioIn1()/AudioIn2() are already 12kHz notch-filtered by
                // ComputerCard 0.3.0 (notchLeft/notchRight), so the extra notch
                // that used to live here is removed to avoid double-filtering.
                // AudioIn1 = left channel, AudioIn2 = right channel (stereo).
                int32_t audioLf = audioL;
                int32_t audioRf = audioR;

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
                    goldfish_stream_head_init(&playHeadL, 0);
                    goldfish_stream_head_init(&playHeadR, 1);
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
                    goldfish_stream_head_init(&playHeadL, 0);
                    goldfish_stream_head_init(&playHeadR, 1);
                    goldfish_stream_set_heads(&playHeadL, &playHeadR);
                    // Start playback from the beginning of the recording. StartPos
                    // (X/Y knobs) is applied only on an explicit reset, not when
                    // playback first begins after recording.
                    phaseL = 0;
                    phaseR = 0;
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
                    // Clean stereo delay: AudioIn1 -> L, AudioIn2 -> R, both delayed
                    // by ONE delay time. Main knob = delay time, SHORT fully CCW
                    // (main = 0) -> LONG fully CW (main = 4095), across the full
                    // exponential range (built at boot from capacity).
                    qSample = quantSample(cvMix);

                    // Main knob position -> exponential delay table position (<<8).
                    int32_t pos = main * 8; // main 0..4095 -> ~0..(128<<8)
                    if (pos < 0) pos = 0;
                    if (pos > (128 << 8)) pos = (128 << 8);
                    int32_t targ = delayLookup(pos); // delay in samples

                    // One-pole smoothing in <<7 delay-sample fixed point.
                    cvsL = (cvsL * 255 + ((int64_t)targ << 7)) >> 8;
                    int64_t cvs1 = cvsL >> 7;
                    int64_t rD   = cvsL & 0x7F;

                    // Record both channels (high-passed) + mono CV into the wrapping
                    // flash delay line.
                    int32_t bufL = highpass_process(&hpf, 200, audioLf);
                    int32_t bufR = highpass_process(&hpfR, 200, audioRf);
                    goldfish_stream_record_sample((int16_t)bufL, (int16_t)bufR, cvMix);

                    // Safety clamp (the exponential table already spans
                    // MIN_DELAY..maxDelay). MIN_DELAY floor set by record->flush
                    // backlog; erase-suspend advances flush through erases.
                    if (cvs1 < (int64_t)MIN_DELAY) cvs1 = MIN_DELAY;
                    if (cvs1 > (int64_t)maxDelayS) cvs1 = maxDelayS;

                    // Read both channels at the SAME delay via 4-tap Catmull-Rom
                    // cubic. The +1 look-ahead tap stays behind the write head
                    // (MIN_DELAY >> 1). L reads channel 0, R reads channel 1.
                    uint32_t wi = goldfish_stream_write_index();
                    int32_t fromBufferL = 0;
                    int32_t fromBufferR = 0;
                    if (wi > (uint32_t)cvs1 + 3u)
                    {
                        uint32_t readInd = wi - (uint32_t)cvs1 - 1u;
                        int32_t t = 128 - (int32_t)rD;
                        int32_t l_m1 = goldfish_stream_head_read(&playHeadL, readInd - 2u);
                        int32_t l_0  = goldfish_stream_head_read(&playHeadL, readInd - 1u);
                        int32_t l_1  = goldfish_stream_head_read(&playHeadL, readInd);
                        int32_t l_2  = goldfish_stream_head_read(&playHeadL, readInd + 1u);
                        fromBufferL = cubicHermite(l_m1, l_0, l_1, l_2, t, 7);
                        int32_t r_m1 = goldfish_stream_head_read(&playHeadR, readInd - 2u);
                        int32_t r_0  = goldfish_stream_head_read(&playHeadR, readInd - 1u);
                        int32_t r_1  = goldfish_stream_head_read(&playHeadR, readInd);
                        int32_t r_2  = goldfish_stream_head_read(&playHeadR, readInd + 1u);
                        fromBufferR = cubicHermite(r_m1, r_0, r_1, r_2, t, 7);
                    }

                    outL = fromBufferL;
                    outR = fromBufferR;
                    outCV = cvMix;

                    break;
                }
                case RECORD:
                {

                    // Record mode: capture both audio channels (L/R) + mono CV.
                    qSample = quantSample(cvMix);

                    goldfish_stream_record_sample((int16_t)audioLf, (int16_t)audioRf, cvMix);

                    outL = audioLf;
                    outR = audioRf;

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

                    // Stereo playback: both channels advance together (L reads
                    // channel 0, R reads channel 1 at the same position). AudioIn2
                    // is now an audio input, so the old L/R speed-spread is gone.
                    int32_t k = (2048 - bigKnob_CV) >> 1;
                    int32_t dphase = k - 1024;

                    phaseL += dphase >> 1;
                    phaseR += dphase >> 1;

                    if (loopLength < 1)
                    {
                        loopLength = bufSize;
                    }

                    // Phase-domain loop length (sample<<8). 64-bit because on 16MB
                    // cards loopLength can be ~22M samples, overflowing a 32-bit <<8.
                    int64_t loopPhase = (int64_t)loopLength << 8;

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
                        phaseL += loopPhase;
                        clockDivider.SetResetPhase(divisor);
                        pulseL = true;
                        pulseR = clockDivider.Step(true);
                    }
                    if (phaseL > loopPhase)
                    {
                        phaseL -= loopPhase;
                        pulseL = true;
                        clockDivider.SetResetPhase(divisor);
                        pulseR = clockDivider.Step(true);
                    }

                    if (phaseR < 0)
                    {
                        phaseR += loopPhase;
                    }
                    if (phaseR > loopPhase)
                    {
                        phaseR -= loopPhase;
                    }

                    int32_t rL = (int32_t)(phaseL & 0xFF);
                    int32_t readIndL = (int32_t)(phaseL >> 8);
                    int32_t rR = (int32_t)(phaseR & 0xFF);
                    int32_t readIndR = (int32_t)(phaseR >> 8);

                    // Decode audio from flash via the per-head streaming readers,
                    // with 4-tap Catmull-Rom cubic interpolation (audio only).
                    int32_t iLm1 = readIndL - 1; if (iLm1 < 0) iLm1 += loopLength;
                    int32_t iL1  = readIndL + 1; if (iL1 >= loopLength) iL1 -= loopLength;
                    int32_t iL2  = readIndL + 2; if (iL2 >= loopLength) iL2 -= loopLength;
                    int32_t sL = cubicHermite(
                        goldfish_stream_head_read(&playHeadL, iLm1),
                        goldfish_stream_head_read(&playHeadL, readIndL),
                        goldfish_stream_head_read(&playHeadL, iL1),
                        goldfish_stream_head_read(&playHeadL, iL2),
                        rL, 8);

                    int32_t iRm1 = readIndR - 1; if (iRm1 < 0) iRm1 += loopLength;
                    int32_t iR1  = readIndR + 1; if (iR1 >= loopLength) iR1 -= loopLength;
                    int32_t iR2  = readIndR + 2; if (iR2 >= loopLength) iR2 -= loopLength;
                    int32_t sR = cubicHermite(
                        goldfish_stream_head_read(&playHeadR, iRm1),
                        goldfish_stream_head_read(&playHeadR, readIndR),
                        goldfish_stream_head_read(&playHeadR, iR1),
                        goldfish_stream_head_read(&playHeadR, iR2),
                        rR, 8);

                    int32_t fadeLength = loopLength; // fade length near loop ends
                    int32_t baseL = sL << 11; // match the prior <<3 * 256 output scale
                    int32_t baseR = sR << 11;

                    // Apply fade-out at the end of the loop / fade-in at the start.
                    if (phaseL >= loopPhase - fadeLength)
                    {
                        int32_t fadeOutFactor = (int32_t)((loopPhase - phaseL) * 256 / fadeLength);
                        baseL = (int32_t)(((int64_t)baseL * fadeOutFactor) >> 8);
                    }
                    if (phaseL < fadeLength)
                    {
                        int32_t fadeInFactor = (int32_t)(phaseL * 256 / fadeLength);
                        baseL = (int32_t)(((int64_t)baseL * fadeInFactor) >> 8);
                    }
                    outL = baseL;

                    if (phaseR >= loopPhase - fadeLength)
                    {
                        int32_t fadeOutFactor = (int32_t)((loopPhase - phaseR) * 256 / fadeLength);
                        baseR = (int32_t)(((int64_t)baseR * fadeOutFactor) >> 8);
                    }
                    if (phaseR < fadeLength)
                    {
                        int32_t fadeInFactor = (int32_t)(phaseR * 256 / fadeLength);
                        baseR = (int32_t)(((int64_t)baseR * fadeInFactor) >> 8);
                    }
                    outR = baseR;

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
    int64_t startPosL;
    int64_t startPosR;
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
    int mainHeld = 2048; // hysteresis state for the main (delay-time) knob
    int16_t cv1;
    int16_t cv2;
    int16_t cvMix;

    // Timing constant only (internal clock rate / knob-position scaling). No
    // longer backs a RAM audio buffer now that all audio lives in flash.
    static constexpr uint32_t bufSize = 64000;
    goldfish_head_t playHeadL;
    goldfish_head_t playHeadR;
    unsigned writeInd, readIndL, readIndR;
    int64_t cvsL = 0;
    int64_t cvsR = 0;
    static constexpr uint32_t MIN_DELAY = 1536u; // ~64ms @24kHz; floor set by flush backlog
    uint32_t maxDelayS = MIN_DELAY;              // capacity-8192, filled in initDelayTable()
    int32_t delayTable[129];                     // exponential delay curve (samples), 0..128
    int32_t ledtimer = 0;
    int32_t hpf = 0;
    int32_t hpfR = 0;
    bool checkZero = false;
    int64_t phaseL = 0;
    int64_t phaseR = 0;
    bool halftime;
    Divider clockDivider;
    int divisor;
    int internalClockCounter = 0;
    int internalClockRate;
    bool lastRisingEdge1 = false;
    bool lastRisingEdge2 = false;

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
        // Resolve each knob (optionally CV-scaled) to a 0..4095 position, then map
        // it across loopLength into the phase domain (sample<<8). The loopLength
        // multiply is done in 64-bit: for long flash recordings loopLength can be
        // millions of samples, which would overflow a 32-bit (knob * loopLength)
        // (e.g. 4095 * ~525k > INT32_MAX), corrupting the start position.
        int32_t px, py;
        if (Connected(Input::CV1) && Connected(Input::CV2))
        {
            px = x * (cv1 + 2048) >> 12;
            py = y * (cv2 + 2048) >> 12;
        }
        else if (Connected(Input::CV1))
        {
            px = x * (cv1 + 2048) >> 12;
            py = y;
        }
        else if (Connected(Input::CV2))
        {
            px = x;
            py = y * (cv2 + 2048) >> 12;
        }
        else
        {
            px = x;
            py = y;
        }
        startPosL = ((int64_t)px * loopLength) >> 4;
        startPosR = ((int64_t)py * loopLength) >> 4;
    }

public:
    // Build the exponential (constant-ratio) delay-time curve spanning
    // MIN_DELAY..maxDelay in samples, from the runtime flash capacity. Called
    // once at boot (single-core). Exponential keeps the ~10-octave delay range
    // perceptually even, with the geometric mean at the curve centre (noon).
    void initDelayTable()
    {
        uint32_t cap = goldfish_stream_capacity_samples();
        maxDelayS = (cap > 8192u) ? (cap - 8192u) : MIN_DELAY;
        double ratio = (double)maxDelayS / (double)MIN_DELAY;
        for (int i = 0; i <= 128; i++)
        {
            delayTable[i] = (int32_t)((double)MIN_DELAY * pow(ratio, (double)i / 128.0) + 0.5);
        }
    }

    // Look up a delay (samples) at fractional table position pos (<<8, 0..128<<8),
    // linearly interpolating between the exponential control points.
    int32_t __not_in_flash_func(delayLookup)(int32_t pos)
    {
        if (pos < 0) pos = 0;
        if (pos > (128 << 8)) pos = (128 << 8);
        int32_t idx = pos >> 8;
        if (idx >= 128) return delayTable[128];
        int32_t frac = pos & 0xFF;
        int32_t a = delayTable[idx];
        int32_t b = delayTable[idx + 1];
        return a + (int32_t)(((int64_t)(b - a) * frac) >> 8);
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
    // Overclock to 192 MHz (default voltage) for core-1 headroom on flash refill
    // and DSP. 192 MHz = 48 MHz x 4, an integer multiple of the 48 MHz ADC/audio
    // reference, so audio-rate clock division stays exact (no fractional jitter).
    // Must run before the ComputerCard object configures its PWM. Same value
    // MLRws uses; proven safe with ComputerCard 0.3.0.
    set_sys_clock_khz(192000, true);

    // Create an instance of the Goldfish class.
    // static: keep this large object in .bss, not on main()'s stack, so its
    // 100+ KB of buffers don't collide with core 1's stack near the top of RAM.
    static Goldfish gf;

    // Enable the normalisation probe for the Goldfish instance
    gf.EnableNormalisationProbe();

    // Detect flash size / compute partition before the audio ISR or core 1
    // start (the JEDEC probe must run single-core).
    goldfish_stream_init();

    // Build the capacity-scaled delay-time curve (needs capacity from init).
    gf.initDelayTable();

    // Core 1 owns all flash erase/program so the core 0 audio path never blocks.
    multicore_launch_core1(goldfish_core1_entry);

    // Run the main processing loop of the Goldfish instance
    gf.Run();

    // Return 0 to indicate successful execution
    return 0;
}
