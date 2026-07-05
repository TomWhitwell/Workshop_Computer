#include "ComputerCard.h"
#include "quantiser.h"
#include "divider.h"
#include "goldfish_stream.h"
#include "goldfish_debug.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
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
        GF_DBG(uint32_t _t0 = timer_hw->timerawl;)
        halftime = !halftime;

        // simple startup counter to allow time for initialisation
        if (startupCounter)
            startupCounter--;

        if (startupCounter == 0)
        {
            bool risingEdge1 = PulseIn1RisingEdge();
            bool risingEdge2 = PulseIn2RisingEdge();
            bool fallingEdge2 = PulseIn2FallingEdge();

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
                    gateMode = false; gateRec = false;
                    recExitPending = false; recExitFade = false;
                    seekArmed = false; seekActive = false;
                    // Crossfade the previous mode's output (captured tail, faded
                    // out) into the live input monitor over XF_LEN samples.
                    recInXf = XF_LEN; recInBase = recTailW;
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
                    delayFadeIn = 0; delayReadyPrev = false;
                    recExitPending = false; recExitFade = false;
                    seekArmed = false; seekActive = false;
                    posSmooth = posSmooth2 = (main * 8) << 7;   // snap (no glide on entry)
                    if (Connected(Input::Pulse2))
                    {
                        // Gated record: monitor the input now; start recording on
                        // the Pulse 2 rising edge, hand off to PLAY on the falling
                        // edge. Don't start the delay line or heads yet.
                        gateMode = true;
                        gateRec  = false;
                        goldfish_stream_set_heads(NULL, NULL);
                    }
                    else
                    {
                        gateMode = false;
                        gateRec  = false;
                        goldfish_stream_delay_start();
                        goldfish_stream_head_init(&playHeadL, 0);
                        goldfish_stream_head_init(&playHeadR, 1);
                        goldfish_stream_set_heads(&playHeadL, &playHeadR);
                    }
                    internalClockCounter = 0;
                    clockDivider.SetResetPhase(divisor);
                    pulseL = true;
                    pulseR = true;
                }
                else if ((s == Switch::Middle) && (lastSwitchVal != Switch::Middle))
                {
                    runMode = PLAY;
                    gateMode = false; gateRec = false;
                    reset = false;
                    // Keep monitoring the live input until the recording flushes,
                    // then crossfade it into the loop start (see PLAY case).
                    recExitPending = true; recExitFade = false;
                    seekArmed = false; seekActive = false;
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
                    // Ask core 1 to (re)decode the loop-start/-end crossfade
                    // previews for this loop (off the audio path).
                    goldfish_stream_request_previews((uint32_t)loopLength);
                    xfBridgeL = 0;
                    xfBridgeR = 0;
                    xfBridgeRevL = 0;
                    xfBridgeRevR = 0;
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

                // Gated record (DELAY + plug in Pulse 2): the gate arms on DELAY
                // entry (monitoring). A rising edge starts recording; a falling
                // edge stops it and hands off to PLAY with the switch still Up.
                if (runMode == DELAY && gateMode)
                {
                    if (!gateRec)
                    {
                        if (risingEdge2 || lastRisingEdge2)
                        {
                            gateRec = true;
                            goldfish_stream_record_start();
                        }
                    }
                    else if (fallingEdge2 || lastFallingEdge2)
                    {
                        goldfish_stream_record_stop();
                        gateRec  = false;
                        gateMode = false;
                        reset    = false;
                        runMode  = PLAY;
                        // Keep monitoring the live input until the recording
                        // flushes, then crossfade it into the loop start.
                        recExitPending = true; recExitFade = false;
                        loopLength = goldfish_stream_recorded_samples();
                        if (loopLength < 1) loopLength = 1;
                        goldfish_stream_head_init(&playHeadL, 0);
                        goldfish_stream_head_init(&playHeadR, 1);
                        goldfish_stream_set_heads(&playHeadL, &playHeadR);
                        phaseL = 0;
                        phaseR = 0;
                        goldfish_stream_request_previews((uint32_t)loopLength);
                        xfBridgeL = xfBridgeR = xfBridgeRevL = xfBridgeRevR = 0;
                        seekActive = false; seekArmed = false;
                        recExitFade = false;
                        internalClockCounter = 0;
                        clockDivider.SetResetPhase(divisor);
                        pulseL = true;
                        pulseR = true;
                    }
                }


                // Main audio processing depending on mode (record, delay, play)
                switch (runMode)
                {
                case DELAY:
                {
                    if (gateMode)
                    {
                        // Gated record: pass the input through (dry monitor) at all
                        // times; capture it while the Pulse 2 gate is high. Seamless
                        // because the monitor is identical armed vs. recording.
                        if (gateRec)
                            goldfish_stream_record_sample((int16_t)audioLf, (int16_t)audioRf, cvMix);
                        outL = audioLf;
                        outR = audioRf;
                        outCV = cvMix;
                        break;
                    }

                    // Clean stereo delay: AudioIn1 -> L, AudioIn2 -> R, both delayed
                    // by ONE delay time. Main knob = delay time, SHORT fully CCW
                    // (main = 0) -> LONG fully CW (main = 4095), across the full
                    // exponential range (built at boot from capacity).

                    // Main knob -> exponential delay position (<<8). Smooth in the
                    // POSITION (exponential) domain so equal knob steps are equal
                    // pitch ratios: a sharp turn glides musically instead of
                    // lurching by a huge number of samples (which tore, worst at
                    // the long end where a small knob move = a big time change).
                    int32_t posTarget = main * 8; // main 0..4095 -> ~0..(128<<8)
                    if (posTarget < 0) posTarget = 0;
                    if (posTarget > (128 << 8)) posTarget = (128 << 8);
                    // Two cascaded one-poles = a 2nd-order (S-curve) response, so
                    // the delay time (and therefore the doppler pitch) eases in and
                    // out smoothly instead of jumping to a fixed rate. Slow poles
                    // (2047/2048) keep the peak rate low so hard slams don't tear.
                    posSmooth  = (int32_t)(((int64_t)posSmooth  * 2047 + ((int64_t)posTarget << 7)) >> 11);
                    posSmooth2 = (int32_t)(((int64_t)posSmooth2 * 2047 + (int64_t)posSmooth) >> 11);
                    int32_t targ = delayLookup(posSmooth2 >> 7); // delay in samples

                    // Light, fast sample-domain smoothing only to interpolate the
                    // integer delay steps into a sub-sample fraction (the S-curve
                    // above already shapes the rate, so no hard cap is needed).
                    cvsL = (cvsL * 15 + ((int64_t)targ << 7)) >> 4;
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
                    // Until the delay line has filled (write head past the delay
                    // time), pass the live input straight through so entering DELAY
                    // is heard immediately instead of a gap of silence; the delayed
                    // signal then crossfades in so the handoff isn't a click.
                    bool ready = (wi > (uint32_t)cvs1 + 3u);
                    if (ready && !delayReadyPrev) delayFadeIn = DELAY_FADE_LEN;
                    delayReadyPrev = ready;

                    int32_t fromBufferL = audioLf;
                    int32_t fromBufferR = audioRf;
                    if (ready)
                    {
                        uint32_t readInd = wi - (uint32_t)cvs1 - 1u;
                        int32_t t = 128 - (int32_t)rD;
                        int32_t l_m1 = goldfish_stream_head_read(&playHeadL, readInd - 2u);
                        int32_t l_0  = goldfish_stream_head_read(&playHeadL, readInd - 1u);
                        int32_t l_1  = goldfish_stream_head_read(&playHeadL, readInd);
                        int32_t l_2  = goldfish_stream_head_read(&playHeadL, readInd + 1u);
                        int32_t wetL = cubicHermite(l_m1, l_0, l_1, l_2, t, 7);
                        int32_t r_m1 = goldfish_stream_head_read(&playHeadR, readInd - 2u);
                        int32_t r_0  = goldfish_stream_head_read(&playHeadR, readInd - 1u);
                        int32_t r_1  = goldfish_stream_head_read(&playHeadR, readInd);
                        int32_t r_2  = goldfish_stream_head_read(&playHeadR, readInd + 1u);
                        int32_t wetR = cubicHermite(r_m1, r_0, r_1, r_2, t, 7);
                        if (delayFadeIn > 0)
                        {
                            int32_t g = DELAY_FADE_LEN - delayFadeIn; // 0..LEN (dry->wet)
                            fromBufferL = (audioLf * (DELAY_FADE_LEN - g) + wetL * g) / DELAY_FADE_LEN;
                            fromBufferR = (audioRf * (DELAY_FADE_LEN - g) + wetR * g) / DELAY_FADE_LEN;
                            delayFadeIn--;
                        }
                        else
                        {
                            fromBufferL = wetL;
                            fromBufferR = wetR;
                        }
                    }

                    outL = fromBufferL;
                    outR = fromBufferR;
                    outCV = cvMix;

                    break;
                }
                case RECORD:
                {

                    // Record mode: capture both audio channels (L/R) + mono CV.

                    GF_DBG(uint32_t _rt0 = timer_hw->timerawl;)
                    goldfish_stream_record_sample((int16_t)audioLf, (int16_t)audioRf, cvMix);
                    GF_DBG(uint32_t _rdt = timer_hw->timerawl - _rt0;
                    if (_rdt > maxRecUs) maxRecUs = _rdt;)

                    outL = audioLf;
                    outR = audioRf;
                    // MLRws-style entry crossfade: fade the captured tail of the
                    // previous output (replayed newest->oldest so it stays
                    // continuous with the last sample) out under the live monitor.
                    if (recInXf > 0)
                    {
                        int p  = XF_LEN - recInXf;                          // 0..XF_LEN-1
                        int ti = (recInBase + (XF_LEN - 1) - p) & (XF_LEN - 1);
                        int32_t ng = p + 1, og = XF_LEN - ng;
                        outL = (recTailL[ti] * og + outL * ng) / XF_LEN;
                        outR = (recTailR[ti] * og + outR * ng) / XF_LEN;
                        recInXf--;
                    }

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
                    // Just left RECORD: keep monitoring the live input (the
                    // outgoing stream) until the recording has flushed, then arm a
                    // seek-style crossfade from the monitor into the loop start.
                    if (recExitPending)
                    {
                        if (!goldfish_stream_io_idle())
                        {
                            outL = audioLf; outR = audioRf; outCV = cvMix;
                            break;
                        }
                        recExitPending = false;
                        if (loopLength > XF_BUF + 4)
                        {
                            seekTargetL = 0; seekTargetR = 0;
                            seekBufBaseL = 0; seekBufBaseR = 0;
                            goldfish_stream_request_seek(0u, 0u);
                            seekArmed = true; seekActive = false;
                            recExitFade = true;
                            xfBridgeL = xfBridgeR = xfBridgeRevL = xfBridgeRevR = 0;
                        }
                        else
                        {
                            // Loop too short to buffer a crossfade: quick declick.
                            phaseL = 0; phaseR = 0;
                            declickSrcL = (int16_t)outL; declickCountL = DECLICK_SAMPLES;
                            declickSrcR = (int16_t)outR; declickCountR = DECLICK_SAMPLES;
                        }
                    }
                    // Playback reads the recording from flash. Wait until any
                    // just-finished recording has flushed (no core-1 erase in
                    // flight) before touching the flash bus.
                    else if (!goldfish_stream_io_idle())
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

                    // Loop-boundary overlap crossfade is used only when the loop
                    // is comfortably longer than the window AND core 1 has decoded
                    // the previews; otherwise fall back to declick.
                    const int16_t *xfStartL = goldfish_stream_preview_start(0);
                    const int16_t *xfStartR = goldfish_stream_preview_start(1);
                    const int16_t *xfEndL   = goldfish_stream_preview_end(0);
                    const int16_t *xfEndR   = goldfish_stream_preview_end(1);
                    bool xfOK = (loopLength > 2 * XF_LEN) && goldfish_stream_previews_ready();
                    int64_t xfWrap  = (int64_t)(loopLength - XF_LEN) << 8;
                    int32_t endBase = loopLength - XF_BUF;   // xfEnd[j] = audio[endBase+j]
                    bool    reverse = (dphase < 0);

                    calculateStartPos();

                    //This is really only a best effort to reduce discontinuities in the audio output that cause clicks on reset
                    //It's not perfect and could be improved
                    checkZero = zeroCrossing(outL, lastSampleL);

                    lastSampleL = fromBufferL;
                    lastSampleR = fromBufferR;

                    // Reset = "cut": jump to startPos with a pre-decoded crossfade
                    // (like MLRws). Ask core 1 to decode the target region and keep
                    // playing the old position until it is ready; the crossfade
                    // starts below. Works in both directions: forward decodes
                    // [target, target+XF_BUF), reverse decodes the region ending at
                    // target so the seek plays backward out of the buffer.
                    if (reset)
                    {
                        reset = false;
                        bool ok = xfOK && (loopLength > XF_BUF + 4);
                        if (ok && reverse)
                        {
                            int32_t lo = XF_BUF - 1, hi = loopLength - 1;
                            int32_t tL = (int32_t)(startPosL >> 8);
                            int32_t tR = (int32_t)(startPosR >> 8);
                            if (tL < lo) tL = lo; else if (tL > hi) tL = hi;
                            if (tR < lo) tR = lo; else if (tR > hi) tR = hi;
                            seekTargetL = tL; seekTargetR = tR;
                            seekBufBaseL = tL - (XF_BUF - 1);
                            seekBufBaseR = tR - (XF_BUF - 1);
                            goldfish_stream_request_seek((uint32_t)seekBufBaseL, (uint32_t)seekBufBaseR);
                            seekArmed = true; seekActive = false;
                        }
                        else if (ok)
                        {
                            int32_t maxT = loopLength - XF_BUF;
                            int32_t tL = (int32_t)(startPosL >> 8);
                            int32_t tR = (int32_t)(startPosR >> 8);
                            if (tL < 0) tL = 0; else if (tL > maxT) tL = maxT;
                            if (tR < 0) tR = 0; else if (tR > maxT) tR = maxT;
                            seekTargetL = tL; seekTargetR = tR;
                            seekBufBaseL = tL; seekBufBaseR = tR;
                            goldfish_stream_request_seek((uint32_t)tL, (uint32_t)tR);
                            seekArmed = true; seekActive = false;
                        }
                        else
                        {
                            // Very short loop: fall back to jump + declick.
                            phaseL = startPosL; phaseR = startPosR;
                            declickSrcL = (int16_t)outL; declickCountL = DECLICK_SAMPLES;
                            declickSrcR = (int16_t)outR; declickCountR = DECLICK_SAMPLES;
                        }
                        xfBridgeL = 0; xfBridgeR = 0; xfBridgeRevL = 0; xfBridgeRevR = 0;
                        pulseL = true;
                        pulseR = true;
                        clockDivider.SetResetPhase(divisor);
                        internalClockCounter = 0;
                    };

                    // Start the seek crossfade once core 1 has decoded the target.
                    if (seekArmed && goldfish_stream_seek_ready())
                    {
                        seekArmed  = false;
                        seekActive = true;
                        seekProg   = 0;
                    }

                    // Record-exit: keep monitoring the live input until core 1 has
                    // decoded the loop start and the crossfade actually begins.
                    if (recExitFade && !seekActive)
                    {
                        outL = audioLf; outR = audioRf; outCV = cvMix;
                        break;
                    }

                    // ---- loop wraps (suspended while a seek crossfade runs) ----
                    if (!seekActive)
                    {
                    // ---- L wrap ----
                    if (phaseL < 0)   // reverse wrap
                    {
                        if (xfOK) { phaseL += xfWrap; xfBridgeRevL = XF_BRIDGE; xfBridgeL = 0; }
                        else      { phaseL += loopPhase; declickSrcL = (int16_t)outL; declickCountL = DECLICK_SAMPLES; }
                        clockDivider.SetResetPhase(divisor);
                        pulseL = true;
                        pulseR = clockDivider.Step(true);
                    }
                    if (phaseL > loopPhase)   // forward wrap
                    {
                        if (xfOK) { phaseL -= xfWrap; xfBridgeL = XF_BRIDGE; xfBridgeRevL = 0; }
                        else      { phaseL -= loopPhase; declickSrcL = (int16_t)outL; declickCountL = DECLICK_SAMPLES; }
                        pulseL = true;
                        clockDivider.SetResetPhase(divisor);
                        pulseR = clockDivider.Step(true);
                    }

                    // ---- R wrap ----
                    if (phaseR < 0)
                    {
                        if (xfOK) { phaseR += xfWrap; xfBridgeRevR = XF_BRIDGE; xfBridgeR = 0; }
                        else      { phaseR += loopPhase; declickSrcR = (int16_t)outR; declickCountR = DECLICK_SAMPLES; }
                    }
                    if (phaseR > loopPhase)
                    {
                        if (xfOK) { phaseR -= xfWrap; xfBridgeR = XF_BRIDGE; xfBridgeRevR = 0; }
                        else      { phaseR -= loopPhase; declickSrcR = (int16_t)outR; declickCountR = DECLICK_SAMPLES; }
                    }
                    }

                    int32_t rL = (int32_t)(phaseL & 0xFF);
                    int32_t readIndL = (int32_t)(phaseL >> 8);
                    int32_t rR = (int32_t)(phaseR & 0xFF);
                    int32_t readIndR = (int32_t)(phaseR >> 8);

                    if (seekActive)
                    {
                        // ---- Reset/cut seek crossfade + handoff (both directions) ----
                        seekProg += dphase >> 1;              // target-relative phase
                        int32_t nIdx  = (int32_t)(seekProg >> 8);   // signed (floor)
                        int32_t nFrac = (int32_t)(seekProg & 0xFF); // 0..255
                        int32_t prog  = (nIdx < 0) ? -nIdx : nIdx;  // distance from target
                        bool done = (prog >= XF_BUF - 1);
                        if (done) { nIdx = (nIdx < 0) ? -(XF_BUF - 1) : (XF_BUF - 1); nFrac = 0; }

                        int32_t absL = seekTargetL + nIdx;   // absolute sample being played
                        int32_t absR = seekTargetR + nIdx;
                        const int16_t *sbL = goldfish_stream_seek_buf(0);
                        const int16_t *sbR = goldfish_stream_seek_buf(1);
                        int32_t newL = xfInterp(sbL, absL - seekBufBaseL, nFrac);
                        int32_t newR = xfInterp(sbR, absR - seekBufBaseR, nFrac);

                        if (prog < XF_LEN && !done)
                        {
                            // Crossfade the still-playing old stream into the
                            // pre-decoded target (new gain rising over XF_LEN). On
                            // a record exit the "old" stream is the live monitor.
                            int32_t oldL, oldR;
                            if (recExitFade)
                            {
                                oldL = audioLf;
                                oldR = audioRf;
                            }
                            else
                            {
                                int32_t iLm1 = readIndL - 1; if (iLm1 < 0) iLm1 += loopLength;
                                int32_t iL1  = readIndL + 1; if (iL1 >= loopLength) iL1 -= loopLength;
                                int32_t iL2  = readIndL + 2; if (iL2 >= loopLength) iL2 -= loopLength;
                                oldL = cubicHermite(
                                    goldfish_stream_head_read(&playHeadL, iLm1),
                                    goldfish_stream_head_read(&playHeadL, readIndL),
                                    goldfish_stream_head_read(&playHeadL, iL1),
                                    goldfish_stream_head_read(&playHeadL, iL2), rL, 8);
                                int32_t iRm1 = readIndR - 1; if (iRm1 < 0) iRm1 += loopLength;
                                int32_t iR1  = readIndR + 1; if (iR1 >= loopLength) iR1 -= loopLength;
                                int32_t iR2  = readIndR + 2; if (iR2 >= loopLength) iR2 -= loopLength;
                                oldR = cubicHermite(
                                    goldfish_stream_head_read(&playHeadR, iRm1),
                                    goldfish_stream_head_read(&playHeadR, readIndR),
                                    goldfish_stream_head_read(&playHeadR, iR1),
                                    goldfish_stream_head_read(&playHeadR, iR2), rR, 8);
                            }
                            int32_t ng = prog + 1, og = XF_LEN - ng;
                            outL = (oldL * og + newL * ng) / XF_LEN;
                            outR = (oldR * og + newR * ng) / XF_LEN;
                        }
                        else
                        {
                            // Bridge: serve the target buffer while the heads reseek
                            // to the handoff point (drive them so core 1 refills).
                            outL = newL;
                            outR = newR;
                            (void)goldfish_stream_head_read(&playHeadL, absL);
                            (void)goldfish_stream_head_read(&playHeadR, absR);
                        }

                        if (done)
                        {
                            phaseL = (int64_t)absL << 8;
                            phaseR = (int64_t)absR << 8;
                            seekActive = false;
                            recExitFade = false;
                        }

                        int32_t cvIdx = absL;
                        if (cvIdx < 0) cvIdx = 0; else if (cvIdx >= loopLength) cvIdx = loopLength - 1;
                        outCV = goldfish_stream_read_cv(cvIdx);

                        break;
                    }

                    // ---- L output ----
                    int32_t outLs;
                    bool usedBridgeL = false;
                    if (xfBridgeL > 0)   // forward post-wrap bridge from loop start
                    {
                        xfBridgeL--;
                        if (readIndL >= XF_LEN && readIndL < XF_BUF - 1)
                        {
                            outLs = xfInterp(xfStartL, readIndL, rL);
                            (void)goldfish_stream_head_read(&playHeadL, readIndL);
                            usedBridgeL = true;
                        }
                    }
                    else if (xfBridgeRevL > 0)   // reverse post-wrap bridge from loop end
                    {
                        xfBridgeRevL--;
                        int idx = readIndL - endBase;
                        if (idx >= 0 && idx < XF_BUF - 1)
                        {
                            outLs = xfInterp(xfEndL, idx, rL);
                            (void)goldfish_stream_head_read(&playHeadL, readIndL);
                            usedBridgeL = true;
                        }
                    }
                    if (!usedBridgeL)
                    {
                        int32_t iLm1 = readIndL - 1; if (iLm1 < 0) iLm1 += loopLength;
                        int32_t iL1  = readIndL + 1; if (iL1 >= loopLength) iL1 -= loopLength;
                        int32_t iL2  = readIndL + 2; if (iL2 >= loopLength) iL2 -= loopLength;
                        int32_t sL = cubicHermite(
                            goldfish_stream_head_read(&playHeadL, iLm1),
                            goldfish_stream_head_read(&playHeadL, readIndL),
                            goldfish_stream_head_read(&playHeadL, iL1),
                            goldfish_stream_head_read(&playHeadL, iL2),
                            rL, 8);
                        if (xfOK && declickCountL == 0 && !reverse && readIndL >= (int32_t)(loopLength - XF_LEN))
                        {
                            // Forward tail: blend loop-end tail (out) with loop start (in).
                            int p = readIndL - (int32_t)(loopLength - XF_LEN); // 0..XF_LEN-1
                            if (p < 0) p = 0; else if (p > XF_LEN - 1) p = XF_LEN - 1;
                            int32_t head = xfInterp(xfStartL, p, rL);
                            int32_t ng = p + 1, og = XF_LEN - ng;
                            outLs = (sL * og + head * ng) / XF_LEN;
                        }
                        else if (xfOK && declickCountL == 0 && reverse && readIndL < XF_LEN)
                        {
                            // Reverse tail: blend loop-start tail (out) with loop end (in).
                            int32_t head = xfInterp(xfEndL, readIndL + (XF_BUF - XF_LEN), rL);
                            int32_t ng = XF_LEN - readIndL, og = readIndL; // readIndL in 0..XF_LEN-1
                            outLs = (sL * og + head * ng) / XF_LEN;
                        }
                        else
                        {
                            outLs = sL;
                        }
                    }
                    if (declickCountL > 0)
                    {
                        int32_t alpha = (int32_t)(DECLICK_SAMPLES - declickCountL) + 1;
                        outLs = (declickSrcL * (DECLICK_SAMPLES - alpha) + outLs * alpha) >> DECLICK_SHIFT;
                        declickCountL--;
                    }
                    outL = outLs;

                    // ---- R output ----
                    int32_t outRs;
                    bool usedBridgeR = false;
                    if (xfBridgeR > 0)
                    {
                        xfBridgeR--;
                        if (readIndR >= XF_LEN && readIndR < XF_BUF - 1)
                        {
                            outRs = xfInterp(xfStartR, readIndR, rR);
                            (void)goldfish_stream_head_read(&playHeadR, readIndR);
                            usedBridgeR = true;
                        }
                    }
                    else if (xfBridgeRevR > 0)
                    {
                        xfBridgeRevR--;
                        int idx = readIndR - endBase;
                        if (idx >= 0 && idx < XF_BUF - 1)
                        {
                            outRs = xfInterp(xfEndR, idx, rR);
                            (void)goldfish_stream_head_read(&playHeadR, readIndR);
                            usedBridgeR = true;
                        }
                    }
                    if (!usedBridgeR)
                    {
                        int32_t iRm1 = readIndR - 1; if (iRm1 < 0) iRm1 += loopLength;
                        int32_t iR1  = readIndR + 1; if (iR1 >= loopLength) iR1 -= loopLength;
                        int32_t iR2  = readIndR + 2; if (iR2 >= loopLength) iR2 -= loopLength;
                        int32_t sR = cubicHermite(
                            goldfish_stream_head_read(&playHeadR, iRm1),
                            goldfish_stream_head_read(&playHeadR, readIndR),
                            goldfish_stream_head_read(&playHeadR, iR1),
                            goldfish_stream_head_read(&playHeadR, iR2),
                            rR, 8);
                        if (xfOK && declickCountR == 0 && !reverse && readIndR >= (int32_t)(loopLength - XF_LEN))
                        {
                            int p = readIndR - (int32_t)(loopLength - XF_LEN);
                            if (p < 0) p = 0; else if (p > XF_LEN - 1) p = XF_LEN - 1;
                            int32_t head = xfInterp(xfStartR, p, rR);
                            int32_t ng = p + 1, og = XF_LEN - ng;
                            outRs = (sR * og + head * ng) / XF_LEN;
                        }
                        else if (xfOK && declickCountR == 0 && reverse && readIndR < XF_LEN)
                        {
                            int32_t head = xfInterp(xfEndR, readIndR + (XF_BUF - XF_LEN), rR);
                            int32_t ng = XF_LEN - readIndR, og = readIndR;
                            outRs = (sR * og + head * ng) / XF_LEN;
                        }
                        else
                        {
                            outRs = sR;
                        }
                    }
                    if (declickCountR > 0)
                    {
                        int32_t alpha = (int32_t)(DECLICK_SAMPLES - declickCountR) + 1;
                        outRs = (declickSrcR * (DECLICK_SAMPLES - alpha) + outRs * alpha) >> DECLICK_SHIFT;
                        declickCountR--;
                    }
                    outR = outRs;

                    if (loopLength > 0)
                    {
                        outCV = (goldfish_stream_read_cv(readIndL) * (256 - rL) + goldfish_stream_read_cv((readIndL + 1) % loopLength) * rL) >> 8;
                    }

                    break;
                }
                };

                clip(outL);
                clip(outR);
                // Rolling capture of the output, used as the fading-out tail when
                // entering RECORD (frozen while that crossfade replays the tail).
                if (recInXf == 0)
                {
                    recTailL[recTailW] = (int16_t)outL;
                    recTailR[recTailW] = (int16_t)outR;
                    recTailW = (recTailW + 1) & (XF_LEN - 1);
                }
                AudioOut1(outL);
                AudioOut2(outR);
                CVOut1(outCV);

                LedBrightness(0, cabs(outL));
                LedBrightness(1, cabs(outR));

                if ((risingEdge2 || lastRisingEdge2) && !gateMode)
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
                    // Quantise the (sample-and-held) CV only on the clock edge that
                    // actually latches it to CV out 2, instead of every sample. In
                    // every mode the note tracks outCV (DELAY/RECORD set outCV =
                    // cvMix; PLAY sets outCV from the recorded CV track).
                    qSample = quantSample(outCV);
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
            lastFallingEdge2 = fallingEdge2;
        }

        // Startup LED animation ("blub, blub"): a swell sweeps the LEDs in order
        // 4,2,0 (left column, rising) then 5,3,1 (right column) over ~2 seconds.
        // Runs every call and overrides the normal LED output until it finishes.
        if (bootAnim > 0)
        {
            bootAnim--;
            static const uint8_t seq[6] = {4, 2, 0, 5, 3, 1};
            int32_t elapsed = BOOT_ANIM_LEN - bootAnim;   // 0 .. BOOT_ANIM_LEN
            int32_t slot = BOOT_ANIM_LEN / 6;             // ~0.33 s per LED
            for (int i = 0; i < 6; i++)
            {
                int32_t center = i * slot + slot / 2;
                int32_t d = elapsed - center; if (d < 0) d = -d;
                int32_t b = (d < slot) ? (slot - d) * 4095 / slot : 0; // triangle swell
                LedBrightness(seq[i], (uint16_t)b);
            }
        }

        GF_DBG(uint32_t _dt = timer_hw->timerawl - _t0;
        if (_dt > maxProcUs) maxProcUs = _dt;
        if (_dt >= 21u) procOverruns++;) // 48kHz period ~= 20.83us
    };

