// Frequency shifter for the Music Thing Modular Workshop System Computer. 
// Includes internal feedback loop and dual crossfaded inputs.
//
// Inputs
//   AudioIn1, AudioIn2 : audio inputs (mixed according to Y knob)
//   CVIn1              : shift frequency CV (added to Main knob)
//   CVIn2              : feedback blend CV (lower ↔ upper sideband)
//
// Outputs
//   AudioOut1          : lower sideband (shifts down)
//   AudioOut2          : upper sideband (shifts up)
//   CVOut1             : static -5V (for normalisation)
//   CVOut2             : static +5V (for normalisation)
//
// Controls
//   Main knob + CVIn1  : shift frequency (centre = 0 Hz)
//   Switch Up          : wide range, log-scaled ±2–7000 Hz
//   Switch Middle/Down : narrow range, linear ±440 Hz
//   X knob             : feedback amount
//   Y knob             : crossfade AudioIn1 → AudioIn2
//   CVIn2              : feedback blend (lower ↔ upper sideband)
//
// LEDs
//   0 / 1 : input / output level
//   2 / 3 : shift magnitude (2 = positive shift, 3 = negative shift)
//   4 / 5 : feedback blend (4 = upper sideband, 5 = lower sideband)

#include "pico/stdlib.h"
#include "ComputerCard.h"
#include <cmath>
#include <cstdint>
#include "hardware/clocks.h"
#include "hardware/vreg.h"

class FreqShifter : public ComputerCard {
public:
    static constexpr int FS = 48000;

    // Hilbert FIR tap count.  Must be odd with an odd centre index
    // (i.e. HTAPS = 4k+3).  Increasing improves sideband rejection.
    static constexpr int HTAPS = 63;

    // Delay ring-buffer for I-path alignment; must be >= (HTAPS-1)/2.
    static constexpr int DELAYRB = 64;

    static constexpr int      SIN_LUT_N    = 4096;
    static constexpr int      SIN_LUT_BITS = 12;
    static constexpr int      FREQ_LUT_N   = 2048;
    static constexpr uint32_t PHASE_BITS   = 32;

    // Shift = 19 maps ±2047 audio to ±50 % of full scale, giving ~6 dB headroom
    // so SSB peak combinations don't saturate the internal signal chain.
    static constexpr int      AUDIO_SHIFT = 19;
    static constexpr int16_t  AUDIO_MAX       = 2047;
    static constexpr int16_t  AUDIO_MIN       = -2048;

    static constexpr double   kPi                = 3.141592653589793238462643383279502884;
    static constexpr int32_t WIDE_MIN_SHIFT_HZ   = 2;
    static constexpr int32_t WIDE_MAX_SHIFT_HZ   = 7000;
    static constexpr int32_t NARROW_MAX_SHIFT_HZ = 440;

    void Init() {
        buildHilbert();
        buildSinLUT();
        buildFreqLUT();
        EnableNormalisationProbe();

        phase          = 0;
        phaseIncSigned = 0;

        CVOut1(-2048);  // static -5V
        CVOut2(2047);   // static +5V

        for (int i = 0; i < 6; ++i) LedOff(i);
    }

    void ProcessSample() override {
        updateControl();

        // Mix AudioIn1 and AudioIn2 according to Y knob.
        int32_t a1    = fromAudio(acCouple(AudioIn1(), dcState1));
        int32_t a2    = fromAudio(acCouple(AudioIn2(), dcState2));
        int32_t inMix = sat(static_cast<int64_t>(mul(a1, in1Gain)) +
                                static_cast<int64_t>(mul(a2, in2Gain)));

        // Add internal feedback.
        int32_t fbApplied = mul(feedbackSignal(), feedbackGain);
        int32_t x = sat(static_cast<int64_t>(inMix) + static_cast<int64_t>(fbApplied));
        inputLevel = smoothLevel(inputLevel, abs32(x));

        // I path: delayed to align with the FIR group delay.
        int32_t I = delay(x);
        int32_t Q = hilbert(x);

        // Advance oscillator and look up sin/cos.
        phase += phaseIncSigned;
        int32_t sn, cs;
        sincos(phase, sn, cs);

        // Single-sideband mixing.
        int32_t yI     = mul(I, cs);
        int32_t yQ     = mul(Q, sn);
        lowSideband  = sat(static_cast<int64_t>(yI) + static_cast<int64_t>(yQ));
        highSideband = sat(static_cast<int64_t>(yI) - static_cast<int64_t>(yQ));

        int32_t absLow  = abs32(lowSideband);
        int32_t absHigh = abs32(highSideband);
        outputLevel = smoothLevel(outputLevel, absHigh > absLow ? absHigh : absLow);

        AudioOut1(toAudio(lowSideband));
        AudioOut2(toAudio(highSideband));
    }

private:
    // Hilbert FIR
    int32_t h[HTAPS]        = {};
    int32_t firState[HTAPS] = {};
    int     firStatePtr     = 0;

