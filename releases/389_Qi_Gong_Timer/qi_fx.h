// =============================================================================
//  qi_fx.h : the effects for the QiGong Timer (stage 2).  No hardware dependency.
//  AI-assisted (Claude / Anthropic).  The reverb is adapted from the Story OST card's FDN
//  (4 lines, Hadamard mixing, damping, wandering line lengths, input diffusers, half rate),
//  with overflow-safe multiplies and a gain compensation suited to very long tails.
//
//  One FxChannel per audio channel.  Signal flow per channel (at 24 kHz):
//      input --+--> delay (darkening feedback loop) --> echoes --+--> echo return --+
//              |                                                 |                 |
//              +----------------> reverb input <-----------------+   reverb return-+--> wet
//  The engine adds the wet signal to the untouched dry input, then a soft limiter above ~78% of full scale.
//
//  Fixed point: inside the effects, audio is "Q6" = 12-bit value x 64 (kFxQ), so quiet tails keep their precision
//  (with only x16, rounding on every pass shortened long tails audibly).
//  The delay memory is 16-bit "Q3" = 12-bit x 8.  Every multiply inside a feedback loop rounds TOWARD ZERO and
//  tiny values are gated to zero, so every tail dies to exact digital silence (no faint hiss forever).
// =============================================================================
#pragma once
#include <stdint.h>
#include <math.h>
#include "cardkit_dsp.h"
#include "qi_config.h"

