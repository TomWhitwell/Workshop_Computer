#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// MarkovMelody — 10-state melodic Markov chain, 5 profiles
// ---------------------------------------------------------------------------
namespace MarkovMelody {

static constexpr int NUM_STATES   = 10;
static constexpr int NUM_PROFILES = 5;

// profiles[profile][from_state][to_state]
// 10 states: 0-9 span root to octave over a diatonic range.
// Weights: higher = more probable. chaos=0 → pure weights.
static const uint8_t profiles[NUM_PROFILES][NUM_STATES][NUM_STATES] = {
    // 0: Pentatonic Stability
    // Root(0) and fifth(4) are strong attractors from any state.
    {
        { 20,  2,  3,  1, 18,  1,  1,  1,  1,  8 }, // from 0 (root)
        { 18,  2, 14,  1, 16,  1,  1,  1,  1,  2 }, // from 1 (2nd)
        { 14,  1,  5,  1, 18,  1,  1,  1,  1,  4 }, // from 2 (3rd)
        {  8,  1,  4,  2, 20,  3,  1,  1,  1,  2 }, // from 3 (4th)
        { 20,  1,  3,  1, 18,  1,  1,  4,  2,  8 }, // from 4 (5th)
        { 16,  1,  4,  1, 14,  2,  1,  2,  1,  5 }, // from 5 (6th)
        { 20,  1,  2,  1, 12,  1,  1,  2,  1,  1 }, // from 6 (7th)
        { 18,  2,  3,  1, 14,  1,  1,  4,  2,  2 }, // from 7
        { 16,  2,  3,  1, 12,  1,  1,  3,  2,  2 }, // from 8
        { 18,  3,  4,  1, 14,  1,  1,  4,  3,  5 }, // from 9 (oct)
    },
    // 1: Chromatic Tension
    // Stepwise motion dominates (±1 state). Snake-like line.
    {
        {  8, 20,  1,  1,  1,  1,  1,  1,  1,  4 }, // from 0
        { 20,  8, 20,  1,  1,  1,  1,  1,  1,  1 }, // from 1
        {  1, 20,  8, 20,  1,  1,  1,  1,  1,  1 }, // from 2
        {  1,  1, 20,  8, 20,  1,  1,  1,  1,  1 }, // from 3
        {  1,  1,  1, 20,  8, 20,  1,  1,  1,  1 }, // from 4
        {  1,  1,  1,  1, 20,  8, 20,  1,  1,  1 }, // from 5
        {  1,  1,  1,  1,  1, 20,  8, 20,  1,  1 }, // from 6
        {  1,  1,  1,  1,  1,  1, 20,  8, 20,  1 }, // from 7
        {  1,  1,  1,  1,  1,  1,  1, 20,  8, 20 }, // from 8
        {  4,  1,  1,  1,  1,  1,  1,  1, 20,  8 }, // from 9
    },
    // 2: Jazz Tendencies
    // Strong pull toward the 7th(6) from almost anywhere.
    {
        {  4,  1,  6,  1,  5,  1, 20,  3,  2,  3 }, // from 0
        {  4,  1, 12,  1,  4,  1, 18,  2,  2,  2 }, // from 1
        {  4,  1,  4,  1,  6,  1, 20,  3,  2,  2 }, // from 2
        {  2,  1,  4,  1,  3,  1, 22,  3,  2,  2 }, // from 3
        { 10,  1,  4,  1,  4,  1, 14, 10,  4,  4 }, // from 4
        {  4,  1,  8,  1,  4,  1, 18,  4,  3,  3 }, // from 5
        { 22,  1,  4,  3,  5,  1,  4,  3,  2,  2 }, // from 6 — resolves to root
        { 14,  1,  4,  1,  8,  1, 10,  4,  4,  4 }, // from 7
        { 10,  1,  4,  1,  6,  1, 14,  5,  3,  4 }, // from 8
        { 12,  1,  4,  1,  8,  1, 12,  4,  4,  4 }, // from 9
    },
    // 3: Glacial
    // Stays on current note. Only adjacent steps are possible.
    {
        { 20, 10,  1,  1, 10,  1,  1,  2,  1,  1 }, // from 0 (root)
        { 14, 18, 10,  1,  4,  1,  1,  1,  1,  1 }, // from 1
        {  8, 10, 18, 10,  4,  1,  1,  1,  1,  1 }, // from 2
        {  6,  1, 10, 18, 10,  1,  1,  1,  1,  1 }, // from 3
        { 12,  1,  1, 10, 20,  8,  1,  1,  1,  1 }, // from 4 (fifth)
        {  6,  1,  1,  1,  8, 18, 10,  1,  1,  1 }, // from 5
        { 14,  1,  1,  1,  4,  8, 16,  5,  1,  1 }, // from 6
        { 10,  1,  1,  1,  6,  1,  6, 18, 10,  1 }, // from 7
        {  8,  1,  1,  1,  4,  1,  1,  8, 18, 10 }, // from 8
        { 10,  1,  1,  1,  4,  1,  1,  4,  8, 18 }, // from 9 (oct)
    },
    // 4: Drone
    // Overwhelming self-loop weight. Note barely ever changes.
    {
        { 40,  2,  1,  1,  1,  1,  1,  1,  1,  1 }, // from 0 (root)
        {  2, 40,  2,  1,  1,  1,  1,  1,  1,  1 }, // from 1
        {  1,  2, 40,  2,  1,  1,  1,  1,  1,  1 }, // from 2
        {  1,  1,  2, 40,  2,  1,  1,  1,  1,  1 }, // from 3
        {  1,  1,  1,  2, 40,  2,  1,  1,  1,  1 }, // from 4
        {  1,  1,  1,  1,  2, 40,  2,  1,  1,  1 }, // from 5
        {  1,  1,  1,  1,  1,  2, 40,  2,  1,  1 }, // from 6
        {  1,  1,  1,  1,  1,  1,  2, 40,  2,  1 }, // from 7
        {  1,  1,  1,  1,  1,  1,  1,  2, 40,  2 }, // from 8
        {  1,  1,  1,  1,  1,  1,  1,  1,  2, 40 }, // from 9 (oct)
    },
};

} // namespace MarkovMelody


