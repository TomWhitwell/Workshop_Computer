#pragma once

// ii (Teletype I2C) value encoding: the conventions every ii device uses, with
// no dependency on blackbird, crow's device tables or Lua.
//
// A message is a command byte, then arguments; values are big-endian. Volts
// travel as signed 16-bit in Teletype units (II_TT_VOLT per volt). A getter
// is its setter's command plus II_GET_OFFSET (crow's convention).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define II_TT_VOLT     1638.3f  // Teletype units per volt (16384 = 10V)
#define II_GET_OFFSET  0x80     // getter command = setter command + this

typedef enum{ ii_void
            , ii_u8
            , ii_s8
            , ii_u16
            , ii_s16
            , ii_s16V    // volts, as Teletype units
            , ii_s16ms   // seconds, as milliseconds
            , ii_float   // 32bit (crow to crow)
            , ii_s32T    // seconds: s16 whole seconds, then s16V fraction (crow)
} ii_Type_t;

// Bytes a value of type `t` takes
uint8_t ii_type_size( ii_Type_t t );

// Read a value of type `t` from `data`
float ii_decode( const uint8_t* data, ii_Type_t t );

// Write `value` as type `t` to `dest` (clamped to the type's range); the bytes written
uint8_t ii_encode( uint8_t* dest, ii_Type_t t, float value );

#ifdef __cplusplus
}
#endif
