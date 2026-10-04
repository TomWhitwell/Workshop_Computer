#include "ComputerCard.h"
#include <cmath>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

/*
 * Sheep: A crunchy granular delay and digital degradation effect
 * by Dune Desormeaux (https://dessertplanet.co)
 * Thank you to Émilie Gillet for Clouds which was a huge inspiration here!
 * Sheep features:
 * - 2 UF2's to choose from based on fidelity + buffer length:
 * - Lofi: 5.2-second stereo circular buffer for audio capture (125k 8-bit samples at 24kHz)
 * - Hifi: 2.6-second stereo circular buffer for audio capture (62.5k 12-bit samples at 24kHz)
 * - Up to 14 simultaneous grains
 * - Linear grain sizes from micro (32 samples = ~0.001 seconds) to macro (24000 samples = 1 second)
 * - Bidirectional playback (-2x to +2x speed)
 * - Loop/glitch mode for captured segment looping
 *
 * Controls:
 * - Main Knob: Grain playback speed/direction (-2x to +2x, center=pause) OR pitch attenuverter when CV2 connected
 * - X Knob: Grain position spread (0=fixed delay, right=random spread) OR attenuverter when CV1 connected (left=invert, center=off, right=normal)
 * - Y Knob: Grain size
 * - CV1: Grain position control (0-6V covers full range, negative values wrap from end) with X knob as attenuverter
 * - CV2: Pitch control (-6V to +6V = -2x to +2x speed) with Main knob as attenuverter
 * - Switch: Up=Freeze Buffer, Middle=Normal, Down=Loop/glitch Mode
 * - Pulse 1 In: grain TRIGGER (rising edge)
 * - Pulse 2 In: Grain GATE
 *
 * Outputs:
 * - Audio Outs: Granular processed audio (stereo)
 * - CV Out 1: Random noise value (updates when grains are triggered)
 * - CV Out 2: Rising sawtooth LFO (0V to 6V) fixed rate, aligned to circular buffer phase
 * - Pulse 1 Out: Triggers when any grain reaches 90% completion (optimized for continuous looping)
 * - Pulse 2 Out: Stochastic clock - triggers when noise < X knob value, rate inversely proportional to grain size
 *
 * LED Feedback:
 * - LEDs 0,1: Audio output volume
 * - LEDs 2,3: CV output levels (brightness = CV voltage magnitude)
 * - LEDs 4,5: Pulse output states (on/off)
 */

// Audio format configuration - controlled by build system
#ifdef LOFI_MODE
	#define BUFF_LENGTH_SAMPLES 125000 // 125,000 samples = 5.2 seconds at 24kHz (8-bit audio)
#else
	#define BUFF_LENGTH_SAMPLES 62500  // 62,500 samples = 2.6 seconds at 24kHz (12-bit audio)
#endif

// Always use full 12-bit audio range for processing (±2048) to maintain consistent volume
// The pack/unpack functions handle the storage bit depth conversion
#define AUDIO_RANGE 2048

#define MAX_GRAIN_SIZE 24000	   // 24,000 samples (1.0 seconds at 24kHz) - maximum grain size
#define MIN_GRAIN_SIZE 32		   // 32 samples (1.33ms at 24kHz) - minimum grain size

class Sheep : public ComputerCard
{
private:
	// 256-entry Hann window lookup table (Q12 format) - calculated at startup
	static constexpr int HANN_TABLE_SIZE = 256;
	int32_t hannWindowTable_[HANN_TABLE_SIZE];
	
	// Timing constants
	static const int32_t SAFETY_MARGIN_SAMPLES = 120;	 // 5ms safety margin
	static const int32_t GRAIN_END_PULSE_DURATION = 100; // 4.2ms pulse duration
	static const int32_t VIRTUAL_DETENT_THRESHOLD = 12;

	// Safety limits
	static const int32_t MAX_FRACTIONAL_ITERATIONS = 4;
	static const int32_t MAX_SAFE_GRAIN_SPEED = 8192;

	// Speed hysteresis to prevent scratchiness from knob noise
	static const int32_t SPEED_HYSTERESIS_THRESHOLD = 32;

	// Grain system constants
	static const int MAX_GRAINS = 14;							  // Maximum number of simultaneous grains
	static const int32_t GRAIN_COMPLETION_THRESHOLD_PERCENT = 90; // used for pulse 1 output when clocked

public:
	Sheep()
	{
		for (int i = 0; i < BUFF_LENGTH_SAMPLES; i++)
		{
			buffer_[i] = 0;
		}

		stretchRatio_ = 4096;
		grainPlaybackSpeed_ = 4096;
		previousGrainPlaybackSpeed_ = 4096; // Initialize to 1x speed for hysteresis
		previousLoopingControlValue_ = 4096; // Initialize to 1x speed for looping hysteresis
		grainSize_ = 1024;
		maxActiveGrains_ = MAX_GRAINS; // Maximum number of active grains
		cachedActiveGrainCount_ = 0;   // Initialize grain count cache
		loopMode_ = false;

		pulseOut1Counter_ = 0;
		pulseOut2Counter_ = 0;
		stochasticClockCounter_ = 0;
		stochasticClockPeriod_ = 2400;

		cvOut1NoiseValue_ = 0;
		cvOut2PhaseValue_ = 0;

		lastOutputL_ = 0;
		lastOutputR_ = 0;

		updateCounter_ = UPDATE_RATE_DIVIDER - 1;

		// Initialize grain timing variables
		globalSampleCounter_ = 0;
		minGrainDistance_ = 0;
		lastGrainTriggerTime_ = 0;

		// Initialize 12kHz notch filter state variables
		mix1L_ = mix2L_ = mixf1L_ = mixf2L_ = 0;
		mix1R_ = mix2R_ = mixf1R_ = mixf2R_ = 0;


		for (int i = 0; i < MAX_GRAINS; i++)
		{
			grains_[i].active = false;
			grains_[i].readPos = 0;
			grains_[i].readFrac = 0;
			grains_[i].sampleCount = 0;
			grains_[i].startPos = 0;
			grains_[i].loopSize = 0;
			grains_[i].looping = false;
			grains_[i].pulse90Triggered = false;
			grains_[i].grainSpeed = 4096; // Initialize to 1x speed (Q12 format)
			grains_[i].baselineControlValue = 4096; // Initialize baseline control value
		}

		// Calculate Hann window lookup table at startup
		// Formula: 0.5 * (1 - cos(2 * pi * n / (N-1))) * 4096 (Q12 format)
		// Using M_PI for maximum accuracy to eliminate grain boundary artifacts
		for (int i = 0; i < HANN_TABLE_SIZE; i++)
		{
			// Calculate normalized position (0.0 to 1.0)
			double pos = (double)i / (HANN_TABLE_SIZE - 1);

			// Calculate 2*pi*pos angle
			double angle = 2.0 * M_PI * pos;

			// Calculate cosine using standard library for maximum accuracy
			double cos_val = cos(angle);

			// Apply Hann window formula: 0.5 * (1 - cos(2*pi*n/(N-1)))
			double hann_double = 0.5 * (1.0 - cos_val);

			// Convert to Q12 format (multiply by 4096)
			int32_t hann_val = (int32_t)(hann_double * 4096.0 + 0.5); // +0.5 for rounding

			// Ensure perfect fade-in/fade-out at boundaries to eliminate clicks
			if (i == 0 || i == HANN_TABLE_SIZE - 1)
			{
				hann_val = 0; // Force zero at start and end
			}

			// Clamp to valid range
			if (hann_val < 0)
				hann_val = 0;
			if (hann_val > 4096)
				hann_val = 4096;

			hannWindowTable_[i] = hann_val;
		}

	}

