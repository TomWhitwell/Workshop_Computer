#pragma once

#include "ComputerCard.h"
#include <cstdint>
#include <cmath>
#include "lookup_tables.h"
#include "mesh_data.h"

// Base Oscillator class
class Oscillator
{
protected:
    // Shared waveform
    int32_t __not_in_flash_func(sine)(uint32_t ph)
    {
        // From ComputerCard sine_wave_lookup example
        uint32_t index = ph >> 23;        // convert from 32-bit phase to 9-bit lookup table index
        int32_t r = (ph & 0x7FFFFF) >> 7; // fractional part is last 23 bits of phase, shifted to 16-bit
        int32_t s1 = SINE_TABLE[index];
        int32_t s2 = SINE_TABLE[(index + 1) & 0x1FF];
        return (s2 * r + s1 * (65536 - r)) >> 20;
    }

    int32_t __not_in_flash_func(saw)(uint32_t ph)
    {
        return (int32_t)ph >> 20;
    }

    int32_t __not_in_flash_func(tri)(uint32_t ph)
    {

        return (abs((int32_t)ph >> 20) - 1024) << 1;
    }

    int32_t __not_in_flash_func(sqr)(uint32_t ph)
    {
        return (ph & 0x80000000) ? 2047 : -2048;
    }

public:
    // Virtual function to be overridden by derived classes
    virtual void __not_in_flash_func(compute)(uint32_t ph, int32_t mod1, int32_t mod2, int32_t *out) = 0;
    virtual ~Oscillator() = default;
};

/// Derived oscillator classes

/// OSC Bank 1 - Function defined shapes

// YinYang Shape Oscillator
class YinYang : public Oscillator
{
    // rotation phase accumulator
    uint32_t ph_rot = 0;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_rot, int32_t *out) override
    {
        // increment rotation phase
        ph_rot += mod_rot - 2048 << 11;

        // clamp grow factor
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;

        // prepare sign and phase for both yin and yang
        int32_t sign = ph >> 31 ? -1 : 1;                                // sign bit
        uint32_t ph_all = (uint32_t)(((uint64_t)(ph * 2) * grow) >> 32); // phase scaled by grow factor

        uint32_t sec = ph_all >> 30; // extract 2 MSB for section
        if (sec == 3)                // 0b11 : eye section, last 1/8 of cycle
        {
            // eye section, single arc
            uint32_t ph_eye = ph_all << 2;
            out[0] = sine(ph_eye * 2) >> 2;
            out[1] = -(sine(ph_eye * 2 + 0x40000000) >> 2) + 1024;
        }
        else
        {
            // body section, 3 parts of 3 arcs
            uint32_t ph_body = (uint32_t)(((uint64_t)ph_all * 0x55555556u) >> 30);
            uint32_t sec_body = ph_body >> 30;
            switch (sec_body)
            {
            case 0:
                out[0] = sine(ph_body * 2) >> 1;
                out[1] = -(sine(ph_body * 2 + 0x40000000) >> 1) + 1024;
                break;
            case 1:
            case 2:
                out[0] = -sine(ph_body - 0x40000000);
                out[1] = sine(ph_body);
                break;
            case 3:
                out[0] = sine(ph_body * 2) >> 1;
                out[1] = (sine(ph_body * 2 + 0x40000000) >> 1) - 1024;
                break;
            }
        }

        int64_t x = sign * out[0];
        int64_t y = sign * (out[1] + 8);

        // apply rotation
        int32_t s = sine(ph_rot);
        int32_t c = sine(ph_rot + 0x40000000);

        out[0] = (int32_t)((x * s + y * c) >> 11);
        out[1] = (int32_t)((x * c - y * s) >> 11);
    }
};

/// OSC Bank 2 - Mesh geometry shapes