public:
    GF_DBG(volatile uint32_t maxProcUs = 0;)
    GF_DBG(volatile uint32_t procOverruns = 0;)
    GF_DBG(volatile uint32_t maxRecUs = 0;)

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

    // Startup LED animation counter (2 s @ 48 kHz, ticked every ProcessSample).
    static constexpr int32_t BOOT_ANIM_LEN = 96000;
    int32_t bootAnim = BOOT_ANIM_LEN;
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
    // DELAY dry->wet handoff crossfade: when the delay line fills, blend the dry
    // passthrough into the delayed signal over DELAY_FADE_LEN samples instead of
    // switching hard (which was an audible discontinuity a moment after entry).
    static constexpr int DELAY_FADE_LEN = 256;
    int  delayFadeIn = 0;
    bool delayReadyPrev = false;
    int32_t posSmooth  = 0;   // smoothed delay knob position (<<7), exp domain
    int32_t posSmooth2 = 0;   // 2nd cascaded pole -> S-curve (eased pitch glide)
    bool checkZero = false;
    int64_t phaseL = 0;
    int64_t phaseR = 0;

    // Held-value declick: fade from the last output into new-position audio on
    // a RESET jump (where we have no pre-decoded stream). Loop wraps in both
    // directions use the two-stream overlap crossfade below.
    static constexpr int DECLICK_SHIFT   = 5;
    static constexpr int DECLICK_SAMPLES = 1 << DECLICK_SHIFT; // 32 (~1.3ms @24kHz)
    int16_t declickSrcL  = 0;   // value to fade from (previous output)
    int16_t declickSrcR  = 0;
    uint8_t declickCountL = 0;
    uint8_t declickCountR = 0;

    // Loop-boundary overlap crossfade (MLRws-style), both directions. The loop
    // start and loop end are pre-decoded on CORE 1 (goldfish_stream previews) so
    // there is no decode spike on the audio path. Over the last XF_LEN samples
    // approaching a boundary the tail (live head, fading out) is crossfaded with
    // the pre-decoded opposite end (fading in); the phase then wraps early by
    // XF_LEN and XF_BRIDGE samples are served from the buffer while the head
    // reseeks. Forward uses the start preview, reverse the end preview.
    static constexpr int XF_LEN    = 256;   // crossfade window
    static constexpr int XF_BRIDGE = 128;   // post-wrap samples served from buffer
    static constexpr int XF_BUF    = (int)GOLDFISH_PREVIEW_LEN; // XF_LEN+XF_BRIDGE+4
    int     xfBridgeL  = 0;   // >0: forward post-wrap bridge from loop start (L)
    int     xfBridgeR  = 0;
    int     xfBridgeRevL = 0; // >0: reverse post-wrap bridge from loop end (L)
    int     xfBridgeRevR = 0;

    // Reset/cut seek crossfade (MLRws-style): on reset, core 1 decodes the target
    // (startPos) into the seek buffer; the old position keeps playing until it is
    // ready, then old is crossfaded into the pre-decoded target and handed off.
    bool    seekArmed   = false;  // reset latched, waiting for core-1 decode
    bool    seekActive  = false;  // crossfade/handoff in progress
    int64_t seekProg    = 0;      // phase advanced from the target (shared L/R)
    int32_t seekTargetL = 0;
    int32_t seekTargetR = 0;
    int32_t seekBufBaseL = 0;     // absolute sample of seek buffer index 0 (per ch)
    int32_t seekBufBaseR = 0;

    // MLRws-style record-transition crossfades (real two-stream overlaps, not a
    // held value). Entering RECORD: the loop/delay being left is about to be
    // erased, so a rolling capture of recent OUTPUT is replayed newest->oldest
    // (continuous with the last sample) and faded out under the rising live-input
    // monitor. Exiting RECORD->PLAY: the live monitor keeps running (fading out)
    // and is crossfaded into the pre-decoded loop start through the seek engine.
    int16_t  recTailL[XF_LEN];
    int16_t  recTailR[XF_LEN];
    uint16_t recTailW  = 0;      // next capture slot (== oldest sample once full)
    int      recInXf   = 0;      // >0: entering-RECORD crossfade in progress
    uint16_t recInBase = 0;      // recTailW snapshot at RECORD entry
    bool     recExitPending = false; // left RECORD: monitor until the flush ends
    bool     recExitFade    = false; // seek crossfade uses the live monitor as "old"

    // Gated record (DELAY mode with a plug in Pulse 2): monitor the input, record
    // while the Pulse 2 gate is high, hand off to PLAY on the falling edge.
    bool    gateMode = false;     // in gated-record DELAY (armed or recording)
    bool    gateRec  = false;     // gate high -> currently recording

    // Linear interpolate buf[idx]..buf[idx+1] at frac/256, clamped to the buffer.
    static inline int32_t xfInterp(const int16_t *buf, int idx, int frac)
    {
        if (idx < 0) idx = 0; else if (idx >= XF_BUF - 1) idx = XF_BUF - 2;
        return (buf[idx] * (256 - frac) + buf[idx + 1] * frac) >> 8;
    }

    bool halftime;
    Divider clockDivider;
    int divisor;
    int internalClockCounter = 0;
    int internalClockRate;
    bool lastRisingEdge1 = false;
    bool lastRisingEdge2 = false;
    bool lastFallingEdge2 = false;

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