	virtual void ProcessSample()
	{
		// Increment global sample counter for grain timing
		globalSampleCounter_++;

		Switch switchPos = SwitchVal();

		// Record audio when not in freeze mode (freeze mode stops recording but allows playback)
		if (switchPos != Switch::Up)
		{
			// Clip inputs to prevent overflow
			int16_t leftIn = clipAudio(AudioIn1());
			int16_t rightIn = clipAudio(AudioIn2());

			// Apply 12kHz notch filter to remove mux interference
			int32_t ooa0 = 16302, a2oa0 = 16221; // Q = 100, very narrow notch
			
			// Filter left channel
			int32_t leftFiltered = (ooa0 * (leftIn + mix2L_) - a2oa0 * mixf2L_) >> 14;
			mix2L_ = mix1L_;
			mix1L_ = leftIn;
			mixf2L_ = mixf1L_;
			mixf1L_ = leftFiltered;
			
			// Filter right channel
			int32_t rightFiltered = (ooa0 * (rightIn + mix2R_) - a2oa0 * mixf2R_) >> 14;
			mix2R_ = mix1R_;
			mix1R_ = rightIn;
			mixf2R_ = mixf1R_;
			mixf1R_ = rightFiltered;

			// Clip filtered outputs and pack into buffer
			leftIn = clipAudio(leftFiltered);
			rightIn = clipAudio(rightFiltered);
			auto stereoSample = packStereo(leftIn, rightIn);
			buffer_[writeHead_] = stereoSample;
		}

		// Always advance writeHead_ for timing and reference
		writeHead_++;
		if (writeHead_ >= BUFF_LENGTH_SAMPLES)
		{
			writeHead_ = 0;
		}

		// X knob controls delay time/spread or becomes attenuverter when CV1 connected
		int32_t xControlValue = cachedXKnob_;

		if (!Connected(Input::CV1))
		{
			if (xControlValue <= 2047)
			{
				// Left half: delay time control only
				delayDistance_ = 1200 + ((xControlValue * (80000 - 1200)) / 2047);
				minGrainDistance_ = 0; // Rate limiting permanently removed
				spreadAmount_ = 0;
			}
			else
			{
				// Right half: spread control with fixed delay - use longer default delay
				delayDistance_ = 20000;
				spreadAmount_ = ((xControlValue - 2048) * 4095) / 2047;
				minGrainDistance_ = 0; // No minimum distance when using spread control
			}
		}
		else
		{
			// CV1 connected: X knob becomes attenuverter
			delayDistance_ = 20000;
			spreadAmount_ = 0;
			minGrainDistance_ = 0; // No minimum distance when CV1 is connected
		}

		bool shouldTriggerGrain = PulseIn1RisingEdge();

		if (switchPos == Switch::Up)
		{
			if (shouldTriggerGrain)
			{
				// Check Pulse Input 2 gate before triggering
				if (!Connected(Input::Pulse2) || PulseIn2())
				{
					triggerNewGrain();
				}
			}

			int16_t outL = generateStretchedSample(0);
			int16_t outR = generateStretchedSample(1);

			// Clip outputs to prevent overflow and ensure clean audio
			outL = clipAudio(outL);
			outR = clipAudio(outR);

			// Store output amplitudes for LED feedback
			lastOutputL_ = outL;
			lastOutputR_ = outR;

			AudioOut1(outL);
			AudioOut2(outR);
		}
		else if (switchPos == Switch::Middle)
		{
			if (loopMode_)
			{
				exitLoopMode();
			}

			if (shouldTriggerGrain)
			{
				// Check Pulse Input 2 gate before triggering
				if (!Connected(Input::Pulse2) || PulseIn2())
				{
					triggerNewGrain();
				}
			}

			int16_t outL = generateStretchedSample(0);
			int16_t outR = generateStretchedSample(1);

			// Clip outputs to prevent overflow and ensure clean audio
			outL = clipAudio(outL);
			outR = clipAudio(outR);

			// Store output amplitudes for LED feedback
			lastOutputL_ = outL;
			lastOutputR_ = outR;

			AudioOut1(outL);
			AudioOut2(outR);
		}
		else
		{
			if (!loopMode_)
			{
				enterLoopMode();
			}

			int16_t outL = generateStretchedSample(0);
			int16_t outR = generateStretchedSample(1);

			// Clip outputs to prevent overflow and ensure clean audio
			outL = clipAudio(outL);
			outR = clipAudio(outR);

			// Store output amplitudes for LED feedback
			lastOutputL_ = outL;
			lastOutputR_ = outR;

			AudioOut1(outL);
			AudioOut2(outR);
		}

		updateGrains();

		// Auto-trigger initial grain in unclocked mode if no grains are active
		// This ensures the self-triggering chain gets started
		if (!Connected(Input::Pulse1))
		{
			// Count active grains
			int32_t activeCount = 0;
			for (int i = 0; i < MAX_GRAINS; i++)
			{
				if (grains_[i].active)
				{
					activeCount++;
				}
			}

			// If no grains are active in unclocked mode, trigger one to start the chain
			// But respect Pulse 2 gate if it's connected
			if (activeCount == 0)
			{
				if (Connected(Input::Pulse2))
				{
					// Pulse 2 is connected: only auto-trigger if Pulse 2 is high
					if (PulseIn2())
					{
						triggerNewGrain();
					}
				}
				else
				{
					// No pulse inputs connected: always auto-trigger
					triggerNewGrain();
				}
			}
		}

		updateCVOutputs();
		updatePulseOutputs();

		// Update control parameters at 1000Hz
		updateCounter_++;
		if (updateCounter_ >= UPDATE_RATE_DIVIDER)
		{
			updateCounter_ = 0;
			updateCachedKnobValues();
			updatePlaybackSpeed();
			updateGrainParameters();
			updateLEDFeedback();
		}
	}

private:
#ifdef LOFI_MODE
	uint16_t buffer_[BUFF_LENGTH_SAMPLES];  // 16-bit storage for two 8-bit samples
#else
	uint32_t buffer_[BUFF_LENGTH_SAMPLES];  // 32-bit storage for two 12-bit samples
#endif
	int32_t writeHead_ = 0;
	int32_t delayDistance_ = 8000;
	int32_t spreadAmount_ = 0;