namespace qi {
using namespace cardkit;

static const int kFxQ = 6;               // internal audio = 12-bit value << kFxQ

// ---- memory (global arrays: big buffers must not live on the stack) ----
static int32_t gFdn[2][4][kFdnSize];     // 52 KB
static int32_t gAp[2][2][261];           // 4 KB
static int16_t gDel[2][kDelLen];         // 94 KB

// ---- overflow-safe multiplies ----
// These run many times per sample, so they are written without branches (same results as the obvious versions:
// s = 0 or -1 is the sign; (x ^ s) - s is |x| or flips it back).  Branches cost extra cycles on the M0+.
// (a * c) >> 15 rounded toward zero; safe for |a| < 2^30 and 0 <= c <= 32767 (a plain int32 product overflows above |a| ~ 65,000)
static inline int32_t MulTZ15S(int32_t a, int32_t c)
{
	int32_t s = a >> 31, m = (a ^ s) - s;
	int32_t r = (m >> 15) * c + (((m & 32767) * c) >> 15);
	return (r ^ s) - s;
}
// (a * g) >> 12 rounded toward zero; safe for |a| < 2^26 and 0 <= g <= 16383
static inline int32_t MulQ12S(int32_t a, int32_t g)
{
	int32_t s = a >> 31, m = (a ^ s) - s;
	int32_t r = (m >> 12) * g + (((m & 4095) * g) >> 12);
	return (r ^ s) - s;
}
// x / 2 rounded toward zero, without a branch (same result as cardkit's Half)
static inline int32_t HalfTZ(int32_t x) { return (x + (int32_t)((uint32_t)x >> 31)) >> 1; }
// x >> 4 rounded toward zero, without a branch
static inline int32_t Shr4TZ(int32_t x) { return (x + ((x >> 31) & 15)) >> 4; }

// Soft knee: |w| passes untouched up to half of `room`, then bends smoothly toward `room` (never reaches past it).
// The division only happens above the knee (the RP2040 has a hardware divider).
static inline int32_t SoftRoom(int32_t w, int32_t room)
{
	int32_t m = w < 0 ? -w : w, h = room >> 1;
	if (m <= h) return w;
	int32_t r = room - h, e = m - h;
	int32_t y = h + (e * r) / (e + r);                            // callers keep e*r inside int32 (see AddWet and the delay)
	return w < 0 ? -y : y;
}
// Output = dry + wet.  The dry signal is never changed; the wet part is limited to the headroom the dry signal leaves.
static inline int32_t AddWet(int32_t dry, int32_t wet)
{
	wet = Clamp(wet, -100000, 100000);                            // (keeps SoftRoom's product inside int32)
	int32_t room = wet >= 0 ? 2047 - dry : 2047 + dry;            // room left in the direction the wet signal pushes
	return dry + SoftRoom(wet, room < 0 ? 0 : room);
}

// One-pole low-pass state step that always lands EXACTLY on its target (from either side): the step is d >> shift
// rounded toward zero, but never smaller than 1 while d != 0.  (Plain >> rounds negative steps the wrong way and
// a filter can then sit one unit away forever, feeding a tiny constant into a feedback loop.)
static inline int32_t SettleStep(int32_t d, int shift)
{
	int32_t s = d >= 0 ? d >> shift : -((-d) >> shift);
	if (s == 0 && d != 0) s = d > 0 ? 1 : -1;
	return s;
}

// ---- knob -> parameter tables (filled once at start-up: the only place floats are used) ----
static int32_t gRevFbTab[33], gRevFbCapTab[33], gRevInTab[33], gDampTab[33], gDampCompTab[33], gDelTimeTab[33];
static void InitFxTables()
{
	float lavg = (float)(kFdnLen[0] + kFdnLen[1] + kFdnLen[2] + kFdnLen[3]) / 4.0f / 24000.0f;   // average loop time (s)
	float gmin = powf(10.0f, -3.0f * lavg / kRevTimeMinS);
	float refIn = sqrtf(1.0f - gmin * gmin);
	for (int i = 0; i <= 32; ++i)
	{
		float t = (float)i / 32.0f;
		float rt60 = kRevTimeMinS * powf(kRevTimeMaxS / kRevTimeMinS, t);
		float g = powf(10.0f, -3.0f * lavg / rt60);                      // loop gain for that decay time
		gRevFbTab[i] = (int32_t)lrintf(g * 32768.0f);
		gRevFbCapTab[i] = (int32_t)lrintf(powf(g, 0.75f) * 32768.0f); // cap: deep bass never rings more than 1.33x the setting
		gRevInTab[i] = (int32_t)lrintf(32767.0f * powf((1.0f - g * g) / (refIn * refIn), kRevCompExp));   // longer tails: less input
		float fc = kDarkBrightHz * powf(kDarkDarkHz / kDarkBrightHz, t);
		float c = 1.0f - expf(-6.2831853f * fc / 24000.0f);
		gDampTab[i] = (int32_t)lrintf(32767.0f * c);
		// the darkening filter also loses a little at low-mid frequencies on every pass; make that up at kRevRefHz
		float w = 6.2831853f * kRevRefHz / 24000.0f, a = 1.0f - c;
		float mag = c / sqrtf(1.0f - 2.0f * a * cosf(w) + a * a);
		gDampCompTab[i] = (int32_t)lrintf(16384.0f / mag);             // Q14, >= 16384
		float ms = kDelMinMs * powf(kDelMaxMs / kDelMinMs, t);
		gDelTimeTab[i] = (int32_t)lrintf(ms * 24.0f * 256.0f);            // samples at 24 kHz, Q8
	}
}
static inline int32_t TabLookup(const int32_t* tab, int32_t k)   // k = 0..4095, linear interpolation
{
	k = Clamp(k, 0, 4095);
	int32_t i = k >> 7, f = k & 127;
	return tab[i] + (((tab[i + 1] - tab[i]) * f) >> 7);
}
static inline int32_t MixGain(int32_t k, int32_t returnQ12)      // squared curve, exactly 0 at the bottom
{
	if (k < kMixDead) return 0;
	int32_t sq = (k * k) >> 12;                                    // 0..4094
	return (sq * returnQ12) >> 12;
}

// ---- the parameters one channel needs (the engine smooths them) ----
struct FxParams
{
	int32_t revFb = 0, revIn = 32767, revDamp = 32767;   // Q15
	int32_t revGain = 0, delGain = 0;                    // Q12
	int32_t delFb = 0;                                   // Q15
	int32_t delTimeQ8 = 0;                               // samples at 24 kHz, Q8
};
// Knob values (0..4095) -> target parameters
static inline FxParams FxTargets(int32_t revTime, int32_t revDark, int32_t revMix, int32_t delMix, int32_t delTime, int32_t delFb)
{
	FxParams p;
	p.revFb   = Clamp((TabLookup(gRevFbTab, revTime) * TabLookup(gDampCompTab, revDark)) >> 14, 0, TabLookup(gRevFbCapTab, revTime));
	if (p.revFb > 32700) p.revFb = 32700;                              // absolute ceiling (loop gain 0.998)
	p.revIn   = TabLookup(gRevInTab, revTime);
	p.revDamp = TabLookup(gDampTab, revDark);
	p.revGain = MixGain(revMix, kRevReturnQ12);
	p.delGain = MixGain(delMix, kDelReturnQ12);
	p.delFb   = (Clamp(delFb, 0, 4095) * kDelFbMaxQ15) >> 12;
	p.delTimeQ8 = TabLookup(gDelTimeTab, delTime);
	return p;
}

// ---- soft pickup (from Story OST): the stored value holds until the knob passes through it ----
struct Pickup
{
	int32_t value = 0, prev = 0;
	bool engaged = false;
	void Update(int32_t k)
	{
		if (!engaged)
		{
			int32_t d = k - value;
			if (Abs(d) < 64 || (prev - value) * d <= 0) engaged = true;
		}
		if (engaged) value = k;
		prev = k;
	}
	void Arm(int32_t k) { engaged = false; prev = k; }
};

// ---- one effects channel ----
class FxChannel
{
public:
	void Init(int ch, const FxParams& p)
	{
		ch_ = ch;
		keeperOn_ = kKeeperOn;
		gain_ = kKeeperOn ? 0 : 32768;
		floor_ = kKeeperFloorInit << 8;
		for (int i = 0; i < 4; ++i)
		{
			fdnDQ8_[i] = (kFdnLen[i] - 8) << 8;
			ph_[i] = (uint32_t)(ch * 4 + i) * 0x2F000000u;              // each line (and channel) starts its wander at a different point
		}
		delDQ8_ = p.delTimeQ8;
	}

