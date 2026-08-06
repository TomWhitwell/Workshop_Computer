#pragma once

#include <cstdint>
#include <cstddef>

class Config
{
public:
    static const uint32_t MAGIC;
    static const size_t FLASH_SIZE;
    static const size_t BLOCK_SIZE;
    static const size_t OFFSET;

    struct Preset
    {
        uint8_t scale;
        uint8_t range;
        uint8_t length;
        uint8_t looplen;
        uint8_t pulseMode1;
        uint8_t pulseMode2;
        uint8_t cvRange;
    };

    struct Vactrol
    {
        uint8_t law = 0;
        uint8_t relation = 0;
        uint8_t rise = 48;
        uint8_t fall = 56;
        uint8_t min1 = 0;
        uint8_t max1 = 255;
        uint8_t min2 = 0;
        uint8_t max2 = 255;
    };

    struct Data
    {
        uint32_t magic = MAGIC;

        union
        {
            struct
            {
                uint8_t bpm_lo;
                uint8_t bpm_hi;
            };
            uint16_t bpm = 1605;
        };
        uint8_t divide = 5;
        uint8_t cvRange = 0;

        Preset preset[2] = {
            {3, 2, 5, 1, 0, 0, 0}, // Preset 0
            {3, 1, 5, 1, 0, 1, 3}  // Preset 1
        };
        Vactrol vactrol = {};
    };

    void load(bool forceReset = false);
    void save();
    Data &get();

private:
    Data config;
};