// Polygon Waveform Oscillator
class PolyCube : public Oscillator
{
    const Point3D *path = CUBE_PATH;
    const uint32_t path_count = CUBE_PATH_COUNT;
    uint32_t ph_rot = 0;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_rot, int32_t *out) override
    {
        // clamp grow factor
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;
        ph = (uint32_t)(((uint64_t)ph * grow) >> 32);

        ph_rot += mod_rot - 2048 << 10;

        // interpolate position along cube path
        uint32_t segment = ((uint64_t)ph * (path_count - 1)) >> 32;
        uint16_t frac = (uint16_t)(((uint64_t)ph * (path_count - 1) & 0xFFFFFFFF) >> 22);

        Point3D p1 = path[segment];
        Point3D p2 = path[(segment + 1) % path_count];

        int32_t x = p1.x + (((p2.x - p1.x) * (int32_t)frac) >> 10);
        int32_t y = p1.y + (((p2.y - p1.y) * (int32_t)frac) >> 10);
        int32_t z = p1.z + (((p2.z - p1.z) * (int32_t)frac) >> 10);

        // apply rotation
        int32_t s = sine(ph_rot);
        int32_t c = sine(ph_rot - 0x40000000);

        int32_t rx = int(x * c - z * s) >> 11;
        int32_t ry = y;
        int32_t rz = (x * s + z * c) >> 11;

        // isometric projection, 30 degrees
        int32_t u = rx;
        int32_t v = (rz >> 1) + ((ry * 3547) >> 12);

        out[0] = u >> 1; // Scale to fit your specific output range
        out[1] = v >> 1;
    }
};

class PolyCone : public Oscillator
{
    const Point3D *path = CONE_PATH;
    const uint32_t path_count = CONE_PATH_COUNT;
    uint32_t ph_rot = 0;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_rot, int32_t *out) override
    {
        // clamp grow factor
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;
        ph = (uint32_t)(((uint64_t)ph * grow) >> 32);

        ph_rot += mod_rot - 2048 << 10;

        // interpolate position along cube path
        uint32_t segment = ((uint64_t)ph * (path_count - 1)) >> 32;
        uint16_t frac = (uint16_t)(((uint64_t)ph * (path_count - 1) & 0xFFFFFFFF) >> 22);

        Point3D p1 = path[segment];
        Point3D p2 = path[(segment + 1) % path_count];

        int32_t x = p1.x + (((p2.x - p1.x) * (int32_t)frac) >> 10);
        int32_t y = p1.y + (((p2.y - p1.y) * (int32_t)frac) >> 10);
        int32_t z = p1.z + (((p2.z - p1.z) * (int32_t)frac) >> 10);

        // apply rotation
        int32_t s = sine(ph_rot);
        int32_t c = sine(ph_rot - 0x40000000);

        int32_t rx = int(x * c - z * s) >> 11;
        int32_t ry = y;
        int32_t rz = (x * s + z * c) >> 11;

        // isometric projection, 30 degrees
        int32_t u = rx;
        int32_t v = (rz >> 1) + ((ry * 3547) >> 12);

        out[0] = u >> 1; // Scale to fit your specific output range
        out[1] = v >> 1;
    }
};

class PolyICO : public Oscillator
{
    const Point3D *path = ICOSPHERE_PATH;
    const uint32_t path_count = ICOSPHERE_PATH_COUNT;

    uint32_t ph_rot = 0;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_rot, int32_t *out) override
    {
        // clamp grow factor
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;
        ph = (uint32_t)(((uint64_t)ph * grow) >> 32);

        ph_rot += mod_rot - 2048 << 10;

        // interpolate position along cube path
        uint32_t segment = ((uint64_t)ph * (path_count - 1)) >> 32;
        uint16_t frac = (uint16_t)(((uint64_t)ph * (path_count - 1) & 0xFFFFFFFF) >> 22);

        Point3D p1 = path[segment];
        Point3D p2 = path[(segment + 1) % path_count];

        int32_t x = p1.x + (((p2.x - p1.x) * (int32_t)frac) >> 10);
        int32_t y = p1.y + (((p2.y - p1.y) * (int32_t)frac) >> 10);
        int32_t z = p1.z + (((p2.z - p1.z) * (int32_t)frac) >> 10);

        // apply rotation
        int32_t s = sine(ph_rot);
        int32_t c = sine(ph_rot - 0x40000000);

        int32_t rx = int(x * c - z * s) >> 11;
        int32_t ry = y;
        int32_t rz = (x * s + z * c) >> 11;

        // isometric projection, 30 degrees
        int32_t u = rx;
        int32_t v = (rz >> 1) + ((ry * 3547) >> 12);

        out[0] = u >> 1; // Scale to fit your specific output range
        out[1] = v >> 1;
    }
};

/// OSC Bank 3 - Wavetable shapes (single cycle stereo samples from vector graphics)