	// Minimum grain distance control (left side of X knob)
	int32_t minGrainDistance_ = 0;	   // Minimum samples between grain triggers (0 = no minimum)
	int32_t lastGrainTriggerTime_ = 0; // Sample counter when last grain was triggered

	// Grain system
	struct Grain
	{
		int32_t readPos;
		int32_t readFrac;
		int32_t sampleCount;
		int32_t startPos;
		int32_t loopSize;
		bool active;
		bool looping;
		bool pulse90Triggered;
		// Per-grain parameters (snapshotted at trigger)
		int32_t delayDistance;
		int32_t spreadAmount;
		int32_t grainSize;
		int32_t grainSpeed; // Store speed for this grain's lifecycle
		int32_t baselineControlValue; // Control value when grain enters loop mode
	};
	Grain grains_[MAX_GRAINS];

	int32_t stretchRatio_;
	int32_t grainPlaybackSpeed_;
	int32_t previousGrainPlaybackSpeed_; // Track last applied speed for hysteresis
	int32_t previousLoopingControlValue_; // Track last applied control value for looping hysteresis
	int32_t grainSize_;
	int32_t maxActiveGrains_;
	int32_t cachedActiveGrainCount_; // Cache grain count to avoid repeated counting in calculateGrainWeight()
	bool loopMode_;

	// Pulse and CV output state
	int32_t pulseOut1Counter_;
	int32_t pulseOut2Counter_;
	int32_t stochasticClockCounter_;
	int32_t stochasticClockPeriod_;

	int16_t cvOut1NoiseValue_;
	int16_t cvOut2PhaseValue_;

	// Audio output amplitude tracking for LED feedback
	int16_t lastOutputL_;
	int16_t lastOutputR_;

	// Control update throttling
	static const int32_t UPDATE_RATE_DIVIDER = 24;
	int32_t updateCounter_;

	// Global sample counter for timing
	int32_t globalSampleCounter_;

	// Cached knob values
	int32_t cachedMainKnob_;
	int32_t cachedXKnob_;
	int32_t cachedYKnob_;

	// 12kHz notch filter state variables (to remove mux interference)
	int32_t mix1L_, mix2L_, mixf1L_, mixf2L_;  // Left channel
	int32_t mix1R_, mix2R_, mixf1R_, mixf2R_;  // Right channel


	// Interpolated sample reading with wraparound
	int16_t __not_in_flash_func(getInterpolatedSample)(int32_t bufferPos, int32_t frac, int channel)
	{
		// Ensure buffer position is within bounds
		int32_t pos1 = bufferPos;
		while (pos1 >= BUFF_LENGTH_SAMPLES)
			pos1 -= BUFF_LENGTH_SAMPLES;
		while (pos1 < 0)
			pos1 += BUFF_LENGTH_SAMPLES;

		int32_t pos2 = pos1 + 1;
		if (pos2 >= BUFF_LENGTH_SAMPLES)
			pos2 = 0;

		// Redundant safety check removed for performance
		// pos1 and pos2 are guaranteed to be in bounds after wraparound logic above

		int16_t sample1 = unpackStereo(buffer_[pos1], channel);
		int16_t sample2 = unpackStereo(buffer_[pos2], channel);

		// Removed fractional part clamping for performance
		// Assumes frac is always in valid range from caller

		// Linear interpolation in Q12
		int32_t diff = sample2 - sample1;
		int32_t interpolated = sample1 + ((diff * frac) >> 12);

		// Clamp result
		if (interpolated > (AUDIO_RANGE - 1))
			interpolated = (AUDIO_RANGE - 1);
		if (interpolated < -AUDIO_RANGE)
			interpolated = -AUDIO_RANGE;

		return (int16_t)interpolated;
	}

	// Calculate looping grain speed with scaled offset from original speed
	int32_t __not_in_flash_func(calculateLoopingGrainSpeed)(int32_t originalSpeed, int32_t baselineControlValue)
	{
		int32_t currentControlValue;

		if (Connected(Input::CV2))
		{
			// CV2 controls pitch, Main knob is attenuverter (use center detent only)
			// No hysteresis for CV2 - apply immediately for responsive CV control
			int32_t cv2Val = CVIn2();
			int32_t mainKnobVal = virtualDetentedKnob(cachedMainKnob_);
			currentControlValue = applyPitchAttenuverter(cv2Val, mainKnobVal);
		}
		else
		{
			// Main knob controls pitch directly (use multiple detents for musical speeds)
			int32_t mainKnobVal = pitchDetentedKnob(cachedMainKnob_);

			// Calculate new speed after detent processing
			if (mainKnobVal <= 2048)
			{
				currentControlValue = -8192 + ((mainKnobVal * 8192) >> 11);
			}
			else
			{
				int32_t rightKnob = mainKnobVal - 2048;
				currentControlValue = (rightKnob * 8192) >> 11;
			}
		}

		// Calculate offset from baseline (not center) - this prevents immediate jumps when entering loop mode
		int32_t offset = currentControlValue - baselineControlValue;

		// Apply scaled offset: finalSpeed = originalSpeed + (originalSpeed * offset / 4096)
		// This gives ±100% speed variation around the original grain speed
		int64_t temp64 = (int64_t)originalSpeed * offset;
		int32_t scaledOffset = (int32_t)(temp64 >> 12); // Divide by 4096
		int32_t finalSpeed = originalSpeed + scaledOffset;

		// Apply hysteresis to the final calculated speed (not CV2 mode for responsive CV control)
		if (!Connected(Input::CV2))
		{
			if (cabs(finalSpeed - previousLoopingControlValue_) <= SPEED_HYSTERESIS_THRESHOLD)
			{
				// Change is too small - keep previous final speed to prevent noise-induced changes
				finalSpeed = previousLoopingControlValue_;
			}
			else
			{
				// Update the looping control tracking variable with the new final speed
				previousLoopingControlValue_ = finalSpeed;
			}
		}

		// Apply safety limits
		if (finalSpeed > MAX_SAFE_GRAIN_SPEED)
		{
			finalSpeed = MAX_SAFE_GRAIN_SPEED;
		}
		if (finalSpeed < -MAX_SAFE_GRAIN_SPEED)
		{
			finalSpeed = -MAX_SAFE_GRAIN_SPEED;
		}

		return finalSpeed;
	}