    // I-path alignment delay
    int32_t delayBuf[DELAYRB] = {};
    int     dWrite = 0;
    int     dRead  = DELAYRB - (HTAPS - 1) / 2;  // pre-offset by group delay

    // Oscillator (phaseIncSigned is uint32_t but treated as two's-complement
    // so negative frequencies correctly decrement the accumulator)
    uint32_t phase          = 0;
    uint32_t phaseIncSigned = 0;

    // Lookup tables
    int32_t  sinLUT[SIN_LUT_N]          = {};
    uint32_t freqLUT[FREQ_LUT_N] = {};

    // Signal state
    int32_t lowSideband    = 0;
    int32_t highSideband   = 0;
    int32_t inputLevel     = 0;
    int32_t outputLevel    = 0;
    int32_t dcState1       = 0;  // per-channel DC-blocking state
    int32_t dcState2       = 0;

    // Control state
    int32_t  in1Gain       = 0x7FFFFFFF;
    int32_t  in2Gain       = 0;
    int32_t  feedbackGain  = 0;
    int32_t  feedbackBlend = 0x40000000;
    int32_t  currentShiftHz   = 0;
    uint16_t controlDivider   = 0;

    // -----------------------------------------------------------------------
    // Fixed-point helpers
    // -----------------------------------------------------------------------
    
    static int16_t acCouple(int16_t input, int32_t& s) {
        s += static_cast<int32_t>(input) - (s >> 12);
        return static_cast<int16_t>(input - static_cast<int16_t>(s >> 12));
    }

    static int16_t clamp12(int32_t v) {
        if (v < 0)    return 0;
        if (v > 4095) return 4095;
        return static_cast<int16_t>(v);
    }

    static int32_t fromQ12(int32_t vQ12) {
        return sat(static_cast<int64_t>(vQ12) << 19);
    }

    static int32_t fromAudio(int16_t sample) {
        return static_cast<int32_t>(sample) << AUDIO_SHIFT;
    }

    static int16_t toAudio(int32_t value) {
        int32_t s = static_cast<int32_t>(value >> AUDIO_SHIFT);
        if (s > AUDIO_MAX) s = AUDIO_MAX;
        if (s < AUDIO_MIN) s = AUDIO_MIN;
        return static_cast<int16_t>(s);
    }

    static int32_t sat(int64_t a) {
        if (a >  0x7FFFFFFFLL) return  0x7FFFFFFF;
        if (a < -0x80000000LL) return static_cast<int32_t>(0x80000000U);
        return static_cast<int32_t>(a);
    }

    static int32_t mul(int32_t a, int32_t b) {
        int64_t acc = static_cast<int64_t>(a) * static_cast<int64_t>(b);
        return sat(acc >> 31);
    }

    static int32_t abs32(int32_t v) {
        if (v >= 0) return static_cast<int32_t>(v);
        if (v == static_cast<int32_t>(0x80000000U)) return 0x7FFFFFFF;
        return static_cast<int32_t>(-v);
    }

    static uint16_t toLed(int32_t v) {
        if (v <= 0) return 0;
        uint32_t s = static_cast<uint32_t>(v) >> 19;
        if (s > 4095U) s = 4095U;
        return static_cast<uint16_t>(s);
    }

    static int32_t smoothLevel(int32_t prev, int32_t cur) {
        if (cur > prev) return prev + ((cur  - prev) >> 2);
        return           prev - ((prev - cur)  >> 6);
    }