class YinYangCalligraphy : public Oscillator
{
    const StereoTable *YIN = &YIN_TABLE;
    const StereoTable *YANG = &YANG_TABLE;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_morph, int32_t *out) override
    {
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;
        ph = (uint32_t)(((uint64_t)ph * grow) >> 32);

        uint32_t morph = (uint32_t)(mod_morph < 0 ? 0 : (mod_morph > 4096 ? 4096 : mod_morph)) << 20;

        int32_t yin_l = lookup1024(ph, YIN->left);
        int32_t yin_r = lookup1024(ph, YIN->right);
        int32_t yang_l = lookup1024(ph, YANG->left);
        int32_t yang_r = lookup1024(ph, YANG->right);

        out[0] = (yin_l * (int32_t)(65536 - (morph >> 16)) + yang_l * (int32_t)(morph >> 16)) * 6 >> 19; // scale 6/8
        out[1] = -(yin_r * (int32_t)(65536 - (morph >> 16)) + yang_r * (int32_t)(morph >> 16)) * 6 >> 19;
    }

protected:
    // ph: 32-bit
    // table: int16_t[1024]
    inline int32_t __not_in_flash_func(lookup1024)(uint32_t ph, const int16_t *table)
    {
        // 10-bit index for 1024 entries
        uint32_t index = ph >> 22; // top 10 bits -> [0, 1023]
        // 22-bit fractional part -> convert to 16-bit fraction
        uint32_t r = (ph & 0x003FFFFF) >> 6; // keep upper 16 bits of fraction

        int32_t s1 = table[index];
        int32_t s2 = table[(index + 1) & 0x3FF]; // wrap at 1024

        // Linear interpolation: ((s2 - s1) * r >> 16) + s1
        return (s2 * (int32_t)r + s1 * (int32_t)(65536 - r)) >> 20;
    }
};

class RibbonWC : public Oscillator
{
    const StereoTable *RIBBON = &RIBBON_TABLE;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_stretch, int32_t *out) override
    {
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;
        ph = (uint32_t)(((uint64_t)ph * grow) >> 32);

        int32_t ribbon_l = lookup1024(ph, RIBBON->left);
        int32_t ribbon_r = lookup1024(ph, RIBBON->right) * (mod_stretch - 2048) >> 11;

        out[0] = ribbon_l * 6 >> 3; // scale 6/8
        out[1] = ribbon_r * 6 >> 3;
    }

protected:
    // ph: 32-bit
    // table: int16_t[1024]
    inline int32_t __not_in_flash_func(lookup1024)(uint32_t ph, const int16_t *table)
    {
        // 10-bit index for 1024 entries
        uint32_t index = ph >> 22; // top 10 bits -> [0, 1023]
        // 22-bit fractional part -> convert to 16-bit fraction
        uint32_t r = (ph & 0x003FFFFF) >> 6; // keep upper 16 bits of fraction

        int32_t s1 = table[index];
        int32_t s2 = table[(index + 1) & 0x3FF]; // wrap at 1024

        // Linear interpolation: ((s2 - s1) * r >> 16) + s1
        return (s2 * (int32_t)r + s1 * (int32_t)(65536 - r)) >> 20;
    }
};


class OutlineWC : public Oscillator
{
    const StereoTable *OUTLINE = &OUTLINE_TABLE;

public:
    void __not_in_flash_func(compute)(uint32_t ph, int32_t mod_grow, int32_t mod_stretch, int32_t *out) override
    {
        uint32_t grow = (uint32_t)(mod_grow < 0 ? 0 : (mod_grow > 4096 ? 4096 : mod_grow)) << 20;
        ph = (uint32_t)(((uint64_t)ph * grow) >> 32);

        int32_t outline_l = lookup1024(ph, OUTLINE->left);
        int32_t outline_r = lookup1024(ph, OUTLINE->right);

        out[0] = outline_l * 6 >> 3; // scale 6/8
        out[1] = outline_r * 6 >> 3;
    }

protected:
    // ph: 32-bit
    // table: int16_t[1024]
    inline int32_t __not_in_flash_func(lookup1024)(uint32_t ph, const int16_t *table)
    {
        // 10-bit index for 1024 entries
        uint32_t index = ph >> 22; // top 10 bits -> [0, 1023]
        // 22-bit fractional part -> convert to 16-bit fraction
        uint32_t r = (ph & 0x003FFFFF) >> 6; // keep upper 16 bits of fraction

        int32_t s1 = table[index];
        int32_t s2 = table[(index + 1) & 0x3FF]; // wrap at 1024

        // Linear interpolation: ((s2 - s1) * r >> 16) + s1
        return (s2 * (int32_t)r + s1 * (int32_t)(65536 - r)) >> 20;
    }
};