	// Update playback speed (affects all active grains)
	void __not_in_flash_func(updatePlaybackSpeed)()
	{
		int32_t newSpeed;

		if (Connected(Input::CV2))
		{
			// CV2 controls pitch, Main knob is attenuverter (use center detent only)
			// No hysteresis for CV2 - apply immediately for responsive CV control
			int32_t cv2Val = CVIn2();
			int32_t mainKnobVal = virtualDetentedKnob(cachedMainKnob_);
			newSpeed = applyPitchAttenuverter(cv2Val, mainKnobVal);
		}
		else
		{
			// Main knob controls pitch directly (use multiple detents for musical speeds)
			// Apply hysteresis to prevent scratchiness from knob noise
			int32_t mainKnobVal = pitchDetentedKnob(cachedMainKnob_);

			// Calculate new speed after detent processing
			if (mainKnobVal <= 2048)
			{
				newSpeed = -8192 + ((mainKnobVal * 8192) >> 11);
			}
			else
			{
				int32_t rightKnob = mainKnobVal - 2048;
				newSpeed = (rightKnob * 8192) >> 11;
			}

			// Apply hysteresis - only update if change is significant
			if (cabs(newSpeed - previousGrainPlaybackSpeed_) <= SPEED_HYSTERESIS_THRESHOLD)
			{
				// Change is too small - keep previous speed to prevent noise-induced changes
				newSpeed = previousGrainPlaybackSpeed_;
			}
		}

		// Limit speed for safety
		if (newSpeed > MAX_SAFE_GRAIN_SPEED)
		{
			newSpeed = MAX_SAFE_GRAIN_SPEED;
		}
		if (newSpeed < -MAX_SAFE_GRAIN_SPEED)
		{
			newSpeed = -MAX_SAFE_GRAIN_SPEED;
		}

		// Update both current and previous speed
		grainPlaybackSpeed_ = newSpeed;
		previousGrainPlaybackSpeed_ = newSpeed;
	}

	// Update grain parameters (affects newly triggered grains only)
	void __not_in_flash_func(updateGrainParameters)()
	{
		int32_t yControlValue = cachedYKnob_;
		// Only edge detents are applied for the Y knob (no center detent), for smooth grain size control
		if (yControlValue > 4090)
		{
			yControlValue = 4095;
		}
		else if (yControlValue < 5)
		{
			yControlValue = 0;
		}

		// Map Y control to stretch ratio (full smooth range, no center detent)
		if (yControlValue <= 2048)
		{
			stretchRatio_ = 1024 + ((yControlValue * 3072) >> 11);
		}
		else
		{
			int32_t rightKnob = yControlValue - 2048;
			stretchRatio_ = 4096 + ((rightKnob * 12288) >> 11);
		}

		// Calculate grain size from stretch ratio
		int32_t normalizedRatio = stretchRatio_ - 1024;
		normalizedRatio = (normalizedRatio * 4096) / 15360;

		if (normalizedRatio < 0)
			normalizedRatio = 0;
		if (normalizedRatio > 4095)
			normalizedRatio = 4095;

		grainSize_ = MIN_GRAIN_SIZE + ((normalizedRatio * (MAX_GRAIN_SIZE - MIN_GRAIN_SIZE)) / 4095);

		if (grainSize_ < MIN_GRAIN_SIZE)
			grainSize_ = MIN_GRAIN_SIZE;
		if (grainSize_ > MAX_GRAIN_SIZE)
			grainSize_ = MAX_GRAIN_SIZE;
	}

	int16_t virtualDetentedKnob(int16_t val)
	{
		if (val > 4090)
		{
			val = 4095;
		}
		else if (val < 5)
		{
			val = 0;
		}

		// Center detent for pause/freeze
		if (cabs(val - 2048) < VIRTUAL_DETENT_THRESHOLD)
		{
			val = 2048;
		}

		return val;
	}

	// Pitch control detents for direct pitch control mode (when CV2 is not connected)
	int16_t pitchDetentedKnob(int16_t val)
	{
		if (val > 4090)
		{
			val = 4095;
		}
		else if (val < 5)
		{
			val = 0;
		}

		// Larger threshold for pitch detents to make them easier to find
		static const int32_t PITCH_DETENT_THRESHOLD = 20;

		// Multiple detents for musically useful speeds
		// Center detent: 0x (pause/freeze)
		if (cabs(val - 2048) < PITCH_DETENT_THRESHOLD)
		{
			val = 2048;
		}
		// +1x detent (normal forward speed)
		else if (cabs(val - 3584) < PITCH_DETENT_THRESHOLD)
		{
			val = 3584;
		}
		// +0.5x detent (half forward speed)
		else if (cabs(val - 3072) < PITCH_DETENT_THRESHOLD)
		{
			val = 3072;
		}
		// -0.5x detent (half reverse speed)
		else if (cabs(val - 1024) < PITCH_DETENT_THRESHOLD)
		{
			val = 1024;
		}
		// -1x detent (normal reverse speed)
		else if (cabs(val - 512) < PITCH_DETENT_THRESHOLD)
		{
			val = 512;
		}

		return val;
	}

	// Apply knob as attenuverter to CV input
	int32_t __not_in_flash_func(applyAttenuverter)(int32_t cvValue, int32_t knobValue)
	{
		// Map knob to scale factor -2.0 to +2.0
		int32_t scaleFactor = ((knobValue - 2048) * 4) + 4096;

		int32_t scaledCV = (cvValue * scaleFactor) >> 12;

		// Convert to 0-4095 range
		int32_t result = scaledCV + 2048;

		if (result < 0)
			result = 0;
		if (result > 4095)
			result = 4095;

		return result;
	}

	// Pitch attenuverter function: applies Main knob as ±1x attenuverter to CV2 pitch input
	int32_t __not_in_flash_func(applyPitchAttenuverter)(int32_t cv2Value, int32_t mainKnobValue)
	{
		// cv2Value: -2048 to +2047 (±5V CV input)
		// mainKnobValue: 0 to 4095 (Main knob with detents)
		// Returns: Q12 speed value (4096 ± attenuated CV2 offset)

		// Map Main knob to ±1x gain factor, with 0 gain at center (2048)
		// 0 -> -4096 (-1x), 2048 -> 0 (0x = no effect), 4095 -> +4096 (+1x)
		int32_t gainFactor;
		if (mainKnobValue == 2048)
		{
			// Exact center - ensure zero gain for perfect 1x speed
			gainFactor = 0;
		}
		else if (mainKnobValue < 2048)
		{
			// Left half: -1x to 0x gain
			gainFactor = -4096 + ((mainKnobValue * 4096) >> 11); // -4096 to 0
		}
		else
		{
			// Right half: 0x to +1x gain
			gainFactor = ((mainKnobValue - 2048) * 4096) >> 11; // 0 to +4096
		}

		// Apply gain to CV2: cv2 * gain / 4096
		// This gives us the attenuated CV signal in the range ±2048
		int32_t attenuatedCV = (cv2Value * gainFactor) >> 12;

		// Convert attenuated CV to speed offset: ±2048 CV -> ±8192 speed (±2x in Q12)
		int32_t speedOffset = attenuatedCV * 4; // Scale CV range to speed range

		// Apply offset relative to normal 1x speed (4096 in Q12)
		int32_t result = 4096 + speedOffset; // Base 1x speed + CV offset

		// Clamp to ±2x speed range around 1x
		if (result > 12288)
			result = 12288; // +3x max (4096 + 8192)
		if (result < -4096)
			result = -4096; // -1x min (4096 - 8192, but clamped)

		return result;
	}

