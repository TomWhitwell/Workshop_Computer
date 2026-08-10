#include "Turing.h"

Turing::Turing(int length, uint32_t seed)
{
    _length = length;
    randomSeed(seed);
    _sequence = next() & 0xFFFF;

    // create default note pool when created
    UpdateNotePool(48, 3, 0);
}

// Call this each time the clock 'ticks'
// Pick a random number 0 to maxRange
// if the number is below the pot reading: bitRotateLflip, otherwise bitRotateL
// set _outputValue as
void Turing::Update(int pot, int maxRange)
{
    int safeZone = maxRange >> 5;
    int sample = safeZone + random(maxRange - (safeZone * 2)); // add safe zones at top and bottom

    if (_count == 0)
    {
        _startValue = _sequence;
    }

    if (++_count >= _length)
    {
        _count = 0;
    }

    if (sample >= pot)
    {
        _sequence = bitRotateLflip(_sequence, _length);
    }

    else
    {
        _sequence = bitRotateL(_sequence, _length);
    }

    if (_count++ == 0)
    {
        _startValue = _sequence;
    }
}

// returns the full current sequence value as 16 bit number 0 to 65535
uint16_t Turing::DAC_16()
{
    return _sequence;
}

// returns the current sequence value as 8 bit number 0 to 255 = ignores the last 8 binary digits
uint8_t Turing::DAC_8()
{

    //  return _sequence >> 8 ; // left hand 8 bits
    return _sequence & 0xFF; // right hand 8 bits
}

void Turing::updateLength(int newLen)
{
    _length = newLen;
}

uint16_t Turing::returnLength()
{
    return _length;
}

uint32_t Turing::next()
{
    constexpr uint32_t a = 1103515245u;
    constexpr uint32_t c = 12345u;
    _seed = a * _seed + c;
    return static_cast<uint32_t>(_seed >> 1); // 31-bit positive
}

void Turing::randomSeed(uint32_t seed)
{
    if (seed != 0)
        _seed = seed; // ignore zero (matches Arduino)
}

uint32_t Turing::random(uint32_t max) // [0, max)
{
    if (max <= 0)
        return 0;
    return next() % max;
}

void Turing::reset()
{
    _sequence = _startValue;
    _count = 0;
}

uint8_t Turing::MidiNote()
{
    if (note_pool_size == 0)
        return 0; // fallback, silence or base note

    uint8_t val = DAC_8();                   // 0–255 from looping 8-bit register
    int index = (val * note_pool_size) >> 8; // fast mapping
    if (index >= note_pool_size)
        index = note_pool_size - 1; // safety

    return note_pool[index];
}

void Turing::UpdateNotePool(int root_note, int octave_range, int scale_type)
{
    // Define the scale tables (can be moved to global/static later)
    static const int chromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const int major[] = {0, 2, 4, 5, 7, 9, 11};
    static const int minor[] = {0, 2, 3, 5, 7, 8, 10};
    static const int minor_pent[] = {0, 3, 5, 7, 10};
    static const int dorian[] = {0, 2, 3, 5, 7, 9, 10};
    static const int pelog[] = {0, 1, 3, 7, 10};
    static const int wholetone[] = {0, 2, 4, 6, 8, 10};

    static const int *const scale_tables[] = {
        chromatic, major, minor, minor_pent, dorian, pelog, wholetone};
    static const int scale_sizes[] = {
        12, 7, 7, 5, 7, 5, 6};

    // Bounds check
    if (scale_type < 0 || scale_type >= (int)(sizeof(scale_tables) / sizeof(scale_tables[0])))
    {
        scale_type = 0;
    }

    const int *scale = scale_tables[scale_type];
    int scale_size = scale_sizes[scale_type];

    note_pool_size = 0;

    for (int oct = 0; oct <= octave_range; ++oct)
    {
        int base = root_note + 12 * oct;
        for (int i = 0; i < scale_size; ++i)
        {
            int note = base + scale[i];
            if (note >= 0 && note < 128)
            {
                note_pool[note_pool_size++] = note;
                if (note_pool_size >= MAX_NOTES)
                    return;
            }
        }
    }
}
