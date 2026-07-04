//A stripped down version of the quantiser from Chris Johnson's Utility Pair project

#ifndef __QUANTISER_H__
#define __QUANTISER_H__

#define COMPUTERCARD_NOIMPL

int note_in = 69, pulseCounter = 0;
// Forced into RAM (not flash rodata) so the record/quantise hot path stays
// flash-safe while core 1 erases flash.
static uint8_t __not_in_flash("goldfish_tables") majorScale[12] = {0, 0, 2, 2, 4, 4, 5, 7, 7, 9, 9, 11};  // Octaves

int16_t __not_in_flash_func(quantSample)(int16_t input)
{
    int32_t note_in_cont = (input + 2048) << 8;
    if (note_in_cont < 0)
        note_in_cont = 0;
    int32_t note_in_up = (note_in_cont - 1000) / 7282;
    int32_t note_in_down = (note_in_cont + 1000) / 7282;

    if (note_in_up > note_in)
    {
        note_in = note_in_up;
    }
    else if (note_in_down < note_in)
    {
        note_in = note_in_down;
    };

    int16_t octave = note_in / 12;
    int16_t noteval = note_in % 12;

    int16_t quantisedNote = 12 * octave + majorScale[noteval];

    return quantisedNote;
}

#endif // quantiser_h