    // -----------------------------------------------------------------------
    // Hilbert FIR
    //
    // Exploits two properties of the antisymmetric Hilbert filter:
    //   h[n] = -h[N-1-n]  → one multiply per symmetric pair
    //   h[n] = 0 for even (n-mid)  → half of all pairs are zero
    // This yields (HTAPS-1)/4 multiplies per sample.
    //
    // static_assert enforces that mid=(HTAPS-1)/2 is odd, placing non-zero
    // coefficients on even-indexed taps. If mid is even, change loop start
    // from 0 to 1.
    // -----------------------------------------------------------------------
    int32_t hilbert(int32_t x) {
        firState[firStatePtr] = x;
        if (++firStatePtr >= HTAPS) firStatePtr = 0;

        int64_t acc = 0;
        constexpr int mid = (HTAPS - 1) / 2;

        static_assert(((HTAPS - 1) / 2) % 2 == 1,
            "HTAPS gives even mid; change loop start from 0 to 1");

        for (int n = 0; n < mid; n += 2) {
            int new_idx = firStatePtr - 1 - n;
            if (new_idx < 0) new_idx += HTAPS;
            int old_idx = firStatePtr + n;
            if (old_idx >= HTAPS) old_idx -= HTAPS;
            // Keep operands as int32_t so the compiler uses smull (32×32→64)
            // rather than a slow software 64×64 multiply.
            int32_t diff = firState[new_idx] - firState[old_idx];
            acc += static_cast<int64_t>(h[n]) * static_cast<int64_t>(diff);
        }
        return sat(acc >> 31);
    }

    // -----------------------------------------------------------------------
    // I-path alignment delay
    // -----------------------------------------------------------------------
    int32_t delay(int32_t x) {
        delayBuf[dWrite] = x;
        if (++dWrite >= DELAYRB) dWrite = 0;
        int32_t y = delayBuf[dRead];
        if (++dRead >= DELAYRB) dRead = 0;
        return y;
    }

    // -----------------------------------------------------------------------
    // Control + LED update
    // -----------------------------------------------------------------------
    void updateControl() {

        int32_t mainQ12    = clamp12(KnobVal(Knob::Main));
        int32_t shiftCVQ12 = clamp12(CVIn1() + 2048);
        int32_t shiftPos   = clamp12(mainQ12 + (shiftCVQ12 - 2048));

        const Switch sw = SwitchVal();

        if (sw == Switch::Up) {
            // Wide, log-scaled bipolar range.
            int32_t magIdx = (shiftPos >= 2048) ? (shiftPos - 2048) : (2047 - shiftPos);
            if (magIdx < 0) magIdx = 0;
            if (magIdx >= FREQ_LUT_N) magIdx = FREQ_LUT_N - 1;
            int32_t coarseHz = phaseIncToHz(freqLUT[magIdx]);
            currentShiftHz   = (shiftPos >= 2048) ? coarseHz : -coarseHz;
        } else {
            // Middle / Down: narrow linear range.
            int32_t centered = shiftPos - 2048;
            currentShiftHz   = (centered * NARROW_MAX_SHIFT_HZ) / 2048;
        }

        phaseIncSigned = hzToPhaseInc(currentShiftHz);
        updateLeds();

        int32_t xQ12    = clamp12(KnobVal(Knob::X));
        feedbackGain = fromQ12(xQ12);

        int32_t yQ12 = clamp12(KnobVal(Knob::Y));
        if (yQ12 < 50) {
            in1Gain = 0x7FFFFFFF;
            in2Gain = 0;
        } else if (yQ12 > 4045) {
            in1Gain = 0;
            in2Gain = 0x7FFFFFFF;
        } else {
            in2Gain = fromQ12(yQ12);
            in1Gain = static_cast<int32_t>(0x7FFFFFFF - in2Gain);
        }

        int32_t cv2Q12   = clamp12(CVIn2() + 2048);
        feedbackBlend = fromQ12(cv2Q12);
    }

    // -----------------------------------------------------------------------
    // Feedback mix
    // -----------------------------------------------------------------------
    int32_t feedbackSignal() const {
        int32_t downPart = mul(lowSideband,
                                    static_cast<int32_t>(0x7FFFFFFF - feedbackBlend));
        int32_t upPart   = mul(highSideband, feedbackBlend);
        return sat(static_cast<int64_t>(downPart) + static_cast<int64_t>(upPart)) >> 1;
    }