	// Once per control tick (1.5 kHz): let the reverb lines wander slowly.
	void Wander()
	{
		for (int i = 0; i < 4; ++i)
		{
			ph_[i] += CycleInc(kFdnWanderSec[i]);
			fdnDQ8_[i] = ((kFdnLen[i] - 8) << 8) + ((SineLookup(ph_[i]) * kFdnWanderQ8) >> 11);
		}
	}

	// The card has just fired this channel's pulse: the sound has not started yet, so open the keeper fully AT ONCE (no fade-in:
	// a fade would soften the chime's attack; only the faint leak is present at this instant, so the jump is inaudible).
	// This is the ONLY thing that opens the keeper (see kKeeperFloorMax in qi_config.h).
	void Alert() { alert_ = kKeeperAlertHold; open_ = true; hold_ = kKeeperHold; openTicks_ = 0; if (keeperOn_) gain_ = 32768; }

	// Once per control tick (1.5 kHz): learn the leak level ("floor").  While closed it simply follows the input's level (the
	// leak), up to kKeeperFloorMax: it cannot open the keeper anyway.  While open (a chime sounding) it can only come down.
	void KeeperLearn()
	{
		if (!open_ || env_ < floor_) floor_ += SettleStep(env_ - floor_, 4);
		if (floor_ > (kKeeperFloorMax << 8)) floor_ = kKeeperFloorMax << 8;
	}
	// The keeper's gain (Q15, 32768 = exactly unity): the engine applies it to the dry signal too if kKeeperOnDry.
	int32_t KeeperGain() const { return gain_; }
	// Dry gating, once per 48 kHz sample.  Unity is exact, so sounds pass bit-for-bit; the pulse opens it before they start.
	inline int32_t GateDry(int32_t x)
	{
		if (!keeperOn_ || gain_ >= 32768) return x;
		return MulTZ15S(x, gain_);
	}
#ifdef QI_HOST_TEST
	void SetKeeper(bool on) { keeperOn_ = on; if (!on) gain_ = 32768; }
	int32_t KeeperFloor() const { return floor_ >> 8; }
	bool KeeperOpen() const { return open_; }
#endif

