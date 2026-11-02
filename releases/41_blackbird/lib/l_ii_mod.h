#pragma once

// ii device modules for Lua (ii.jf, ii.crow, ...), ported from crow's
// lib/l_ii_mod.c, plus crow's ii_* Lua functions (ii.raw, ii.pullup, ...).
// The device tables are generated into ii_mod_gen.h from lib/ii/*.lua.

#include <stdint.h>
#include <lua.h>
#include <lauxlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct{
    uint8_t cmd;
    const char* name;
} ii_mod_cmds_t;

#define II_MAX_ADDRESSES 8

typedef struct{
    uint8_t     addr;
    const char* name;
    uint8_t     addresses[II_MAX_ADDRESSES]; // leave room for up to 8 so we have a static size
    const ii_mod_cmds_t* commands;
    int         command_count;
} ii_box_t;

// Register the C functions ii.lua needs (c_ii_load, ii_lead, ...)
void l_ii_mod_preload(lua_State* L);

#ifdef __cplusplus
}
#endif
