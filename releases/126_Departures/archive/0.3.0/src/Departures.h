// Copyright (c) 2026 Adrian Vos. SPDX-License-Identifier: MIT
// Original, AI-assisted prototype. No AMB-1 implementation is used here.
#pragma once
#include <cmath>
#include <cstdint>

namespace departures {
constexpr int kRate = 48000;
inline int Clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
inline int Mul(int a, int b) { return (a * b) >> 15; }
// Rounding toward zero makes quiet feedback eventually reach true silence.
inline int Gain(int a, int b) { return (a * b) / 32768; }
inline int Limit(int a) { return Clamp(a, -30000, 30000); }
inline int Approach(int a, int b, int shift) {
    const int d = b - a;
    return a + (d >= 0 ? (d + (1 << shift) - 1) >> shift : -((-d + (1 << shift) - 1) >> shift));
}

struct Tables {
    inline static int16_t sine[1025]{};
    inline static uint32_t pitch[128]{};
    static void Init() {
        // Floating point is used only before starting the audio interrupt.
        for (int i = 0; i <= 1024; ++i)
            sine[i] = static_cast<int16_t>(std::lround(32767.0 * std::sin(i * 6.283185307179586 / 1024.0)));
        for (int i = 0; i < 128; ++i)
            pitch[i] = static_cast<uint32_t>(std::lround(440.0 * std::exp2((i - 69) / 12.0) * (4294967296.0 / kRate)));
    }
};
inline int Sin(uint32_t phase) {
    const unsigned i = phase >> 22;
    return Tables::sine[i] + (((Tables::sine[i + 1] - Tables::sine[i]) * static_cast<int>((phase >> 6) & 65535)) >> 16);
}

struct Inputs {
    int main = 2048, colour = 1800, space = 2300;
    int transposeCV = 0, activityCV = 0;
    int sw = 1; // ComputerCard: Down=0, Middle=1, Up=2.
    bool clockConnected = false, chordConnected = false;
    bool clockRise = false, chordRise = false;
};
struct Stereo { int left, right; };

struct BellVoice {
    uint32_t phase = 0, modPhase = 0, inc = 0, modInc = 0;
    int env = 0, brightness = 0;
    int Process() {
        phase += inc;
        modPhase += modInc;
        const int mod = Sin(modPhase) * (brightness >> 8);
        const int out = Mul(Sin(phase + static_cast<uint32_t>(mod)), env >> 8);
        // Amplitude and brightness fade at different rates, like a struck bar.
        env -= (env + 32767) >> 15;
        brightness -= (brightness + 8191) >> 13;
        return out;
    }
};
struct Bell {
    BellVoice voice{}, tail{};
    int crossfade = 0;
    void Strike(int note, int colour, int identity, int level = 23000) {
        tail = voice;
        crossfade = 240; // Preserve the previous oscillator for a 5ms crossfade.
        voice = {};
        voice.inc = Tables::pitch[note];
        const int ratio = 512 + identity * 127;
        voice.modInc = static_cast<uint32_t>((static_cast<uint64_t>(voice.inc) * ratio) >> 8);
        // Eight fractional bits preserve long exponential tails at low levels.
        voice.env = Clamp(level, 0, 23000) * 256;
        voice.brightness = (1800 + colour * 2) * 256;
    }
    int Process() {
        const int now = voice.Process();
        if (!crossfade) return now;
        const int old = tail.Process();
        const int out = (now * (240 - crossfade) + old * crossfade) / 240;
        --crossfade;
        return out;
    }
};

// Small, dark stereo space: a two-head echo at 12kHz and a four-line
// feedback delay network at 24kHz. All storage is bounded and fixed-size.
class Space {
    static constexpr int kEcho = 24000;
    int16_t echo_[kEcho]{};
    int16_t lines_[4][2083]{};
    const int lengths_[4] = {1423, 1619, 1877, 2083};
    int positions_[4]{}, write_ = 0, division_ = 0;
    int damping_[4]{}, echoDamping_ = 0, sum_ = 0;
    int wetL_ = 0, wetR_ = 0, smoothL_ = 0, smoothR_ = 0;
    uint32_t drift_ = 0;
    int ReadEcho(int delayQ8) const {
        int p = write_ - (delayQ8 >> 8);
        if (p < 0) p += kEcho;
        int previous = p ? p - 1 : kEcho - 1;
        return echo_[p] + (((echo_[previous] - echo_[p]) * (delayQ8 & 255)) >> 8);
    }
public:
    Stereo Process(int input, int amount) {
        sum_ += input;
        ++division_;
        // Reverb on odd samples, echo on every fourth (even) sample: the
        // two expensive updates never land in the same audio interrupt.
        if ((division_ & 1) != 0) {
            // Orthogonal mixing spreads energy between all four lines.
            int s[4];
            for (int i = 0; i < 4; ++i) {
                damping_[i] = Approach(damping_[i], lines_[i][positions_[i]], 2);
                s[i] = damping_[i];
            }
            const int mixed[4] = {(s[0]+s[1]+s[2]+s[3])/2, (s[0]-s[1]+s[2]-s[3])/2,
                                  (s[0]+s[1]-s[2]-s[3])/2, (s[0]-s[1]-s[2]+s[3])/2};
            const int feedback = 23500 + amount; // <0.843, even fully clockwise.
            for (int i = 0; i < 4; ++i) {
                lines_[i][positions_[i]] = static_cast<int16_t>(Limit(input / 4 + Gain(mixed[i], feedback)));
                if (++positions_[i] == lengths_[i]) positions_[i] = 0;
            }
            wetL_ = (s[0] + s[2]) / 2;
            wetR_ = (s[1] - s[3]) / 2;
        }
        if (division_ == 4) {
            division_ = 0;
            drift_ += 25877; // One gentle tape cycle in about fourteen seconds.
            const int wobble = Sin(drift_);
            const int l = ReadEcho(19200 * 256 + wobble / 3);
            const int r = ReadEcho(14100 * 256 - wobble / 4);
            echoDamping_ = Approach(echoDamping_, (l + r) / 2, 2);
            echo_[write_] = static_cast<int16_t>(Limit(sum_ / 4 + Gain(echoDamping_, 20000 + amount / 2)));
            if (++write_ == kEcho) write_ = 0;
            sum_ = 0;
            echoL = l;
            echoR = r;
        }
        // Smooth the lower-rate effects back into the 48kHz output.
        smoothL_ = Approach(smoothL_, wetL_ + echoL / 2, 2);
        smoothR_ = Approach(smoothR_, wetR_ + echoR / 2, 2);
        return {smoothL_, smoothR_};
    }
    int echoL = 0, echoR = 0;
};

class Engine {
    struct Pad {
        uint32_t phase = 0, breath = 0, breathInc = 0, inc = 0;
        int note = 50, pending = 50, env = 0;
        int transition = 32768, level = 0;
        bool fadingOut = false;
    } pads_[3];
    struct Phrase {
        int length = 0, position = 0;
        uint8_t degree[37]{}, chance[37]{}, accent[37]{};
        int roundsUntilRest = 3;
        int octave = 0, roundsUntilAnswer = 2;
        bool resting = false;
    } phrases_[4];
    Bell bells_[4];
    Space space_;
    uint32_t random_ = 1, samples_ = 0, clockAge_ = 0;
    uint32_t phraseRandom_ = 1;
    int answeringLayer_ = -1;
    uint32_t internalAge_ = 0, bassPhase_ = 0, bassInc_ = 0, bassTarget_ = 0;
    int activity_ = 2048, colour_ = 1800, spaceAmount_ = 2300, transpose_ = 0;
    int density_ = 2048, livingColour_ = 1800, livingSpace_ = 2300;
    int tide_ = 0, tidePadGain_ = 28672, bellLevel_ = 21000;
    uint32_t tidePhase_[3]{};
    int restingLayer_ = -1, bassRoundsUntilRest_ = 5, bassRestRounds_ = 0;
    int chord_ = 0, chordBeats_ = 0, bassNote_ = 38, bassEnv_ = 0, bassLevel_ = 0;
    int gate_[2]{}, settle_[2]{}, sw_ = 1, controlPhase_ = 0;
    int dcL_ = 0, dcR_ = 0, fade_ = 0;
    bool clockConnected_ = false, chordConnected_ = false, beatPending_ = false, advancePending_ = false;
    bool melodyPending_[4]{};
    inline static constexpr int scale_[7] = {0, 2, 3, 5, 7, 9, 10}; // D Dorian.
    inline static constexpr int roots_[8] = {0, 2, 4, 1, 5, 3, 4, 0};
    uint32_t Random() {
        random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
        return random_;
    }
    uint32_t PhraseRandom() {
        // Expression uses its own random stream. Adding accents or octave
        // decisions cannot shuffle the established pad/tide/rest seed sequence.
        phraseRandom_ ^= phraseRandom_ << 13;
        phraseRandom_ ^= phraseRandom_ >> 17;
        phraseRandom_ ^= phraseRandom_ << 5;
        return phraseRandom_;
    }
    // All callers use scale degrees 0..11. Avoid software division inside
    // the chord-change interrupt by handling the single possible octave here.
    static int Degree(int degree) { return degree < 7 ? scale_[degree] : scale_[degree - 7] + 12; }
    static int PitchClass(int note) {
        // Pad/chord notes are 45..74; this also covers chord tones 50..65.
        return note >= 72 ? note - 72 : note >= 60 ? note - 60 : note >= 48 ? note - 48 : note - 36;
    }
    void AdvanceChord() {
        chord_ = (chord_ + 1) & 7;
        chordBeats_ = 0;
        ++chordChanges;
        const int chordNotes[3] = {50 + Degree(roots_[chord_]),
                                   50 + Degree(roots_[chord_] + 2),
                                   50 + Degree(roots_[chord_] + 4)};
        const int classes[3] = {PitchClass(chordNotes[0]), PitchClass(chordNotes[1]), PitchClass(chordNotes[2])};
        bool used[3]{}, held[3]{};
        // Keep every sounding note the next chord shares. In particular, do
        // not reset its oscillator, breath, or gain when the harmony changes.
        for (int i = 0; i < 3; ++i) {
            const int soundingClass = PitchClass(pads_[i].note);
            for (int n = 0; n < 3; ++n) {
                if (!used[n] && soundingClass == classes[n]) {
                    used[n] = held[i] = true;
                    pads_[i].pending = pads_[i].note;
                    pads_[i].fadingOut = false;
                    ++sharedNotes;
                    break;
                }
            }
        }
        // Assign unused chord tones to the remaining voices, choosing nearby
        // octaves. Their pitch changes only after a separate fade reaches zero.
        for (int i = 0; i < 3; ++i) {
            if (held[i]) continue;
            int chosen = -1, bestNote = 50, bestDistance = 100;
            for (int n = 0; n < 3; ++n) {
                if (used[n]) continue;
                int note = chordNotes[n];
                while (note - pads_[i].note > 6) note -= 12;
                while (pads_[i].note - note > 6) note += 12;
                if (note < 45) note += 12;
                if (note > 74) note -= 12;
                const int d = note > pads_[i].note ? note - pads_[i].note : pads_[i].note - note;
                if (d < bestDistance) { chosen = n; bestNote = note; bestDistance = d; }
            }
            used[chosen] = true;
            pads_[i].pending = bestNote;
            pads_[i].fadingOut = true;
        }
    }
    void BassNote() {
        bassNote_ = 38 + Degree(roots_[chord_]);
        bassTarget_ = Tables::pitch[Clamp(bassNote_ + transpose_, 24, 84)];
        bassEnv_ = 16000 * 256;
        bassPitch = bassNote_ + transpose_;
        gate_[1] = 528;
        ++bassEvents;
    }
    void Beat() {
        if (sw_ != 2 && ++chordBeats_ >= 24) AdvanceChord();
        for (bool &pending : melodyPending_) pending = true;
        if ((beats & 3) == 0) {
            // A bass rest skips two four-beat phrases. Existing sound decays;
            // no gate or new pitch is sent while resting. Only one layer may
            // rest at a time, so the pad and other phrases keep the piece alive.
            if (bassRestRounds_ && --bassRestRounds_ == 0) {
                restingLayer_ = -1;
                bassRoundsUntilRest_ = 4 + (Random() & 3);
            }
            if (!bassRestRounds_ && sw_ != 2 && bassRoundsUntilRest_ == 0 && restingLayer_ < 0) {
                bassRestRounds_ = 2;
                restingLayer_ = 4;
                ++bassRests;
            }
            if (!bassRestRounds_) {
                if (sw_ != 2 && bassRoundsUntilRest_ > 0) --bassRoundsUntilRest_;
                BassNote();
            }
        }
        ++beats;
    }
    void PhraseStep(int i) {
        auto &p = phrases_[i];
        const int at = p.position;
        if (at == 0) {
            if (p.resting) {
                p.resting = false;
                restingLayer_ = -1;
                p.roundsUntilRest = 3 + (Random() & 3);
            } else if (sw_ != 2) {
                if (p.roundsUntilRest == 0 && restingLayer_ < 0) {
                    p.resting = true;
                    restingLayer_ = i;
                    ++bellRests;
                } else if (p.roundsUntilRest > 0) --p.roundsUntilRest;
            }
            if (p.octave != 0) {
                // An answer lasts one complete round, then returns home,
                // even if Hold was selected after that answer began.
                p.octave = 0;
                answeringLayer_ = -1;
                p.roundsUntilAnswer = 3 + (PhraseRandom() & 3);
                ++octaveReturns;
            } else if (!p.resting && sw_ != 2) {
                if (p.roundsUntilAnswer == 0 && answeringLayer_ < 0) {
                    p.octave = (PhraseRandom() & 1) ? 12 : -12;
                    answeringLayer_ = i;
                    ++octaveAnswers;
                } else if (p.roundsUntilAnswer > 0) --p.roundsUntilAnswer;
            }
        }
        // Stored note choices return each loop. Only one choice occasionally
        // changes at a loop boundary, rather than rolling every note afresh.
        if (at == 0 && sw_ != 2 && (Random() & 4095) < static_cast<unsigned>(activity_ / 2)) {
            const int slot = Random() % p.length;
            const uint32_t choice = Random(); // Same draw count as 0.2.2.
            const int before = p.degree[slot];
            const int direction = before == 0 ? 1 : before == 6 ? -1 : (choice & 1) ? 1 : -1;
            p.degree[slot] = static_cast<uint8_t>(before + direction);
            ++phraseMutations;
        }
        if (!p.resting && p.chance[at] < 16 + density_ / 24) {
            const int degree = roots_[chord_] + p.degree[at];
            int note = 62 + Degree(degree) + transpose_ + p.octave;
            // Fold an out-of-range octave answer back by an octave, preserving
            // its scale pitch rather than clamping it to an unrelated note.
            if (note > 96) note -= 12;
            if (note < 36) note += 12;
            const int accent = p.accent[at];
            const int level = Mul(bellLevel_, (accent + 1) * 128);
            const int colour = Clamp(livingColour_ + (accent - 224) * 6, 0, 4095);
            bells_[i].Strike(note, colour, i, level);
            melodyPitch = note;
            // A low gap separates simultaneous or adjacent output events.
            gate_[0] = 528;
            ++melodyEvents;
            ++bellEvents[i];
        }
        if (++p.position == p.length) p.position = 0;
    }
    void Controls(const Inputs &in) {
        activity_ = Clamp(in.main + in.activityCV * 2, 0, 4095);
        colour_ = Clamp(in.colour, 0, 4095);
        spaceAmount_ = Clamp(in.space - 20, 0, 4075);
        // Input calibration is unavailable. Approx. 1V/oct, with 1/4-note
        // hysteresis around each semitone boundary to resist ADC jitter.
        const int raw = Clamp(in.transposeCV, -2048, 2047) * 9;
        if (raw > transpose_ * 256 + 192 || raw < transpose_ * 256 - 192)
            transpose_ = Clamp((raw + (raw >= 0 ? 128 : -128)) / 256, -12, 12);
        if (in.sw == 0 && sw_ != 0) AdvanceChord();
        sw_ = in.sw;
        const bool connected[2] = {in.clockConnected, in.chordConnected};
        for (int i = 0; i < 2; ++i)
            settle_[i] = connected[i] ? Clamp(settle_[i] + 1, 0, 20) : 0;
        if (clockConnected_ != in.clockConnected) { internalAge_ = 0; clockAge_ = 0; }
        clockConnected_ = in.clockConnected;
        chordConnected_ = in.chordConnected;
    }
    void PadControl(int i) {
        auto &p = pads_[i];
        p.breath += p.breathInc;
        const int wave = (32767 - Sin(p.breath + 0x40000000u)) / 2;
        p.env = Mul(wave, wave); // A soft start and a long crest.
        if (p.fadingOut) {
            p.transition = Clamp(p.transition - 128, 0, 32768); // 256ms out.
            if (p.transition == 0) { p.note = p.pending; p.fadingOut = false; }
        } else p.transition = Clamp(p.transition + 64, 0, 32768); // 512ms in.
        p.level = Mul(Mul(p.env, p.transition), tidePadGain_);
        p.inc = Tables::pitch[Clamp(p.note + transpose_, 24, 96)];
    }
    void TideControl() {
        // Independent slow cycles keep a recognisable rise and fall without
        // resetting with the clock, the chord, or the phrase loops.
        tidePhase_[0] += 60492; // about 71 seconds, updated at 1kHz.
        tidePhase_[1] += 33818; // about 127 seconds.
        tidePhase_[2] += 19260; // about 223 seconds.
        tide_ = Sin(tidePhase_[0]) / 2 + Sin(tidePhase_[1]) / 3 + Sin(tidePhase_[2]) / 6;
        density_ = Clamp(activity_ + Mul(tide_, 560), 0, 4095);
        livingColour_ = Clamp(colour_ + Mul(tide_, 500), 0, 4095);
        // Keep Y's dry endpoint exact, and feedback inside its original bounds.
        livingSpace_ = Clamp(spaceAmount_ - Mul(tide_, spaceAmount_ / 8), 0, 4075);
        tidePadGain_ = 28672 + Mul(tide_, 4096);
        bellLevel_ = 21000 + Mul(tide_, 2000);
    }
public:
    int melodyPitch = 62, bassPitch = 38;
    uint32_t beats = 0, melodyEvents = 0, bassEvents = 0, chordChanges = 0;
    uint32_t sharedNotes = 0, bellRests = 0, bassRests = 0;
    uint32_t octaveAnswers = 0, octaveReturns = 0, phraseMutations = 0;
    uint32_t bellEvents[4]{};
    void Init(uint32_t seed) {
        random_ = seed ? seed : 1;
        phraseRandom_ = seed ^ 0x9e3779b9u;
        if (!phraseRandom_) phraseRandom_ = 1;
        constexpr int lengths[4] = {11, 19, 29, 37};
        constexpr uint8_t motif[8] = {240, 208, 224, 192, 232, 200, 216, 196};
        for (int i = 0; i < 4; ++i) {
            phrases_[i].length = lengths[i];
            phrases_[i].roundsUntilRest = 3 + i;
            phrases_[i].roundsUntilAnswer = 2 + i;
            for (int n = 0; n < lengths[i]; ++n) {
                phrases_[i].degree[n] = Random() % 7;
                phrases_[i].chance[n] = Random() & 255;
                // A stored repeating emphasis pattern with a little seeded
                // variation. Its strongest note never exceeds 0.2.2's level.
                phrases_[i].accent[n] = motif[(n + i * 2) & 7] + (PhraseRandom() & 15);
            }
            // Each stream has a known opening, with the rest seeded afresh.
            phrases_[i].chance[i * 2] = 0;
            phrases_[i].accent[i * 2] = 255;
        }
        for (int i = 0; i < 3; ++i) {
            pads_[i].note = pads_[i].pending = 50 + Degree(i * 2);
            pads_[i].inc = Tables::pitch[pads_[i].note];
            pads_[i].breath = static_cast<uint32_t>(i) * 0x38000000u;
            pads_[i].breathInc = static_cast<uint32_t>(4294967296ull / (7000 + i * 4300));
            pads_[i].phase = Random();
        }
        bassInc_ = bassTarget_ = Tables::pitch[38];
        for (auto &phase : tidePhase_) phase = Random();
        BassNote();
    }
    Stereo Process(const Inputs &in, int audioL = 0, int audioR = 0) {
        ++samples_;
        // Let the panel and jack detector settle for 100ms on startup.
        if (samples_ <= 4800) return {0, 0};
        if (++controlPhase_ == 48) controlPhase_ = 0;
        // Stagger non-audio work over separate samples to bound interrupt time.
        if (controlPhase_ == 38) Controls(in);
        if (controlPhase_ == 46) TideControl();
        if (controlPhase_ >= 18 && controlPhase_ <= 34 && ((controlPhase_ - 18) & 7) == 0)
            PadControl((controlPhase_ - 18) >> 3);
        if (controlPhase_ >= 6 && controlPhase_ <= 30 && ((controlPhase_ - 6) & 7) == 0) {
            const int i = (controlPhase_ - 6) >> 3;
            if (melodyPending_[i]) { PhraseStep(i); melodyPending_[i] = false; }
        }
        if (in.clockRise && clockConnected_ && settle_[0] == 20 && clockAge_ >= 2400) {
            clockAge_ = 0;
            beatPending_ = true;
        }
        if (in.chordRise && chordConnected_ && settle_[1] == 20 && chordAge_ >= 2400) {
            chordAge_ = 0;
            advancePending_ = true;
        }
        if (clockAge_ < 480000) ++clockAge_;
        if (chordAge_ < 480000) ++chordAge_;
        // A patched but stopped clock stops new beats. Unplug to run freely.
        if (!clockConnected_ && ++internalAge_ >= static_cast<uint32_t>(80000 - activity_ * 11)) {
            internalAge_ = 0;
            beatPending_ = true;
        }
        if (beatPending_ && controlPhase_ == 2) { Beat(); beatPending_ = false; }
        if (advancePending_ && controlPhase_ == 42) { AdvanceChord(); advancePending_ = false; }

        int left = 0, right = 0;
        for (int i = 0; i < 3; ++i) {
            auto &p = pads_[i];
            p.phase += p.inc;
            int v = Sin(p.phase) + Mul(Sin(p.phase * 2), 1000 + livingColour_ * 2);
            v = Mul(v, p.level) / 7;
            left += Mul(v, 28000 - i * 6000);
            right += Mul(v, 16000 + i * 6000);
        }
        for (int i = 0; i < 4; ++i) {
            // 0.2.2: ease the bells back by 25% (-2.5dB) against pad/bass.
            // This also lowers their effects send without changing the other layers.
            const int v = bells_[i].Process() / 4;
            left += Mul(v, 28500 - i * 6500);
            right += Mul(v, 9000 + i * 6500);
        }
        bassInc_ = static_cast<uint32_t>(Approach(static_cast<int>(bassInc_), static_cast<int>(bassTarget_), 12));
        bassPhase_ += bassInc_;
        bassEnv_ -= (bassEnv_ + 131071) >> 17;
        bassLevel_ = Approach(bassLevel_, bassEnv_ >> 8, 7);
        const int bass = Mul(Sin(bassPhase_), bassLevel_) / 6;
        left += bass; right += bass;
        // Optional external audio goes to the effects, leaving the ensemble clear.
        // Audio inputs are in 12-bit DAC units, while voices/effects use a
        // wider internal range before the final /8 output scaling. Give the
        // wet-only external path enough send gain to remain useful: x8 here
        // is +12dB over the original x2 send. The existing limiter bounds hot
        // inputs and feedback, including two correlated full-scale channels.
        const int source = Limit((left + right) / 2 + (audioL + audioR) * 8);
        const Stereo wet = space_.Process(source, livingSpace_);
        left = left * (8192 - livingSpace_) / 8192 + wet.left * livingSpace_ / 4096;
        right = right * (8192 - livingSpace_) / 8192 + wet.right * livingSpace_ / 4096;
        // Remove accumulated DC before the 12-bit output, then fade in over about 1.37s.
        dcL_ = Approach(dcL_, left * 256, 12);
        dcR_ = Approach(dcR_, right * 256, 12);
        left -= dcL_ / 256; right -= dcR_ / 256;
        if (fade_ < 32768 && (samples_ & 1) == 0) ++fade_;
        left = Mul(left, fade_) / 8;
        right = Mul(right, fade_) / 8;
        for (int i = 0; i < 2; ++i) if (gate_[i]) --gate_[i];
        return {OutputLimit(left), OutputLimit(right)};
    }
    static int OutputLimit(int x) {
        const int a = x < 0 ? -x : x;
        const int b = a <= 1500 ? a : 1500 + (a - 1500) / 4;
        return x < 0 ? -Clamp(b, 0, 2047) : Clamp(b, 0, 2047);
    }
    // Give the analogue pitch output 1ms to settle before each 10ms trigger.
    bool Gate(int i) const { return gate_[i] > 0 && gate_[i] <= 480; }
    int Led(int i) const {
        if (i < 3) return pads_[i].level >> 3;
        if (i == 3) return gate_[0] ? 4095 : 0;
        if (i == 4) return sw_ == 2 ? 4095 : clockConnected_ ? 1200 : 0;
        return 0; // Hardware wrapper reserves LED5 for a timing warning.
    }
    int Chord() const { return chord_; }
    int Transpose() const { return transpose_; }
    int PadNote(int i) const { return pads_[i].note; }
    int PadPending(int i) const { return pads_[i].pending; }
    int PadTransition(int i) const { return pads_[i].transition; }
    uint32_t PadPhase(int i) const { return pads_[i].phase; }
    uint32_t PadBreath(int i) const { return pads_[i].breath; }
    int RestingLayer() const { return restingLayer_; }
    bool BellResting(int i) const { return phrases_[i].resting; }
    int Tide() const { return tide_; }
    int Density() const { return density_; }
    int LivingColour() const { return livingColour_; }
    int LivingSpace() const { return livingSpace_; }
    int PhraseLength(int i) const { return phrases_[i].length; }
    int PhraseDegree(int i, int step) const { return phrases_[i].degree[step]; }
    int PhraseAccent(int i, int step) const { return phrases_[i].accent[step]; }
    int PhraseOctave(int i) const { return phrases_[i].octave; }
private:
    uint32_t chordAge_ = 480000;
};
} // namespace departures