	// Once per 48 kHz sample.  x = the dry input (12-bit).  tick = true on this channel's half-rate turn.
	// Returns the wet signal in Q6 (12-bit x 64).
	inline int32_t Process(int32_t x, bool tick, const FxParams& p)
	{
		acc_ += x;
		if (tick) { Tick(acc_, p); acc_ = 0; return wetPrev_; }
		return HalfTZ(wetPrev_ + wetCur_);                              // halfway between the last two 24 kHz results
	}

#ifdef QI_HOST_TEST
	int32_t peak_ = 0, clampHits_ = 0, kneeHits_ = 0;                     // test-only meters: reverb peak / reverb clamps / delay knee
	int32_t dcPeak_ = 0;                                                   // test-only: largest correction the wet DC blocker applied (Q6)
#endif

private:
	inline void Tick(int32_t acc, const FxParams& p)                  // acc = sum of the last two input samples
	{
		// ---- high-pass on the effect send (two stages at ~30 Hz; the dry path is untouched) ----
		// The Computer's inputs pass DC, and a long reverb sustains very low frequencies: an offset or a sudden step in
		// the patch would otherwise fill the reverb with a slow swell (found by the fuzz and DC tests).  Bells have
		// nothing down there.  The states keep 8 extra bits and settle exactly, so silence stays silence.
		int32_t x6 = acc * (1 << (kFxQ - 1));                            // input in Q6
		hpA_ += SettleStep(x6 * 256 - hpA_, kSendHpShift);
		int32_t h1 = x6 - (hpA_ >> 8);
		hpB_ += SettleStep(h1 * 256 - hpB_, kSendHpShift);
		int32_t hp = h1 - (hpB_ >> 8);
		if (keeperOn_) hp = Keeper(hp);

		// ---- delay (16-bit memory, Q3) ----
		delDQ8_ += (p.delTimeQ8 - delDQ8_) >> kDelGlideShift;          // echo-time changes glide
		int32_t d = delDQ8_ >> 8, fr = delDQ8_ & 255;
		int32_t i0 = delW_ - d;  if (i0 < 0) i0 += kDelLen;
		int32_t i1 = i0 - 1;     if (i1 < 0) i1 += kDelLen;
		int16_t* const del = gDel[ch_];
		int32_t a = del[i0], b = del[i1];
		int32_t y = a + (((b - a) * fr) >> 8);                           // the echo (Q3)
		delLp_ += MulTZ15S(y - delLp_, kDelDampQ15);                     // repeats get darker
		int32_t w = (hp >> (kFxQ - 3)) + MulTZ15S(delLp_, p.delFb);      // input (Q3) + feedback
		if (Abs(w) < 2) w = 0;                                           // gate: the repeats die to true silence
		if (w > kDelKnee || w < -kDelKnee) { w = SoftRoom(w, 2 * kDelKnee); HostKnee(); }    // soft saturation, never a hard clip
		del[delW_] = (int16_t)w;
		if (++delW_ >= kDelLen) delW_ = 0;
		int32_t echo = y * (1 << (kFxQ - 3));                            // Q3 -> Q6

		// ---- reverb (Q6) ----
		int32_t in = MulTZ15S(hp + MulQ12S(echo, p.delGain), p.revIn);  // the input AND the echoes, level-compensated
		in = Allpass(0, in);
		in = Allpass(1, in);
		int32_t (*const lines)[kFdnSize] = gFdn[ch_];
		const int32_t w0 = fdnW_;
		int32_t dl[4];
		for (int i = 0; i < 4; ++i) dl[i] = FdnRead(lines[i], w0, fdnDQ8_[i]);
		int32_t s01 = dl[0] + dl[1], d01 = dl[0] - dl[1], s23 = dl[2] + dl[3], d23 = dl[2] - dl[3];
		int32_t mix[4] = {HalfTZ(s01 + s23), HalfTZ(d01 + d23), HalfTZ(s01 - s23), HalfTZ(d01 - d23)};   // Hadamard mixing
		const int32_t damp = p.revDamp, fb = p.revFb;
		for (int i = 0; i < 4; ++i)
		{
			// darker with every pass.  lp_ keeps 4 extra bits: without them a dark (small) coefficient has a "dead band" where
			// rounding stops the filter moving, and a leftover value circulated forever (found by the darkest-setting test).
			int32_t lp = lp_[i] + MulTZ15S(mix[i] * 16 - lp_[i], damp);
			lp_[i] = lp;
			int32_t v = in + MulTZ15S(Shr4TZ(lp), fb);
			if ((uint32_t)(v + 2) < 5u) v = 0;                           // the tiny gate (|v| < 3): tails always reach exact silence
			if ((uint32_t)(v + kFdnClamp) > (uint32_t)(2 * kFdnClamp)) { v = Clamp(v, -kFdnClamp, kFdnClamp); HostClamp(); }
#ifdef QI_HOST_TEST
			if (Abs(v) > peak_) peak_ = Abs(v);
#endif
			lines[i][w0] = v;
		}
		fdnW_ = (w0 + 1 >= kFdnSize) ? 0 : w0 + 1;
		int32_t rev = HalfTZ(d01 + d23);                                 // (= dl0 - dl1 + dl2 - dl3, halved)

		int32_t wet = MulQ12S(rev, p.revGain) + MulQ12S(echo, p.delGain);
		wetDc_ += SettleStep(wet * 16 - wetDc_, kWetDcShift);            // backstop: no DC ever leaves the effects (~5 Hz)
		wet -= Shr4TZ(wetDc_);
#ifdef QI_HOST_TEST
		if (Abs(Shr4TZ(wetDc_)) > dcPeak_) dcPeak_ = Abs(Shr4TZ(wetDc_));
#endif
		wetPrev_ = wetCur_;
		wetCur_ = wet;
	}