    // -----------------------------------------------------------------------
    // Phase / frequency helpers
    // -----------------------------------------------------------------------
    int32_t phaseIncToHz(uint32_t inc) const {
        uint64_t n = static_cast<uint64_t>(inc) * FS;
        return static_cast<int32_t>(n >> PHASE_BITS);
    }

    static uint32_t hzToPhaseInc(int32_t shiftHz) {
        int64_t n = static_cast<int64_t>(shiftHz) * static_cast<int64_t>(1ULL << PHASE_BITS);
        return static_cast<uint32_t>(n / FS);
    }

    void sincos(uint32_t ph, int32_t& s, int32_t& c) const {
        constexpr uint32_t LUT_MASK = SIN_LUT_N - 1;
        uint32_t idx   = ph >> (PHASE_BITS - SIN_LUT_BITS);
        uint32_t idx90 = (idx + SIN_LUT_N / 4) & LUT_MASK;
        s = sinLUT[idx & LUT_MASK];
        c = sinLUT[idx90];
    }

    // -----------------------------------------------------------------------
    // LED display
    // -----------------------------------------------------------------------
    void updateLeds() {
        LedBrightness(0, toLed(inputLevel));
        LedBrightness(1, toLed(outputLevel));

        int32_t magHz  = currentShiftHz >= 0 ? currentShiftHz : -currentShiftHz;
        int32_t maxHz  = (SwitchVal() == Switch::Up) ? WIDE_MAX_SHIFT_HZ : NARROW_MAX_SHIFT_HZ;
        uint16_t shiftLevel = 0;
        if (maxHz > 0) {
            int32_t scaled = (magHz * 4095) / maxHz;
            if (scaled < 0)    scaled = 0;
            if (scaled > 4095) scaled = 4095;
            shiftLevel = static_cast<uint16_t>(scaled);
        }

        LedBrightness(2, currentShiftHz >= 0 ? shiftLevel : 0);
        LedBrightness(3, currentShiftHz  < 0 ? shiftLevel : 0);

        LedBrightness(4, toLed(feedbackBlend));
        LedBrightness(5, toLed(static_cast<int32_t>(0x7FFFFFFF - feedbackBlend)));
    }

    // -----------------------------------------------------------------------
    // Initialisation 
    // -----------------------------------------------------------------------
    void buildHilbert() {
        const int mid = (HTAPS - 1) / 2;

        for (int n = 0; n < HTAPS; ++n) {
            int    k     = n - mid;
            double ideal = (k != 0 && (k & 1)) ? (2.0 / (kPi * k)) : 0.0;
            double w     = 0.54 - 0.46 * std::cos(2.0 * kPi * n / (HTAPS - 1));
            double scaled = ideal * w * 2147483647.0;
            if (scaled >  2147483647.0) scaled =  2147483647.0;
            if (scaled < -2147483648.0) scaled = -2147483648.0;
            h[n] = static_cast<int32_t>(std::llround(scaled));
        }

        // Enforce exact antisymmetry after quantisation.
        for (int n = 0; n < mid; ++n) {
            int64_t val  = static_cast<int64_t>(h[n]) - static_cast<int64_t>(h[HTAPS - 1 - n]);
            h[n]         = static_cast<int32_t>(val / 2);
            h[HTAPS-1-n] = static_cast<int32_t>(-h[n]);
        }
        h[mid] = 0;
    }

    void buildSinLUT() {
        for (int i = 0; i < SIN_LUT_N; ++i) {
            double angle = 2.0 * kPi * i / SIN_LUT_N;
            sinLUT[i] = static_cast<int32_t>(std::llround(std::sin(angle) * 2147483647.0));
        }
    }

    void buildFreqLUT() {
        const double minHz = static_cast<double>(WIDE_MIN_SHIFT_HZ);
        const double maxHz = static_cast<double>(WIDE_MAX_SHIFT_HZ);
        const double ratio = std::pow(maxHz / minHz, 1.0 / (FREQ_LUT_N - 1));
        double hz = minHz;
        for (int i = 0; i < FREQ_LUT_N; ++i) {
            freqLUT[i] = hzToPhaseInc(static_cast<int32_t>(std::llround(hz)));
            hz *= ratio;
        }
    }
};

int main() {
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    set_sys_clock_khz(200000, true);
    FreqShifter freqShift;
    freqShift.Init();
    freqShift.Run();
}
