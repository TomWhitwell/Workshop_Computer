#pragma once
#include <stdint.h>

class Turing

{

#define bitRotateL(value, len) ((((value) >> ((len) - 1)) & 0x01) | ((value) << 1))
#define bitRotateLflip(value, len) (((~((value) >> ((len) - 1)) & 0x01) | ((value) << 1)))

public:
    Turing(int length, uint32_t seed);
    void Update(int pot, int maxRange);
    void updateLength(int newLen);
    uint16_t returnLength();
    uint16_t DAC_16();
    uint8_t DAC_8();
    void DAC_print8();
    void DAC_print16();
    void randomSeed(uint32_t seed);
    // uint8_t MidiNote(int low_note, int high_note, int scale_type, int sieve_type);
    void UpdateNotePool(int root_note, int octave_range, int scale_type);
    uint8_t MidiNote();
    void reset(); // Experimental - resets turing to the value at start of the current cycle

private:
    uint16_t _sequence = 0; // randomise on initialisation
    int _length = 16;
    inline static uint32_t _seed = 1;
    uint32_t next();
    uint32_t random(uint32_t max);

    static constexpr uint8_t MAX_NOTES = 128;
    uint8_t note_pool[MAX_NOTES];
    int note_pool_size = 0;

    // Experimental: Reset system
    uint16_t _startValue = 0;
    uint8_t _count = 0;
};
