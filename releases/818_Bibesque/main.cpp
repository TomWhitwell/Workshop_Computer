// Bib 1.1.0-beta for Workshop Computer: original 64-frame Bib DSP.
//
// This does not use ComputerCard.h. It borrows the Workshop pin map, ADC DNL
// correction, mux cadence, and MCP4822 formatting from ComputerCard v0.3.0
// (MIT, Chris Johnson) while providing a 64-frame block hand-off to core 1.

#include <cstdint>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/sync.h"

#include "bib_block_support.h"
#include "tanh_table.h"
#include "bib_dsp.h"

namespace
{
constexpr uint32_t kSampleRate = 48000;
constexpr uint8_t kBlockFrames = 64;
constexpr uint8_t kBlockCount = 4;
constexpr uint8_t kBlockMask = kBlockCount - 1;
constexpr uint8_t kOutputDelayBlocks = 2;

static_assert((kBlockCount & kBlockMask) == 0, "block count must be a power of two");

// Workshop Computer hardware map.
constexpr uint kPulseIn1 = 2;
constexpr uint kPulseIn2 = 3;
constexpr uint kLedPins[] = {10, 11, 12, 13, 14, 15};
constexpr uint kNormalisationProbe = 4;
constexpr uint kDacSck = 18;
constexpr uint kDacTx = 19;
constexpr uint kDacCs = 21;
constexpr uint kMuxA = 24;
constexpr uint kMuxB = 25;
constexpr uint kAudioRight = 26;
constexpr uint kAudioLeft = 27;
constexpr uint kMuxIo1 = 28;
constexpr uint kMuxIo2 = 29;

constexpr uint16_t kDacChannelA = 0x0000;
constexpr uint16_t kDacChannelB = 0x8000;
constexpr int kWavefoldDriveCap = 3072;
constexpr int kDelayFeedbackCap = 2490;
constexpr int kShimmerMaximum = 3072;

struct BibParameters
{
    int drive;
    int delaySend;
    int delayTime;
    int delayFeedback;
    int reverbSend;
    int reverbFeedback;
    int wetDryMix;
    int outputLevel;
    int wavefold;
    int shimmer;
    int tapeRateQ16;
    bool tapeActive;
};

// The four pairs are Bib's original pages: drive/send, delay time/feedback,
// reverb send/feedback, then mix/output. Values retain their original defaults
// while the Main control selects which page X and Y edit.
int pageValues[4][2] = {
    {2048, 2048},
    {2048, 2048},
    {0, 3072},
    {2048, 2048},
};
struct StereoFrame
{
    int16_t left;
    int16_t right;
};

struct StereoBlock
{
    StereoFrame frames[kBlockFrames];
};

enum class SwitchPosition : uint8_t { Down, Middle, Up };

struct BibSwitchState
{
    SwitchPosition stable = SwitchPosition::Middle;
    SwitchPosition candidate = SwitchPosition::Middle;
    uint8_t candidateBlocks = 3;
    bool wavefold = false;
    uint32_t blockClock = 0;
    uint32_t tapSequenceStartBlock = 0;
    uint32_t lastTapBlock = 0;
    uint32_t tapOffsetsSamples[8] = {};
    uint8_t recordedTapCount = 0;
    int tapDelayTime = 16384;
    int delayTimePickupControl = 0;
    bool tapDelayActive = false;
    uint32_t lastPulse1Block = 0;
    uint32_t clockPeriodSamples = 0;
    int clockedDelayTime = 16384;
    int clockHandoffControl = 0;
    bool clockSync = false;
    bool clockHandoff = false;
    bool actionsArmed = false;
    // Match Bibesque's audible startup character, while keeping headroom
    // below original Bib's maximum pressure-derived shimmer setting.
    int shimmerAmount = 1024;
    int tapeSpeed = 2048;
    int tapeWobble = 0;
    uint16_t tapeWobblePhase = 0;
};

struct PagePickup
{
    int lastControl[2] = {};
    bool primed = false;
};

BibSwitchState bibSwitchState = {};
PagePickup pagePickups[6] = {};
uint8_t activePage = 0xff;

struct ControlSnapshot
{
    int16_t knobs[3];
    int16_t cv[2];
    SwitchPosition switchPosition;
    uint8_t pulseRisingMask;
    uint8_t connectedMask;
};

// Each slot has one producer and one consumer. Core 0 writes input slots and
// consumes output slots; core 1 does the opposite. The flags are release/acquire
// hand-offs, with no locks or FIFO traffic in the sample ISR.
alignas(4) StereoBlock inputBlocks[kBlockCount] = {};
alignas(4) StereoBlock outputBlocks[kBlockCount] = {};
alignas(4) ControlSnapshot controlBlocks[kBlockCount] = {};
volatile bool inputReady[kBlockCount] = {};
volatile bool outputReady[kBlockCount] = {};

uint16_t adcBuffers[2][8] = {};
uint16_t dacBuffers[2][2] = {};
uint8_t adcPhase = 0;
uint8_t muxState = 0;
uint8_t captureBlock = 0;
uint8_t captureFrame = 0;
uint8_t activeOutputBlock = 0;
bool activeOutputValid = false;
uint32_t blocksCaptured = 0;
volatile uint32_t inputOverruns = 0;
volatile uint32_t outputUnderruns = 0;
uint8_t adcDma = 0;
uint8_t spiDma = 0;
int32_t knobSmoothing[4] = {};
int32_t cvSmoothing[2] = {};
int16_t knobValues[3] = {};
int16_t cvValues[2] = {};
SwitchPosition switchPosition = SwitchPosition::Middle;
bool previousPulse[2] = {};
uint8_t pendingPulseRising = 0;
uint16_t normalisationHistory = 0;
uint16_t plugHistory[6] = {};
bool connected[6] = {};
uint8_t normalisationPhase = 0;
uint8_t pulseFlashBlocks = 0;

int32_t Clamp12(int32_t value)
{
    if (value < -2048) return -2048;
    if (value > 2047) return 2047;
    return value;
}

int ClampControl(int value)
{
    if (value < 0) return 0;
    if (value > 4095) return 4095;
    return value;
}

uint16_t DacWord(int32_t value, uint16_t channel)
{
    const uint16_t code = static_cast<uint16_t>((Clamp12(value) & 0x0fff) + 0x0800) & 0x0fff;
    return channel | 0x3000 | code;
}

void CorrectAdcDnl(uint16_t &value)
{
    const uint16_t shifted = value + 512;
    value += ((value & 0x03ff) == 0x01ff) << 2;
    value += (shifted >> 10) << 3;
    value = static_cast<uint32_t>(value * 520349) >> 19;
}

int16_t ToBibRange(int32_t sample12)
{
    return static_cast<int16_t>(Clamp12(sample12) << 4);
}

int32_t FromBibRange(int16_t sample16)
{
    return sample16 >> 4;
}

void SetLed(uint8_t index, bool on)
{
    pwm_set_gpio_level(kLedPins[index], on ? 65535 : 0);
}

void SetLedBrightness(uint8_t index, int value)
{
    const uint16_t brightness = static_cast<uint16_t>(ClampControl(value) << 4);
    pwm_set_gpio_level(kLedPins[index], brightness);
}

uint32_t NextNormalisationBit()
{
    static uint32_t state = 1;
    state = state * 1664525u + 1013904223u;
    return state >> 31;
}

uint8_t ConnectedMask()
{
    uint8_t mask = 0;
    for (uint8_t index = 0; index < 6; ++index)
    {
        mask |= static_cast<uint8_t>(connected[index]) << index;
    }
    return mask;
}

void UpdateNormalisation(const uint16_t samples[8], uint8_t capturedMuxState, bool pulse1, bool pulse2)
{
    if (normalisationPhase == 0)
    {
        const uint32_t bit = NextNormalisationBit();
        gpio_put(kNormalisationProbe, bit);
        normalisationHistory = static_cast<uint16_t>((normalisationHistory << 1) | bit);
    }

    const uint8_t cvIndex = capturedMuxState & 1;
    if (normalisationPhase == 14 || normalisationPhase == 15)
    {
        plugHistory[2 + cvIndex] = static_cast<uint16_t>((plugHistory[2 + cvIndex] << 1) | (samples[7] < 1800));
    }

    if (normalisationPhase == 15)
    {
        plugHistory[0] = static_cast<uint16_t>((plugHistory[0] << 1) | (samples[5] < 1800));
        plugHistory[1] = static_cast<uint16_t>((plugHistory[1] << 1) | (samples[4] < 1800));
        plugHistory[4] = static_cast<uint16_t>((plugHistory[4] << 1) | pulse1);
        plugHistory[5] = static_cast<uint16_t>((plugHistory[5] << 1) | pulse2);
        for (uint8_t index = 0; index < 6; ++index)
        {
            connected[index] = normalisationHistory != plugHistory[index];
        }
    }
    normalisationPhase = (normalisationPhase + 1) & 0x0f;
}

void UpdateControlInputs(uint16_t samples[8], uint8_t capturedMuxState)
{
    // ComputerCard scans one of Main/X/Y/Switch per mux state and alternates
    // CV 1/2. Keep its filter coefficients and switch thresholds so later
    // block cards begin from familiar Workshop control values.
    const uint8_t controlIndex = capturedMuxState;
    knobSmoothing[controlIndex] = (127 * knobSmoothing[controlIndex] + 16 * samples[6]) >> 7;
    const int16_t rawControl = static_cast<int16_t>(knobSmoothing[controlIndex] >> 4);
    if (controlIndex < 3)
    {
        knobValues[controlIndex] = rawControl;
    }
    else
    {
        switchPosition = static_cast<SwitchPosition>((rawControl > 1000) + (rawControl > 3000));
    }

    CorrectAdcDnl(samples[7]);
    const uint8_t cvIndex = capturedMuxState & 1;
    cvSmoothing[cvIndex] = (15 * cvSmoothing[cvIndex] + 16 * samples[7]) >> 4;
    cvValues[cvIndex] = static_cast<int16_t>(2048 - (cvSmoothing[cvIndex] >> 4));
}

uint8_t BibMode(int16_t mainControl)
{
    const int32_t clamped = ClampControl(mainControl);
    return static_cast<uint8_t>(clamped >> 10);
}

int BibDelayTime(int control)
{
    // This is update_delay_time() expressed directly in the Workshop's 12-bit
    // control domain. It retains Bib's quadratic 8..98304-sample response.
    const int32_t squared = static_cast<int32_t>(control) * control;
    return clampi((squared * 96) >> 14, 8, 96 * 1024);
}

void PrimePagePickup(uint8_t mode, int xControl, int yControl)
{
    pagePickups[mode].lastControl[0] = xControl;
    pagePickups[mode].lastControl[1] = yControl;
    pagePickups[mode].primed = true;
}

void UpdatePickedControl(uint8_t mode, uint8_t index, int control)
{
    PagePickup &pickup = pagePickups[mode];
    if (!pickup.primed)
    {
        pickup.lastControl[index] = control;
        pickup.primed = true;
        return;
    }

    const int delta = control - pickup.lastControl[index];
    constexpr int kMovementThreshold = 16;
    if (delta <= kMovementThreshold && delta >= -kMovementThreshold)
    {
        return;
    }

    pickup.lastControl[index] = control;
    int &stored = pageValues[mode][index];
    const int separation = stored - control;
    if (separation <= kMovementThreshold && separation >= -kMovementThreshold)
    {
        stored = control;
        return;
    }

    // Bib's catch-up response, rescaled from q24 to the Workshop's 12-bit
    // controls. This avoids a parameter jump from a parked physical knob.
    constexpr int kSmall = 1;
    constexpr int kUpper = 4096;
    int warp;
    if (delta > 0)
    {
        warp = ((kUpper + kSmall - stored) << 4) / maxi(1, (kUpper + kSmall - control) >> 4);
    }
    else
    {
        warp = ((kSmall + stored) << 4) / maxi(1, (kSmall + control) >> 4);
    }
    warp = clampi(warp, 12, 1280);
    stored = ClampControl(stored + ((warp * delta) >> 8));
}

void UpdateShimmerControl(int control)
{
    PagePickup &pickup = pagePickups[4];
    if (!pickup.primed)
    {
        pickup.lastControl[0] = control;
        pickup.primed = true;
        return;
    }

    const int delta = control - pickup.lastControl[0];
    constexpr int kMovementThreshold = 16;
    if (delta <= kMovementThreshold && delta >= -kMovementThreshold)
    {
        return;
    }
    pickup.lastControl[0] = control;

    // Switch Up is a separate retained shimmer page. Relative pickup avoids
    // applying a parked X position as an accidental shimmer setting.
    bibSwitchState.shimmerAmount = clampi(bibSwitchState.shimmerAmount + delta, 0, kShimmerMaximum);
}

void UpdateTapeControls(int xControl, int yControl)
{
    PagePickup &pickup = pagePickups[5];
    if (!pickup.primed)
    {
        pickup.lastControl[0] = xControl;
        pickup.lastControl[1] = yControl;
        pickup.primed = true;
        return;
    }

    constexpr int kMovementThreshold = 16;
    const int xDelta = xControl - pickup.lastControl[0];
    const int yDelta = yControl - pickup.lastControl[1];
    if (xDelta > kMovementThreshold || xDelta < -kMovementThreshold)
    {
        pickup.lastControl[0] = xControl;
        bibSwitchState.tapeSpeed = ClampControl(bibSwitchState.tapeSpeed + xDelta);
    }
    if (yDelta > kMovementThreshold || yDelta < -kMovementThreshold)
    {
        pickup.lastControl[1] = yControl;
        bibSwitchState.tapeWobble = ClampControl(bibSwitchState.tapeWobble + yDelta);
    }
}

int TapeRateQ16(int speed, int wobble)
{
    // X maps linearly from stopped to 2x transport. Y recreates Bibesque's
    // slow, bounded wow/flutter around that selected tape rate.
    bibSwitchState.tapeWobblePhase = static_cast<uint16_t>(bibSwitchState.tapeWobblePhase + 131);
    int32_t triangle = bibSwitchState.tapeWobblePhase < 32768 ?
        bibSwitchState.tapeWobblePhase : 65535 - bibSwitchState.tapeWobblePhase;
    triangle = (triangle << 1) - 32768;
    const int32_t baseRate = static_cast<int32_t>(speed) << 5;
    const int32_t depth = (static_cast<int32_t>(wobble) * 1024) >> 12;
    const int32_t modulation = (depth * triangle) >> 13;
    return clampi(baseRate + ((baseRate * modulation) >> 14), 0, 131072);
}

void ResetDelayTaps()
{
    num_delay_taps = 1;
    delay_tap_times_q12[0] = 4096;
    delay_tap_levels_q12[0] = 4096;
}

void RecordDelayTap(int xControl)
{
    // Bib's spider records a one-second phrase. The Workshop switch does not
    // have pressure, so each captured head is deliberately equal level.
    constexpr uint32_t kTapTimeoutBlocks = kSampleRate / kBlockFrames;
    if (bibSwitchState.tapSequenceStartBlock == 0 ||
        bibSwitchState.blockClock - bibSwitchState.lastTapBlock > kTapTimeoutBlocks)
    {
        bibSwitchState.tapSequenceStartBlock = bibSwitchState.blockClock;
        bibSwitchState.recordedTapCount = 0;
    }
    else if (bibSwitchState.recordedTapCount < 8)
    {
        const uint32_t elapsedBlocks = bibSwitchState.blockClock - bibSwitchState.tapSequenceStartBlock;
        const uint32_t fullTime = elapsedBlocks * kBlockFrames;
        bibSwitchState.tapOffsetsSamples[bibSwitchState.recordedTapCount++] = fullTime;

        // The most recent tap is the full delay period. Earlier taps become
        // Q12 fractions of it, exactly matching process_bib()'s tap arrays.
        bibSwitchState.tapDelayTime = clampi(static_cast<int>(fullTime), 8, 96 * 1024);
        for (uint8_t tap = 0; tap < bibSwitchState.recordedTapCount; ++tap)
        {
            const uint64_t ratio = (static_cast<uint64_t>(bibSwitchState.tapOffsetsSamples[tap]) << 12) / fullTime;
            delay_tap_times_q12[tap] = static_cast<uint16_t>(mini(ratio, 4096));
            delay_tap_levels_q12[tap] = 4096;
        }
        num_delay_taps = bibSwitchState.recordedTapCount;
        bibSwitchState.delayTimePickupControl = xControl;
        bibSwitchState.tapDelayActive = true;
    }
    bibSwitchState.lastTapBlock = bibSwitchState.blockClock;
    // A manual tap phrase is the player's deliberate timing choice, so it
    // takes precedence over an earlier external clock.
    bibSwitchState.clockSync = false;
    bibSwitchState.clockHandoff = false;
}

int QuantiseDelayToClock(int target, uint32_t clockPeriod)
{
    // Bib's clock selection looks across octaves, then chooses the nearest
    // 3/4, straight, or dotted division without changing the stored X value.
    uint64_t period = clockPeriod;
    for (int attempt = 0; attempt < 24 && period != 0; ++attempt)
    {
        const uint64_t dotted = (period * 3u) / 2u;
        const uint64_t below = dotted / 2u;
        if (below > static_cast<uint32_t>(target))
        {
            period >>= 1;
            continue;
        }
        if (dotted <= static_cast<uint32_t>(target))
        {
            period <<= 1;
            continue;
        }

        int closest = target;
        uint32_t distance = 0xffffffffu;
        const uint64_t candidates[] = {below, period, dotted};
        for (uint64_t candidate : candidates)
        {
            if (candidate < 8 || candidate >= 96u * 1024u) continue;
            const int value = static_cast<int>(candidate);
            const uint32_t difference = value > target ? value - target : target - value;
            if (difference < distance)
            {
                distance = difference;
                closest = value;
            }
        }
        return closest;
    }
    return target;
}

void UpdatePulseClock(const ControlSnapshot &controls, int xControl)
{
    if ((controls.pulseRisingMask & 0x01) != 0)
    {
        if (bibSwitchState.lastPulse1Block != 0)
        {
            const uint32_t interval = (bibSwitchState.blockClock - bibSwitchState.lastPulse1Block) * kBlockFrames;
            constexpr uint32_t kMinimumClockPeriod = kSampleRate / 20; // 50 ms
            constexpr uint32_t kMaximumClockPeriod = kSampleRate * 2;
            const bool plausible = !bibSwitchState.clockSync ||
                (interval >= (bibSwitchState.clockPeriodSamples >> 1) &&
                 interval <= (bibSwitchState.clockPeriodSamples << 1));
            if (interval >= kMinimumClockPeriod && interval < kMaximumClockPeriod && plausible)
            {
                bibSwitchState.clockPeriodSamples = interval;
                bibSwitchState.clockSync = true;
                bibSwitchState.clockHandoff = false;
            }
        }
        bibSwitchState.lastPulse1Block = bibSwitchState.blockClock;
    }

    if (bibSwitchState.clockSync && bibSwitchState.lastPulse1Block != 0 &&
        (bibSwitchState.blockClock - bibSwitchState.lastPulse1Block) * kBlockFrames >
            (bibSwitchState.clockPeriodSamples << 1))
    {
        bibSwitchState.clockSync = false;
        bibSwitchState.clockHandoff = true;
        bibSwitchState.clockHandoffControl = xControl;
        bibSwitchState.lastPulse1Block = 0;
    }
}

void UpdateLeds(uint8_t mode, int xValue, int yValue, bool shimmerPage,
                const BibParameters &parameters)
{
    // This is Bibesque's practical Workshop display: a binary page marker,
    // effective X/Y including CV, then two useful state indicators.
    SetLed(0, (mode & 0x01) != 0);
    SetLed(1, (mode & 0x02) != 0);
    SetLedBrightness(2, shimmerPage ? parameters.shimmer : xValue);
    SetLedBrightness(3, yValue);

    switch (mode)
    {
    case 0:
        SetLed(4, bibSwitchState.wavefold);
        SetLed(5, parameters.delaySend < 0);
        break;
    case 1:
        SetLed(4, bibSwitchState.clockSync);
        SetLedBrightness(5, num_delay_taps * 512);
        break;
    case 2:
        SetLedBrightness(4, parameters.shimmer);
        SetLedBrightness(5, ClampControl((parameters.wetDryMix + 4096) >> 1));
        break;
    default:
        SetLed(4, bibSwitchState.actionsArmed && bibSwitchState.stable == SwitchPosition::Down);
        SetLedBrightness(5, parameters.outputLevel);
        break;
    }
}

bool UpdateSwitchDebounce(SwitchPosition requested)
{
    if (requested != bibSwitchState.candidate)
    {
        bibSwitchState.candidate = requested;
        bibSwitchState.candidateBlocks = 1;
        return false;
    }

    if (bibSwitchState.candidateBlocks < 3)
    {
        ++bibSwitchState.candidateBlocks;
        return false;
    }

    if (bibSwitchState.stable == bibSwitchState.candidate)
    {
        if (bibSwitchState.stable != SwitchPosition::Down)
        {
            bibSwitchState.actionsArmed = true;
        }
        return false;
    }

    bibSwitchState.stable = bibSwitchState.candidate;
    if (bibSwitchState.stable != SwitchPosition::Down)
    {
        bibSwitchState.actionsArmed = true;
        return false;
    }
    return bibSwitchState.actionsArmed;
}

BibParameters UpdateBibParameters(const ControlSnapshot &controls)
{
    ++bibSwitchState.blockClock;
    const uint8_t mode = BibMode(controls.knobs[0]);
    const int xControl = ClampControl(controls.knobs[1]);
    const int yControl = ClampControl(controls.knobs[2]);
    const bool switchDownPressed = UpdateSwitchDebounce(controls.switchPosition);
    UpdatePulseClock(controls, xControl);
    const bool tapePage = mode == 1 && bibSwitchState.stable == SwitchPosition::Up;
    const bool shimmerPage = mode == 2 && bibSwitchState.stable == SwitchPosition::Up;
    const uint8_t controlPage = tapePage ? 5 : (shimmerPage ? 4 : mode);
    if (activePage != controlPage)
    {
        activePage = controlPage;
        PrimePagePickup(controlPage, xControl, yControl);
    }
    if (mode == 1 && bibSwitchState.tapDelayActive)
    {
        const int delta = xControl - bibSwitchState.delayTimePickupControl;
        if (delta > 64 || delta < -64)
        {
            bibSwitchState.tapDelayActive = false;
            ResetDelayTaps();
        }
    }
    if (mode == 1 && bibSwitchState.clockHandoff)
    {
        const int delta = xControl - bibSwitchState.clockHandoffControl;
        if (delta > 64 || delta < -64)
        {
            bibSwitchState.clockHandoff = false;
        }
    }
    if (tapePage)
    {
        UpdateTapeControls(xControl, yControl);
    }
    else if (shimmerPage)
    {
        UpdateShimmerControl(xControl);
    }
    else
    {
        UpdatePickedControl(mode, 0, xControl);
        UpdatePickedControl(mode, 1, yControl);
    }
    if (switchDownPressed && mode == 0)
    {
        bibSwitchState.wavefold = !bibSwitchState.wavefold;
    }
    if (switchDownPressed && mode == 1)
    {
        RecordDelayTap(xControl);
    }

    const int xCv = (controls.connectedMask & (1u << 2)) != 0 ? controls.cv[0] : 0;
    const int yCv = (controls.connectedMask & (1u << 3)) != 0 ? controls.cv[1] : 0;
    const int xValue = tapePage ? ClampControl(bibSwitchState.tapeSpeed + xCv) : ClampControl(pageValues[mode][0] + xCv);
    const int yValue = tapePage ? ClampControl(bibSwitchState.tapeWobble + yCv) : ClampControl(pageValues[mode][1] + yCv);
    const int drive = ClampControl(pageValues[0][0] + xCv);
    const int delaySend = ClampControl(pageValues[0][1] + yCv);
    const int manualDelayTime = BibDelayTime(ClampControl(pageValues[1][0] + xCv));
    int delayTime = bibSwitchState.tapDelayActive ? bibSwitchState.tapDelayTime : manualDelayTime;
    if (bibSwitchState.clockSync)
    {
        delayTime = QuantiseDelayToClock(delayTime, bibSwitchState.clockPeriodSamples);
        bibSwitchState.clockedDelayTime = delayTime;
    }
    else if (bibSwitchState.clockHandoff)
    {
        delayTime = bibSwitchState.clockedDelayTime;
    }

    BibParameters parameters = {
        drive,
        (delaySend << 1) - 4096,
        delayTime,
        mini(ClampControl(pageValues[1][1] + yCv), kDelayFeedbackCap),
        ClampControl(pageValues[2][0] + xCv),
        ClampControl(pageValues[2][1] + yCv),
        (ClampControl(pageValues[3][0] + xCv) << 1) - 4096,
        ClampControl(pageValues[3][1] + yCv),
        bibSwitchState.wavefold,
        mini(kShimmerMaximum, ClampControl(bibSwitchState.shimmerAmount + xCv)),
        tapePage ? TapeRateQ16(xValue, yValue) : 65536,
        tapePage,
    };

    // Original Bib's mode-3 spider hold is a dub gesture: it mutes the live
    // drive/send path and puts delay feedback at maximum for as long as held.
    if (mode == 3 && bibSwitchState.actionsArmed && bibSwitchState.stable == SwitchPosition::Down)
    {
        if (parameters.wetDryMix < 0) parameters.wetDryMix = 0;
        parameters.drive = 0;
        parameters.delaySend = 0;
        parameters.delayFeedback = 4096;
    }
    UpdateLeds(mode, xValue, yValue, shimmerPage, parameters);
    return parameters;
}

void ProcessBlock(const StereoBlock &input, const ControlSnapshot &controls, StereoBlock &output)
{
    static_assert(sizeof(StereoFrame) == sizeof(int16_t) * 2, "Bib requires interleaved stereo frames");

    // process_bib() operates in-place on exactly 64 interleaved stereo frames.
    // Its source, delay/reverb algorithm, and lookup tables are copied from
    // the public Buddies repository without DSP changes for this test.
    const BibParameters parameters = UpdateBibParameters(controls);
    for (uint8_t frame = 0; frame < kBlockFrames; ++frame)
    {
        output.frames[frame] = input.frames[frame];
    }
    // The original wavefold can reach an intentionally destructive top-end
    // gain. Keep regular drive unchanged, but make the folded range musical.
    const int drive = parameters.wavefold ? mini(parameters.drive, kWavefoldDriveCap) : parameters.drive;
    process_bib(reinterpret_cast<int16_t *>(output.frames), drive, parameters.delaySend, parameters.delayTime,
                parameters.delayFeedback, parameters.reverbSend, parameters.reverbFeedback, parameters.shimmer,
                parameters.wetDryMix,
                parameters.outputLevel, 0, parameters.tapeRateQ16, 0, parameters.tapeActive ? 2 : 0,
                parameters.wavefold);
}

void __not_in_flash_func(BlockWorker)()
{
    uint8_t readBlock = 0;
    while (true)
    {
        if (!inputReady[readBlock] || outputReady[readBlock])
        {
            tight_loop_contents();
            continue;
        }

        // Acquire a fully written input slot before reading it.
        __dmb();
        ProcessBlock(inputBlocks[readBlock], controlBlocks[readBlock], outputBlocks[readBlock]);
        // Publish output before allowing core 0 to reuse the input slot.
        __dmb();
        outputReady[readBlock] = true;
        __dmb();
        inputReady[readBlock] = false;
        readBlock = (readBlock + 1) & kBlockMask;
    }
}

void __isr __not_in_flash_func(AudioDmaComplete)()
{
    const uint8_t completedPhase = adcPhase;
    const uint8_t capturedMuxState = muxState;
    adcPhase = 1 - adcPhase;

    // Restart ADC capture and DAC transmission before doing any conversion.
    dma_hw->ints0 = 1u << adcDma;
    dma_channel_set_write_addr(adcDma, adcBuffers[adcPhase], true);
    dma_channel_set_read_addr(spiDma, dacBuffers[adcPhase], true);

    uint16_t right0 = adcBuffers[completedPhase][0];
    uint16_t left0 = adcBuffers[completedPhase][1];
    uint16_t right1 = adcBuffers[completedPhase][4];
    uint16_t left1 = adcBuffers[completedPhase][5];
    const bool pulse1 = !gpio_get(kPulseIn1);
    const bool pulse2 = !gpio_get(kPulseIn2);
    if (pulse1 && !previousPulse[0]) pendingPulseRising |= 0x01;
    if (pulse2 && !previousPulse[1]) pendingPulseRising |= 0x02;
    previousPulse[0] = pulse1;
    previousPulse[1] = pulse2;
    UpdateNormalisation(adcBuffers[completedPhase], capturedMuxState, pulse1, pulse2);
    UpdateControlInputs(adcBuffers[completedPhase], capturedMuxState);

    CorrectAdcDnl(right0);
    CorrectAdcDnl(left0);
    CorrectAdcDnl(right1);
    CorrectAdcDnl(left1);

    // Audio input op-amps are inverting. Match ComputerCard's two-conversion
    // average, then convert at the boundary to Bib's native 16-bit range.
    int32_t inputRight = -((static_cast<int32_t>(right0) + right1 - 0x1000) >> 1);
    int32_t inputLeft = -((static_cast<int32_t>(left0) + left1 - 0x1000) >> 1);
    // An unpatched input contains the normalisation probe sequence. Once the
    // probe classifies a jack as disconnected, never let that sequence enter
    // the block audio stream as audible white noise.
    if (!connected[0]) inputLeft = 0;
    if (!connected[1]) inputRight = 0;
    inputBlocks[captureBlock].frames[captureFrame] = {ToBibRange(inputLeft), ToBibRange(inputRight)};

    if (captureFrame == 0)
    {
        activeOutputBlock = (captureBlock + kBlockCount - kOutputDelayBlocks) & kBlockMask;
        activeOutputValid = outputReady[activeOutputBlock];
        if (!activeOutputValid && blocksCaptured >= kOutputDelayBlocks)
        {
            ++outputUnderruns;
            SetLed(1, true);
        }
    }

    StereoFrame output = {};
    if (activeOutputValid)
    {
        output = outputBlocks[activeOutputBlock].frames[captureFrame];
    }
    // DAC output hardware is also inverting.
    dacBuffers[completedPhase][0] = DacWord(-FromBibRange(output.left), kDacChannelA);
    dacBuffers[completedPhase][1] = DacWord(-FromBibRange(output.right), kDacChannelB);

    // Keep the external mux in the proven ComputerCard cadence. This first
    // transport pass does not yet expose control/CV values to a card program.
    muxState = (muxState + 1) & 0x03;
    gpio_put(kMuxA, muxState & 0x01);
    gpio_put(kMuxB, muxState & 0x02);

    ++captureFrame;
    if (captureFrame != kBlockFrames) return;

    captureFrame = 0;
    ++blocksCaptured;
    controlBlocks[captureBlock] = {
        {knobValues[0], knobValues[1], knobValues[2]},
        {cvValues[0], cvValues[1]},
        switchPosition,
        pendingPulseRising,
        ConnectedMask(),
    };
    if (pendingPulseRising & 0x01) pulseFlashBlocks = 75;
    pendingPulseRising = 0;
    __dmb();
    inputReady[captureBlock] = true;
    __dmb();

    if (activeOutputValid)
    {
        __dmb();
        outputReady[activeOutputBlock] = false;
    }

    const uint8_t nextCapture = (captureBlock + 1) & kBlockMask;
    if (inputReady[nextCapture])
    {
        ++inputOverruns;
        SetLed(0, true);
    }
    captureBlock = nextCapture;
}

void InitialiseLeds()
{
    for (uint8_t index = 0; index < 6; index += 2)
    {
        gpio_set_function(kLedPins[index], GPIO_FUNC_PWM);
        gpio_set_function(kLedPins[index + 1], GPIO_FUNC_PWM);
        pwm_config config = pwm_get_default_config();
        pwm_config_set_wrap(&config, 65535);
        pwm_init(pwm_gpio_to_slice_num(kLedPins[index]), &config, true);
        pwm_init(pwm_gpio_to_slice_num(kLedPins[index + 1]), &config, true);
        SetLed(index, false);
        SetLed(index + 1, false);
    }
}

void InitialiseHardware()
{
    InitialiseLeds();

    gpio_init(kMuxA);
    gpio_init(kMuxB);
    gpio_set_dir(kMuxA, GPIO_OUT);
    gpio_set_dir(kMuxB, GPIO_OUT);
    gpio_put(kMuxA, false);
    gpio_put(kMuxB, false);

    gpio_init(kNormalisationProbe);
    gpio_set_dir(kNormalisationProbe, GPIO_OUT);
    gpio_put(kNormalisationProbe, false);

    gpio_init(kPulseIn1);
    gpio_init(kPulseIn2);
    gpio_set_dir(kPulseIn1, GPIO_IN);
    gpio_set_dir(kPulseIn2, GPIO_IN);
    gpio_pull_up(kPulseIn1);
    gpio_pull_up(kPulseIn2);

    adc_init();
    adc_gpio_init(kAudioRight);
    adc_gpio_init(kAudioLeft);
    adc_gpio_init(kMuxIo1);
    adc_gpio_init(kMuxIo2);
    adc_select_input(0);
    adc_set_round_robin(0x0f);
    adc_fifo_setup(true, true, 1, false, false);
    // 48 MHz / 125 = 384 kHz: two samples from each ADC input per frame.
    adc_set_clkdiv(124);

    spi_init(spi0, 15625000);
    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(kDacSck, GPIO_FUNC_SPI);
    gpio_set_function(kDacTx, GPIO_FUNC_SPI);
    gpio_set_function(kDacCs, GPIO_FUNC_SPI);

    adcDma = dma_claim_unused_channel(true);
    spiDma = dma_claim_unused_channel(true);

    dma_channel_config adcConfig = dma_channel_get_default_config(adcDma);
    channel_config_set_transfer_data_size(&adcConfig, DMA_SIZE_16);
    channel_config_set_read_increment(&adcConfig, false);
    channel_config_set_write_increment(&adcConfig, true);
    channel_config_set_dreq(&adcConfig, DREQ_ADC);
    dma_channel_configure(adcDma, &adcConfig, adcBuffers[adcPhase], &adc_hw->fifo, 8, false);

    dma_channel_config spiConfig = dma_channel_get_default_config(spiDma);
    channel_config_set_transfer_data_size(&spiConfig, DMA_SIZE_16);
    channel_config_set_dreq(&spiConfig, DREQ_SPI0_TX);
    dma_channel_configure(spiDma, &spiConfig, &spi_get_hw(spi0)->dr, nullptr, 2, false);

    dma_channel_set_irq0_enabled(adcDma, true);
    irq_set_exclusive_handler(DMA_IRQ_0, AudioDmaComplete);
    irq_set_enabled(DMA_IRQ_0, true);
    adc_run(true);
    dma_channel_set_write_addr(adcDma, adcBuffers[adcPhase], true);
}
} // namespace

int main()
{
    // Keep the known-good Bib card clock. The block runtime can later be
    // measured at 192 and 240 MHz without changing its scheduling design.
    set_sys_clock_khz(192000, true);
    InitialiseHardware();
    multicore_launch_core1(BlockWorker);

    while (true)
    {
        tight_loop_contents();
    }
}