	// Calculate unclocked grain trigger threshold based on Y knob for overlap control
	// Returns percentage (0-100) of grain completion at which to trigger next grain
	int32_t __not_in_flash_func(calculateUnclockTriggerThreshold)()
	{
		// Y knob controls overlap: Y=0 -> high threshold (less overlap), Y=max -> low threshold (more overlap)
		int32_t yValue = cachedYKnob_; // 0 to 4095

		// Inverted linear mapping: 90% at Y=0 (less overlap), decreasing to 10% at Y=max (more overlap)
		// threshold = 90 - (Y * 80 / 4095)
		int32_t triggerThreshold = 90 - ((yValue * 80) / 4095); // 90% to 10% threshold

		// Ensure threshold is within valid range
		if (triggerThreshold < 10)
			triggerThreshold = 10; // Minimum 10% (maximum overlap)
		if (triggerThreshold > 90)
			triggerThreshold = 90; // Maximum 90% (minimum overlap)

		return triggerThreshold;
	}

	void __not_in_flash_func(triggerNewGrain)()
	{
		// Count currently active grains to respect dynamic limit
		int32_t activeCount = 0;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				activeCount++;
			}
		}

		// Don't trigger new grain if we're at the current limit
		if (activeCount >= maxActiveGrains_)
		{
			return;
		}

		// Find an inactive grain slot
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (!grains_[i].active)
			{
				grains_[i].active = true;
				cachedActiveGrainCount_++; // Update cached count when grain is activated

				// Update last grain trigger time for minimum distance tracking
				lastGrainTriggerTime_ = globalSampleCounter_;

				// Snapshot delay, spread, grain size, and speed for this grain
				grains_[i].delayDistance = delayDistance_;
				grains_[i].spreadAmount = spreadAmount_;
				grains_[i].grainSize = grainSize_;
				grains_[i].grainSpeed = grainPlaybackSpeed_; // Snapshot current speed for grain's lifecycle

				// Reset pulse trigger flag for this grain
				grains_[i].pulse90Triggered = false;

				// Generate new noise value for CV Out 1 when grain is triggered
				cvOut1NoiseValue_ = (int16_t)((rnd12() & 0xFFF) - 2048); // -2048 to +2047

				// Calculate base playback position using write head for consistent delay timing
				int32_t basePlaybackPos = writeHead_ - grains_[i].delayDistance;
				if (basePlaybackPos < 0)
					basePlaybackPos += BUFF_LENGTH_SAMPLES;

				// Handle CV1 position control vs normal spread control
				int32_t playbackPos;
				if (Connected(Input::CV1))
				{
					// CV1 connected: CV1 controls grain position with X knob as attenuverter
					// Read CV1 input only when grain is triggered
					int32_t cv1Val = CVIn1();		 // -2048 to +2047 (±5V)
					int32_t xKnobVal = cachedXKnob_; // 0 to 4095 (X knob as attenuverter)

					// Convert CV1 to position control value with wrapping
					// 0-5V (cv1Val 0 to +2047) uses full range 0-4095
					// Negative values wrap around from the end
					int32_t rawPositionValue;
					if (cv1Val >= 0)
					{
						// Positive CV: scale 0-2047 to 0-4095 (double the resolution for 0-5V)
						rawPositionValue = (cv1Val * 4095) / 2047;
					}
					else
					{
						// Negative CV: wrap from end of range
						// -2048 to -1 maps to 4095 down to 2048 (wrapping from end)
						rawPositionValue = 4095 + ((cv1Val * 2048) / 2048); // cv1Val is negative, so this subtracts
					}

					// Clamp to valid range
					if (rawPositionValue < 0)
						rawPositionValue = 0;
					if (rawPositionValue > 4095)
						rawPositionValue = 4095;

					// Apply X knob as proper attenuverter
					// X knob at 0 = full inversion (-1x)
					// X knob at 2048 = no effect (0x = no effect), 4095 = full positive (+1x)
					int32_t positionControlValue;

					// Map X knob to gain factor: 0 -> -1x, 2048 -> 0x, 4095 -> +1x
					int32_t gainFactor;
					if (xKnobVal <= 2048)
					{
						// Left half: -1x to 0x
						gainFactor = -4096 + ((xKnobVal * 4096) / 2048); // -4096 to 0
					}
					else
					{
						// Right half: 0x to +1x
						gainFactor = ((xKnobVal - 2048) * 4096) / 2047; // 0 to +4096
					}

					// Apply attenuverter: center position (2048) + (CV offset * gain)
					int32_t cvOffset = rawPositionValue - 2048;			   // -2048 to +2047
					int32_t scaledOffset = (cvOffset * gainFactor) / 4096; // Apply gain
					positionControlValue = 2048 + scaledOffset;			   // Add back to center

					// Final clamp
					if (positionControlValue < 0)
						positionControlValue = 0;
					if (positionControlValue > 4095)
						positionControlValue = 4095;

					// Check if buffer is frozen (switch up)
					bool bufferIsFrozen = (SwitchVal() == Switch::Up);

					if (bufferIsFrozen)
					{
						// Freeze mode: CV1 scrubs the entire buffer (0-4095 maps to full buffer range)
						// 0 = beginning of buffer, 4095 = end of buffer
						playbackPos = (positionControlValue * (BUFF_LENGTH_SAMPLES - 1)) / 4095;
					}
					else
					{
						// Normal mode: CV1 directly controls buffer position
						// 0 = beginning of buffer, 4095 = end of buffer (no randomization/spread)
						playbackPos = (positionControlValue * (BUFF_LENGTH_SAMPLES - 1)) / 4095;
					}

					// Ensure position is within buffer bounds
					if (playbackPos >= BUFF_LENGTH_SAMPLES)
						playbackPos = BUFF_LENGTH_SAMPLES - 1;
					if (playbackPos < 0)
						playbackPos = 0;
				}
				else
				{
					// CV1 disconnected: Use normal spread control (original behavior)
					if (grains_[i].spreadAmount == 0)
					{
						playbackPos = basePlaybackPos;
					}
					else
					{
						int32_t randomValue = rnd12() & 0xFFF;	   // 0 to 4095
						int32_t randomOffset = randomValue - 2047; // -2047 to +2048, centered better
						const int32_t maxSafeOffset = BUFF_LENGTH_SAMPLES >> 3;
						int64_t temp64 = (int64_t)randomOffset * maxSafeOffset;
						temp64 >>= 11;
						if (temp64 > maxSafeOffset)
							temp64 = maxSafeOffset;
						if (temp64 < -maxSafeOffset)
							temp64 = -maxSafeOffset;
						temp64 = (temp64 * grains_[i].spreadAmount) >> 12;
						if (temp64 > maxSafeOffset)
							temp64 = maxSafeOffset;
						if (temp64 < -maxSafeOffset)
							temp64 = -maxSafeOffset;
						randomOffset = (int32_t)temp64;
						playbackPos = basePlaybackPos + randomOffset;
					}
				}
				while (playbackPos >= BUFF_LENGTH_SAMPLES)
					playbackPos -= BUFF_LENGTH_SAMPLES;
				while (playbackPos < 0)
					playbackPos += BUFF_LENGTH_SAMPLES;

				// Apply write head safety check ONLY when buffer is recording (not frozen)
				// In freeze mode, grains can access the entire buffer safely
				// Also skip safety check when CV1 is connected since position is explicitly controlled
				bool bufferIsFrozen = (SwitchVal() == Switch::Up);
				bool cv1Connected = Connected(Input::CV1);
				if (!bufferIsFrozen && !cv1Connected)
				{
					const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
					int32_t maxSafePos = writeHead_ - safetyMargin;
					if (maxSafePos < 0)
						maxSafePos += BUFF_LENGTH_SAMPLES;
					int32_t distanceFromWrite = writeHead_ - playbackPos;
					if (distanceFromWrite < 0)
						distanceFromWrite += BUFF_LENGTH_SAMPLES;
					if (distanceFromWrite < safetyMargin)
					{
						playbackPos = maxSafePos;
					}
				}

				grains_[i].readPos = playbackPos;
				grains_[i].readFrac = 0;
				grains_[i].startPos = grains_[i].readPos;
				grains_[i].sampleCount = 0;
				grains_[i].loopSize = grains_[i].grainSize;
				break;
			}
		}
	}

	int32_t __not_in_flash_func(calculateGrainWeight)(int grainIndex)
	{
		Grain &grain = grains_[grainIndex];

		// In loop/glitch mode, bypass windowing for harsh discontinuities
		if (grain.looping)
		{
			return 4096; // Full weight - no windowing for glitch effects
		}

		// Safety check to prevent division by zero
		if (grain.grainSize <= 0)
		{
			return 4096; // Full weight if grain size is invalid
		}

		// Count active grains to determine if windowing is needed
		// Use cached count for performance - updated only when grains are created/destroyed
		// Only apply windowing when multiple grains are active (overlapping)
		if (cachedActiveGrainCount_ <= 1)
		{
			return 4096; // Single grain - full weight for maximum clarity
		}

		// Normal windowing for overlapping grains
		// Position in grain: 0 to grainSize-1, normalized to 0-4095 (Q12)
		int32_t posQ12 = (grain.sampleCount << 12) / grain.grainSize; // Q12 normalized position (0-4095)
		if (posQ12 < 0)
			posQ12 = 0;
		if (posQ12 > 4095)
			posQ12 = 4095;

		// Map Q12 position to table lookup with proper interpolation
		// Scale posQ12 to table range: 0-4095 -> 0-(HANN_TABLE_SIZE-1) in Q12
		int32_t tablePos = (posQ12 * (HANN_TABLE_SIZE - 1)) >> 12;	  // Integer table index
		int32_t tableFrac = (posQ12 * (HANN_TABLE_SIZE - 1)) & 0xFFF; // Q12 fractional part

		// Clamp table index to valid range
		if (tablePos >= HANN_TABLE_SIZE - 1)
		{
			tablePos = HANN_TABLE_SIZE - 1;
			tableFrac = 0;
		}

		// Linear interpolation between table entries
		int32_t w0 = hannWindowTable_[tablePos];
		int32_t w1 = (tablePos < HANN_TABLE_SIZE - 1) ? hannWindowTable_[tablePos + 1] : hannWindowTable_[tablePos];
		int32_t weight = w0 + (((w1 - w0) * tableFrac) >> 12);

		// Ensure weight is never negative (should not happen with proper Hann window)
		if (weight < 0)
			weight = 0;

		return weight;
	}

	int16_t __not_in_flash_func(generateStretchedSample)(int channel)
	{
		int32_t mixedSample = 0;
		int32_t totalWeight = 0;

		// Mix all active grains
		for (int i = 0; i < maxActiveGrains_; i++)
		{
			if (grains_[i].active)
			{
				// Get interpolated sample from buffer with wraparound
				int16_t grainSample = getInterpolatedSample(grains_[i].readPos, grains_[i].readFrac, channel);
				int32_t weight = calculateGrainWeight(i);

				mixedSample += (grainSample * weight) >> 12; // Q12 format
				totalWeight += weight;
			}
		}

		// Handle output normalization with division by zero protection
		if (totalWeight > 0)
		{
			// Always normalize by total weight for consistent granular processing
			int32_t result = (mixedSample << 12) / totalWeight;

			// Clamp to prevent overflow
			if (result > 2047)
				result = 2047;
			if (result < -2048)
				result = -2048;

			return (int16_t)result;
		}
		else
		{
			// No active grains or zero total weight - return silence
			return 0;
		}
	}

	void __not_in_flash_func(updateGrains)()
	{
		// Update all active grains
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				// Get grain playback speed from this grain's stored speed
				int32_t grainSpeed = grains_[i].grainSpeed;

				// Handle looping grains differently
				if (grains_[i].looping)
				{
					// In loop mode, grains loop within their original captured segment
					// They advance through their grain but loop back to the start when finished
					// This creates repeating stutters of the captured audio segment

					grainSpeed = calculateLoopingGrainSpeed(grains_[i].grainSpeed, grains_[i].baselineControlValue); // Use original speed with scaled offset from baseline

					if (grainSpeed != 0)
					{
						// Increment sample count for windowing calculation
						grains_[i].sampleCount++;

						// Advance read position with fractional tracking
						grains_[i].readFrac += grainSpeed;

						// Handle integer overflow from fractional part
						while (grains_[i].readFrac >= 4096)
						{ // >= 1.0 in Q12
							grains_[i].readPos++;
							grains_[i].readFrac -= 4096;
						}

						// Handle negative speeds (reverse looping)
						while (grains_[i].readFrac < 0)
						{
							grains_[i].readPos--;
							grains_[i].readFrac += 4096;
						}
						// Loop back to start when grain reaches its end
						// This creates the stuttering loop effect
						if (grains_[i].sampleCount >= grains_[i].grainSize)
						{
							// Reset to beginning of grain segment for looping
							grains_[i].readPos = grains_[i].startPos;
							grains_[i].readFrac = 0;
							grains_[i].sampleCount = 0;
							grains_[i].pulse90Triggered = false; // Reset pulse trigger for next loop iteration
						}

						// Handle buffer wraparound for readPos - more efficient than while loops
						if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES)
						{
							grains_[i].readPos %= BUFF_LENGTH_SAMPLES;
						}
						if (grains_[i].readPos < 0)
						{
							grains_[i].readPos = ((grains_[i].readPos % BUFF_LENGTH_SAMPLES) + BUFF_LENGTH_SAMPLES) % BUFF_LENGTH_SAMPLES;
						}
					}

					// Looping grains never deactivate automatically
				}
				else
				{
					// Normal grain behavior
					grains_[i].sampleCount++;

					// Store original position for boundary checking
					int32_t originalPos = grains_[i].readPos;
					int32_t originalFrac = grains_[i].readFrac;

					// Advance read position with fractional tracking
					grains_[i].readFrac += grainSpeed;

					// CRASH PROTECTION: Bounded iteration limits to prevent runaway loops
					int32_t iterationCount = 0;

					// Handle integer overflow from fractional part with bounded iterations
					while (grains_[i].readFrac >= 4096 && iterationCount < MAX_FRACTIONAL_ITERATIONS)
					{
						grains_[i].readPos++;
						grains_[i].readFrac -= 4096;
						iterationCount++;

						// Handle buffer wraparound
						if (grains_[i].readPos >= BUFF_LENGTH_SAMPLES)
						{
							grains_[i].readPos -= BUFF_LENGTH_SAMPLES;
						}
					}

					// If we hit the iteration limit, clamp the fractional part
					if (grains_[i].readFrac >= 4096)
					{
						grains_[i].readFrac = 4095; // Clamp to just under 1.0
					}

					// Reset iteration counter for negative speed handling
					iterationCount = 0;

					// Handle negative speeds (reverse playback) with bounded iterations
					while (grains_[i].readFrac < 0 && iterationCount < MAX_FRACTIONAL_ITERATIONS)
					{
						grains_[i].readPos--;
						grains_[i].readFrac += 4096;
						iterationCount++;

						// Handle buffer wraparound
						if (grains_[i].readPos < 0)
						{
							grains_[i].readPos += BUFF_LENGTH_SAMPLES;
						}
					}

					// If we hit the iteration limit, clamp the fractional part
					if (grains_[i].readFrac < 0)
					{
						grains_[i].readFrac = 0; // Clamp to 0
					}

					// WRITE HEAD BOUNDARY CHECK: Prevent grains from reading past write head
					// Only apply this check when buffer is recording (not frozen)
					bool bufferIsFrozen = (SwitchVal() == Switch::Up);
					if (!bufferIsFrozen)
					{
						const int32_t safetyMargin = SAFETY_MARGIN_SAMPLES;
						int32_t maxSafePos = writeHead_ - safetyMargin;
						if (maxSafePos < 0)
							maxSafePos += BUFF_LENGTH_SAMPLES;

						// Calculate distance from grain to write head (accounting for circular buffer)
						int32_t distanceToWrite = writeHead_ - grains_[i].readPos;
						if (distanceToWrite < 0)
							distanceToWrite += BUFF_LENGTH_SAMPLES;

						// If grain is too close to write head, clamp it to safe position
						if (distanceToWrite < safetyMargin)
						{
							grains_[i].readPos = maxSafePos;
							grains_[i].readFrac = 0; // Reset fractional part when clamped
						}
					}

					// Check if grain has reached completion threshold and trigger Pulse 1
					if (grains_[i].grainSize > 0 && !grains_[i].pulse90Triggered)
					{
						// Use different thresholds for clocked vs unclocked modes
						int32_t thresholdPercent;
						if (Connected(Input::Pulse1))
						{
							// Clocked mode: use fixed 90% threshold for pulse output timing
							thresholdPercent = GRAIN_COMPLETION_THRESHOLD_PERCENT;
						}
						else
						{
							// Unclocked mode: use Y knob-controlled threshold for overlap behavior
							thresholdPercent = calculateUnclockTriggerThreshold();
						}

						int32_t thresholdSamples = (grains_[i].grainSize * thresholdPercent) / 100;
						if (grains_[i].sampleCount >= thresholdSamples)
						{
							grains_[i].pulse90Triggered = true; // Mark as triggered for this grain
							
							// Trigger pulse output only if counter is ready (maintains 100-sample pulse width)
							if (pulseOut1Counter_ <= 0)
							{
								pulseOut1Counter_ = GRAIN_END_PULSE_DURATION; // 100 samples
							}
							
							// Auto-trigger new grain regardless of pulse counter state (allows faster triggering)
							if (!Connected(Input::Pulse1))
							{
								if (Connected(Input::Pulse2))
								{
									// PulseIn2 is plugged in: only fire if high
									if (PulseIn2())
									{
										triggerNewGrain();
									}
								}
								else
								{
									// Neither pulse input is plugged in: always fire with Y knob-controlled timing
									triggerNewGrain();
								}
							}
						}
					}

					// Deactivate grain if it's finished
					if (grains_[i].sampleCount >= grains_[i].grainSize)
					{
						grains_[i].active = false;
						cachedActiveGrainCount_--; // Update cached count when grain is deactivated
					}
				}
			}
		}
	}

	// Update pulse outputs
	void __not_in_flash_func(updatePulseOutputs)()
	{
		// Update stochastic clock period based on grain size
		// Wider range than grain size: 240 samples (10ms) to 4800 samples (200ms) at 24kHz
		// Direct relationship: smaller grains = faster clock (shorter period), larger grains = slower clock (longer period)
		int32_t normalizedY = cachedYKnob_; // 0 to 4095
		int32_t maxPeriod = 4800;			// 200ms at 24kHz
		int32_t minPeriod = 240;			// 10ms at 24kHz
		// Inverse mapping: higher Y knob = shorter period
		stochasticClockPeriod_ = maxPeriod - ((normalizedY * (maxPeriod - minPeriod)) / 4095);
		// Removed conservative clamping for performance - calculation should always be in range

		// Update stochastic clock counter
		stochasticClockCounter_++;

		if (stochasticClockCounter_ >= stochasticClockPeriod_)
		{
			stochasticClockCounter_ = 0;

			// Generate random value and compare with X knob threshold
			int32_t randomValue = rnd12() & 0xFFF; // 0 to 4095
			int32_t xKnobValue = cachedXKnob_;	   // 0 to 4095

			// If random value is less than X knob value, trigger pulse
			// Higher X knob = higher threshold = more pulses
			if (randomValue < xKnobValue && pulseOut2Counter_ <= 0)
			{
				pulseOut2Counter_ = GRAIN_END_PULSE_DURATION; // 100 samples
			}
		}

		// Handle pulse output 1 (countdown)
		if (pulseOut1Counter_ > 0)
		{
			pulseOut1Counter_--;
			PulseOut1(true);
		}
		else
		{
			PulseOut1(false);
		}

		// Handle pulse output 2 (countdown)
		if (pulseOut2Counter_ > 0)
		{
			pulseOut2Counter_--;
			PulseOut2(true);
		}
		else
		{
			PulseOut2(false);
		}
	}

	// Loop mode functions
	void __not_in_flash_func(enterLoopMode)()
	{
		loopMode_ = true;

		// Calculate current control value to use as baseline for all grains entering loop mode
		int32_t currentControlValue;
		if (Connected(Input::CV2))
		{
			int32_t cv2Val = CVIn2();
			int32_t mainKnobVal = virtualDetentedKnob(cachedMainKnob_);
			currentControlValue = applyPitchAttenuverter(cv2Val, mainKnobVal);
		}
		else
		{
			int32_t mainKnobVal = pitchDetentedKnob(cachedMainKnob_);
			if (mainKnobVal <= 2048)
			{
				currentControlValue = -8192 + ((mainKnobVal * 8192) >> 11);
			}
			else
			{
				int32_t rightKnob = mainKnobVal - 2048;
				currentControlValue = (rightKnob * 8192) >> 11;
			}
		}

		// Check if any grains are currently active
		bool hasActiveGrains = false;
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active)
			{
				hasActiveGrains = true;
				grains_[i].looping = true;
				grains_[i].baselineControlValue = currentControlValue; // Capture baseline when entering loop mode
				// Use the grain's stored loop size (set when grain was created)
				// This prevents race condition where grainSize_ changes after grain creation
				// Keep current sampleCount for smooth transition to loop mode
			}
		}

		// If no grains are active, trigger one grain to ensure we have something to loop
		if (!hasActiveGrains)
		{
			triggerNewGrain();
			// Now convert the newly triggered grain to looping mode
			for (int i = 0; i < MAX_GRAINS; i++)
			{
				if (grains_[i].active && !grains_[i].looping)
				{
					grains_[i].looping = true;
					grains_[i].baselineControlValue = currentControlValue; // Capture baseline for new grain
					// Keep initial sampleCount for proper windowing
					break; // Only need to convert the first one we find
				}
			}
		}
	}

	void __not_in_flash_func(exitLoopMode)()
	{
		loopMode_ = false;

		// Convert all looping grains back to normal mode
		for (int i = 0; i < MAX_GRAINS; i++)
		{
			if (grains_[i].active && grains_[i].looping)
			{
				grains_[i].looping = false;
				grains_[i].loopSize = 0;
				// Keep current sample count for smooth transition from loop mode
			}
		}
	}

	// Update CV outputs
	void __not_in_flash_func(updateCVOutputs)()
	{
		// CV Out 1: Current noise value (updated when grains are triggered)
		CVOut1(cvOut1NoiseValue_);

		// CV Out 2: Write head position mapped to 0-5V (0 to 2047)
		cvOut2PhaseValue_ = (int16_t)((writeHead_ * 2047) / (BUFF_LENGTH_SAMPLES - 1));
		if (cvOut2PhaseValue_ > 2047)
			cvOut2PhaseValue_ = 2047;
		CVOut2(cvOut2PhaseValue_);
	}

	// Update LED feedback for all outputs
	void __not_in_flash_func(updateLEDFeedback)()
	{
		// LEDs 0,1: Audio outputs (brightness based on output amplitude)
		uint16_t ledL = (uint16_t)((cabs(lastOutputL_) * 4095) / 2048); // Scale from ±2048 to 0-4095
		uint16_t ledR = (uint16_t)((cabs(lastOutputR_) * 4095) / 2048); // Scale from ±2048 to 0-4095

		LedBrightness(0, ledL);
		LedBrightness(1, ledR);

		// LEDs 2,3: CV outputs (brightness based on CV level)
		uint16_t ledCV1 = (uint16_t)((cabs(cvOut1NoiseValue_) * 4095) / 2048);
		uint16_t ledCV2 = (uint16_t)((cvOut2PhaseValue_ * 4095) / 2047); // Phase is 0-2047

		LedBrightness(2, ledCV1);
		LedBrightness(3, ledCV2);

		// LEDs 4,5: Pulse outputs (on/off based on pulse state)
		LedOn(4, pulseOut1Counter_ > 0);
		LedOn(5, pulseOut2Counter_ > 0);
	}

	// RNG! Different values for each card but the same on each boot
	uint32_t __not_in_flash_func(rnd12)()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed ^= UniqueCardID() >> 20;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 20;
	}