// ---------------------------------------------------------------------------
// MarkovPercData — 8-state rhythmic Markov chain, 4 profiles
// ---------------------------------------------------------------------------
namespace MarkovPercData {

static constexpr int NUM_STATES   = 8;
static constexpr int NUM_PROFILES = 4;

// Hit state indices
static constexpr uint8_t STATE_REST      = 0;
static constexpr uint8_t STATE_HIT       = 1;
static constexpr uint8_t STATE_ACC_HIT   = 2;
static constexpr uint8_t STATE_FLAM      = 3;
static constexpr uint8_t STATE_ACC_FLAM  = 4;
static constexpr uint8_t STATE_RATCHET_2 = 5;
static constexpr uint8_t STATE_RATCHET_3 = 6;
static constexpr uint8_t STATE_RATCHET_4 = 7;

// Accent levels (0-5)
static constexpr uint8_t ACCENT_NONE  = 0;
static constexpr uint8_t ACCENT_GRACE = 1;
static constexpr uint8_t ACCENT_SOFT  = 2;
static constexpr uint8_t ACCENT_MED   = 3;
static constexpr uint8_t ACCENT_HARD  = 4;
static constexpr uint8_t ACCENT_FULL  = 5;

// profiles[profile][from_state][to_state]
//          R    H   AH    F   AF   R2   R3   R4
static const uint8_t profiles[NUM_PROFILES][NUM_STATES][NUM_STATES] = {
    // 0: Steady (Rock/Pop)
    // Gravitates toward hits and accented hits. Rests are brief.
    {
        {  2, 18, 12,  4,  2,  3,  1,  1 }, // from REST
        {  4, 16, 10,  6,  2,  4,  1,  1 }, // from HIT
        {  4, 14, 10,  4,  4,  4,  2,  1 }, // from ACC_HIT
        {  4, 16, 10,  4,  2,  5,  1,  1 }, // from FLAM
        {  6, 14, 10,  4,  2,  4,  2,  1 }, // from ACC_FLAM
        {  8, 14,  8,  4,  2,  4,  1,  1 }, // from RATCHET_2
        { 12, 12,  6,  2,  2,  4,  2,  2 }, // from RATCHET_3
        { 14, 12,  6,  2,  2,  2,  2,  2 }, // from RATCHET_4
    },
    // 1: Syncopated (Funk/Latin)
    // Rests are structurally meaningful. Flams very common.
    {
        {  4, 10,  6, 12,  8, 10,  2,  1 }, // from REST
        { 10,  6,  4, 14,  8, 10,  4,  2 }, // from HIT
        {  8,  8,  4, 12, 10,  8,  4,  2 }, // from ACC_HIT
        {  8, 10,  4,  8,  8, 12,  4,  2 }, // from FLAM
        {  8,  8,  6, 10,  8, 10,  4,  2 }, // from ACC_FLAM
        { 12,  8,  4,  8,  6,  8,  4,  2 }, // from RATCHET_2
        { 14,  8,  4,  6,  4,  6,  4,  2 }, // from RATCHET_3
        { 16,  6,  4,  4,  4,  6,  4,  2 }, // from RATCHET_4
    },
    // 2: Jazz/Free
    // Complex patterns, ratchets common. Accented flams are signature gesture.
    {
        {  4,  6,  4,  8, 14, 12,  8,  6 }, // from REST
        {  6,  4,  4,  8, 12, 14, 10,  6 }, // from HIT
        {  4,  4,  4,  8, 14, 12, 10,  6 }, // from ACC_HIT
        {  6,  6,  4,  6, 14, 12, 10,  6 }, // from FLAM
        {  4,  4,  4,  6, 12, 14, 12,  8 }, // from ACC_FLAM
        {  8,  6,  4,  8, 10, 10, 12,  8 }, // from RATCHET_2
        { 10,  4,  4,  6,  8,  8, 14, 10 }, // from RATCHET_3
        { 12,  4,  4,  4,  6,  6, 12, 14 }, // from RATCHET_4
    },
    // 3: Sparse
    // Long silences punctuated by single hits.
    {
        { 12, 10,  2,  1,  1,  1,  1,  1 }, // from REST
        { 18,  6,  2,  1,  1,  1,  1,  1 }, // from HIT
        { 16,  6,  4,  1,  1,  1,  1,  1 }, // from ACC_HIT
        { 18,  4,  2,  2,  1,  1,  1,  1 }, // from FLAM
        { 18,  4,  2,  1,  2,  1,  1,  1 }, // from ACC_FLAM
        { 20,  4,  2,  1,  1,  2,  1,  1 }, // from RATCHET_2
        { 20,  4,  2,  1,  1,  1,  1,  1 }, // from RATCHET_3
        { 20,  4,  2,  1,  1,  1,  1,  1 }, // from RATCHET_4
    },
};

} // namespace MarkovPercData