	// ---- the silence keeper (one call per 24 kHz tick): returns the send, gated ----
	inline int32_t Keeper(int32_t hp)
	{
		int32_t a = Abs(hp) * (1 << (8 - kFxQ));                         // |input| in 12-bit units, Q8
		env_ += SettleStep(a - env_, a > env_ ? kKeeperRise : kKeeperFall);
		int32_t closeTh = floor_ + (floor_ >> 1) + (kKeeperCloseAdd << 8);
		if (alert_ > 0) { --alert_; open_ = true; hold_ = kKeeperHold; }   // our own pulse: the sound is on its way
		else if (open_)
		{
			if (env_ < closeTh) { if (hold_ > 0) --hold_; else open_ = false; } else hold_ = kKeeperHold;
			if (++openTicks_ > kKeeperMaxOpen) open_ = false;   // (never longer than 20 s after a pulse)
		}
		// (nothing else opens it: only the card's own pulses)
		if (open_) gain_ = 32768;
		else { gain_ -= (gain_ >> kKeeperFadeOut) + 1; if (gain_ < kKeeperSnap) gain_ = 0; }   // (+1: an exponential fade must not stall)
		return gain_ >= 32768 ? hp : MulTZ15S(hp, gain_);
	}

	inline int32_t Allpass(int i, int32_t x)
	{
		int idx = apIdx_[i];
		int32_t* const buf = gAp[ch_][i];
		int32_t b = buf[idx];
		buf[idx] = x + HalfTZ(b);
		if (++idx >= kApLen[i]) idx = 0;
		apIdx_[i] = idx;
		return b - x;
	}
	static inline int32_t FdnRead(const int32_t* line, int32_t w, int32_t dQ8)   // a line at its wandering length (interpolated)
	{
		int32_t d = dQ8 >> 8, fr = dQ8 & 255;
		int32_t i0 = w - d;  if (i0 < 0) i0 += kFdnSize;
		int32_t i1 = i0 - 1; if (i1 < 0) i1 += kFdnSize;
		int32_t a = line[i0];
		return a + MulTZ8(line[i1] - a, fr);
	}
	static inline int32_t MulTZ8(int32_t a, int32_t f)                // (a * f) >> 8 toward zero, f = 0..255; safe for any |a| < 2^30
	{
		int32_t s = a >> 31, m = (a ^ s) - s;
		int32_t r = (m >> 8) * f + (((m & 255) * f) >> 8);
		return (r ^ s) - s;
	}
	inline void HostClamp()
	{
#ifdef QI_HOST_TEST
		++clampHits_;
#endif
	}
	inline void HostKnee()
	{
#ifdef QI_HOST_TEST
		++kneeHits_;
#endif
	}

	int ch_ = 0;
	int32_t acc_ = 0, wetPrev_ = 0, wetCur_ = 0, hpA_ = 0, hpB_ = 0, wetDc_ = 0;
	bool keeperOn_ = true, open_ = false;
	int32_t env_ = 0, floor_ = 0, gain_ = 0, hold_ = 0, alert_ = 0, openTicks_ = 0;
	int32_t delW_ = 0, delDQ8_ = 0, delLp_ = 0;
	int32_t fdnW_ = 0, fdnDQ8_[4] = {0, 0, 0, 0}, lp_[4] = {0, 0, 0, 0};
	int apIdx_[2] = {0, 0};
	uint32_t ph_[4] = {0, 0, 0, 0};
};

}  // namespace qi