#ifdef LOFI_MODE
	// 8-bit conversion helper functions

// 8-bit audio functions for LoFi mode - pack into 16-bit storage
uint16_t packStereo(int16_t left, int16_t right)
{
	// convert two 12 bit signed values to signed 8 bit values and pack into a single 16 bit word
	int8_t left8 = static_cast<int8_t>(left >> 4);
	int8_t right8 = static_cast<int8_t>(right >> 4);
	return (static_cast<uint8_t>(left8) << 8) | static_cast<uint8_t>(right8);
}

int16_t unpackStereo(uint16_t stereo, int8_t index)
{
	// unpack a 16 bit word into two signed 8 bit values and convert to signed 12 bit values (returning one of them based on index)
	if (index == 0)
	{
		int8_t left8 = (stereo >> 8) & 0xFF;
		return static_cast<int16_t>(left8) << 4;
	}
	else
	{
		int8_t right8 = stereo & 0xFF;
		return static_cast<int16_t>(right8) << 4;
	}
}
#else
	// 12-bit audio functions for HiFi mode - pack into 32-bit storage
	uint32_t packStereo(int16_t left, int16_t right)
	{
		// Pack two 12-bit signed values into lower 24 bits of uint32_t
		uint32_t leftBits = (left + 2048) & 0xFFF;   // Convert to 12-bit unsigned
		uint32_t rightBits = (right + 2048) & 0xFFF; // Convert to 12-bit unsigned
		return (leftBits << 12) | rightBits;
	}

	int16_t unpackStereo(uint32_t stereo, int8_t index)
	{
		if (index == 0) {
			// Left channel: upper 12 bits
			uint32_t leftBits = (stereo >> 12) & 0xFFF;
			return (int16_t)(leftBits - 2048);  // Convert back to signed
		} else {
			// Right channel: lower 12 bits
			uint32_t rightBits = stereo & 0xFFF;
			return (int16_t)(rightBits - 2048);  // Convert back to signed
		}
	}
#endif

	int32_t cabs(int32_t a)
	{
		return (a > 0) ? a : -a;
	}

	// Clip audio sample to valid range (conditional based on audio mode)
	int16_t __not_in_flash_func(clipAudio)(int32_t sample)
	{
		if (sample > (AUDIO_RANGE - 1))
			sample = (AUDIO_RANGE - 1);
		if (sample < -AUDIO_RANGE)
			sample = -AUDIO_RANGE;
		return (int16_t)sample;
	}

	// Update cached knob values at 1000Hz
	void __not_in_flash_func(updateCachedKnobValues)()
	{
		cachedMainKnob_ = KnobVal(Main);
		cachedXKnob_ = KnobVal(X);
		cachedYKnob_ = KnobVal(Y);
	}
};
int main()
{
	set_sys_clock_khz(200000, true);
	Sheep card;
	card.EnableNormalisationProbe();
	card.Run();
}
