// ii (Teletype I2C) value encoding, from crow (monome/crow lib/ii.c, GPL-3.0)

#include "ii_codec.h"

#include <stdio.h>
#include <string.h>

static float lim_f( float v, float lo, float hi ){
    return v < lo ? lo : (v > hi ? hi : v);
}

uint8_t ii_type_size( ii_Type_t t )
{
    switch(t){ case ii_void:  return 0;
               case ii_u8:    return 1;
               case ii_s8:    return 1;
               case ii_u16:   return 2;
               case ii_s16:   return 2;
               case ii_s16V:  return 2;
               case ii_s16ms: return 2;
               case ii_float: return 4;
               case ii_s32T:  return 4;
               default:       return 0;
    }
    return 0;
}

float ii_decode( const uint8_t* data, ii_Type_t type )
{
    float val = 0; // return value default to zero
    uint16_t u16 = 0;
    int16_t s16;
    switch( type ){
        case ii_u8:
            val = (float)(*data++);
            break;
        case ii_s8:
            val = (float)(*(const int8_t*)data++);
            break;
        case ii_u16:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            val = (float)u16;
            break;
        case ii_s16:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16;
            break;
        case ii_s16V:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16 * (1.0f / II_TT_VOLT); // Scale Teletype down to float
            break;
        case ii_s16ms:
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16 / 1000.0f;
            break;
        case ii_float:
            memcpy(&val, data, 4);
            break;
        case ii_s32T:
            // seconds: signed 16
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val = (float)s16;
            // subsecond: signed 16 V
            u16  = ((uint16_t)*data++)<<8;
            u16 |= *data++;
            memcpy(&s16, &u16, 2);
            val += (float)s16 * (1.0f / II_TT_VOLT);
            break;
        default: printf("ii_decode unmatched\n"); break;
    }
    return val;
}

uint8_t ii_encode( uint8_t* dest, ii_Type_t type, float data )
{
    uint8_t len = 0;
    uint8_t* d = dest;

    uint16_t u16; int16_t s16;
    switch( type ){
        case ii_u8: d[len++] = (uint8_t)lim_f(data,0.0f,255.0f);
            break;
        case ii_s8: d[len++] = (uint8_t)(int8_t)lim_f(data,-128.0f,127.0f);
            break;
        case ii_u16:
            u16 = (uint16_t)lim_f(data, 0.0f, 65535.0f);
            d[len++] = (uint8_t)(u16>>8);          // High byte first
            d[len++] = (uint8_t)(u16 & 0x00FF);    // Low byte
            break;
        case ii_s16V:
            data *= II_TT_VOLT; // Scale float up to Teletype
            __attribute__((fallthrough)); // then as ii_s16
        case ii_s16:
            s16 = (int16_t)lim_f(data, -32768.0f, 32767.0f);
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            break;
        case ii_s16ms:
            data *= 1000.0f;
            s16 = (int16_t)lim_f(data, -32768.0f, 32767.0f);
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            break;
        case ii_float:
            memcpy( &(d[len]), &data, 4 );
            len += 4;
            break;
        case ii_s32T:{
            int iTime = (int)data;
            float subTime = data - (float)iTime;
            if( iTime < -32768 ){
                iTime = -32768; subTime = 0.0f;
            } else if( iTime > 32767 ){
                iTime = 32767; subTime = 0.0f;
            }
            s16 = (int16_t)iTime;
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            subTime *= II_TT_VOLT; // Scale float up to Teletype
            s16 = (int16_t)subTime;
            memcpy(&u16, &s16, 2);
            d[len++] = (uint8_t)(u16>>8);
            d[len++] = (uint8_t)(u16 & 0x00FF);
            break;}
        default: return 0;
    }
    return len;
}
