#include "l_bootstrap.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "l_crowlib.h"
#include "lib/caw.h"  // For Caw_printf to send crow-style messages

// TinyUSB CDC for direct debug output
#include "tusb.h"

// Lua libs wrapped in C-headers
#include "build/crowlib.h"
#include "build/asl.h"
#include "build/asllib.h"
#include "build/clock.h"
#include "build/metro.h"
#include "build/public.h"
#include "build/input.h"
#include "build/output.h"
#include "build/ii.h"
// #include "build/iihelp.h"    // generated lua stub for loading i2c modules
#include "build/calibrate.h"
#include "build/sequins.h"
#include "build/quote.h"
#include "build/timeline.h"
#include "build/hotswap.h"

// #include "build/ii_lualink.h" // generated C header for linking to lua

struct lua_lib_locator{
    const char* name;
    const unsigned char* addr_of_luacode;
    const bool stripped;
    const unsigned int len;
};

static int _open_lib( lua_State *L, const struct lua_lib_locator* lib, const char* name );
// Forward declaration of lua_c_tell from main.cpp for hardware commands
extern int LuaManager_lua_c_tell(lua_State* L);

static void lua_full_gc(lua_State* L);

// _c.tell function for detection callbacks and output commands
// This implements crow's tell() function which sends formatted messages over USB
int l_bootstrap_c_tell(lua_State* L) {
    int nargs = lua_gettop(L);
    
    if (nargs < 1) {
        lua_settop(L, 0);
        return 0;
    }
    
    const char* event_type = luaL_checkstring(L, 1);
    
    // Handle ONLY 'output' as a hardware command - delegate to lua_c_tell
    // All other messages (stream, change, window, etc) are sent as ^^ protocol messages
    if (nargs >= 2 && lua_isnumber(L, 2) && strcmp(event_type, "output") == 0) {
        // Delegate to the hardware handler for output commands only
        return LuaManager_lua_c_tell(L);
    }
    
    // Handle crow-style ^^ messages (stream, change, window, pupdate, pub, etc)
    // These are sent to the host computer over USB
    // Format: ^^event_type(arg1,arg2,...)
    // All arguments are coerced to strings (like real crow's _print_tell)
    switch(nargs) {
        case 1:
            Caw_printf("^^%s()", event_type);
            break;
        case 2:
            Caw_printf("^^%s(%s)", event_type, luaL_checkstring(L, 2));
            break;
        case 3:
            // Check if arg 3 is a table - if so, convert to "[table]" string
            if (lua_istable(L, 3)) {
                Caw_printf("^^%s(%s,[table])", event_type, luaL_checkstring(L, 2));
            } else {
                Caw_printf("^^%s(%s,%s)", event_type, 
                           luaL_checkstring(L, 2),
                           luaL_checkstring(L, 3));
            }
            break;
        case 4:
            Caw_printf("^^%s(%s,%s,%s)", event_type,
                       luaL_checkstring(L, 2),
                       luaL_checkstring(L, 3),
                       luaL_checkstring(L, 4));
            break;
        case 5:
            Caw_printf("^^%s(%s,%s,%s,%s)", event_type,
                       luaL_checkstring(L, 2),
                       luaL_checkstring(L, 3),
                       luaL_checkstring(L, 4),
                       luaL_checkstring(L, 5));
            break;
        default:
            // More than 5 args - just send first 5
            Caw_printf("^^%s(%s,%s,%s,%s,...)", event_type,
                       luaL_checkstring(L, 2),
                       luaL_checkstring(L, 3),
                       luaL_checkstring(L, 4),
                       luaL_checkstring(L, 5));
            break;
    }
    
    lua_settop(L, 0);
    return 0;
}

// mark the 3rd arg 'false' if you need to debug that library
const struct lua_lib_locator Lua_libs[] =
    { { "lua_crowlib"   , crowlib   , true, crowlib_len}
    , { "lua_asl"       , asl       , true, asl_len}
    , { "lua_asllib"    , asllib    , true, asllib_len}
    , { "lua_clock"     , clock     , true, clock_len}
    , { "lua_metro"     , metro     , true, metro_len}
    , { "lua_input"     , input     , true, input_len}
    , { "lua_output"    , output    , true, output_len}
    , { "lua_public"    , public    , true, public_len}
    , { "lua_ii"        , ii        , true, ii_len}
    // , { "build_iihelp"  , build_iihelp_lc    , true, build_iihelp_lc_len}
    , { "lua_calibrate" , calibrate , true, calibrate_len}
    , { "lua_sequins"   , sequins   , true, sequins_len}
    , { "lua_quote"     , quote     , true, quote_len}
    , { "lua_timeline"  , timeline  , true, timeline_len}
    , { "lua_hotswap"   , hotswap   , true, hotswap_len}
    , { NULL            , NULL               , true, 0}
    };


void l_bootstrap_init(lua_State* L){
    // collectgarbage('setpause', 120): see the GC note in LuaManager::init (main.cpp)
    lua_gc(L, LUA_GCSETPAUSE, 120);
    lua_gc(L, LUA_GCSETSTEPMUL, 260);

    // dofile just calls c_dofile
    lua_getglobal(L, "c_dofile");
    lua_setglobal(L, "dofile");

    // crowlib.lua now only contains our print() definition
    // _c = dofile('lua/crowlib.lua')
    lua_pushliteral(L, "lua/crowlib.lua");
    l_bootstrap_dofile(L); // hotrod without l_call
    lua_settop(L, 0);

    // NOTE: _c and crow tables are created in main.cpp BEFORE l_bootstrap_init is called
    // DO NOT recreate them here or we'll lose the setup done in main.cpp
    // main.cpp creates:
    //   - _c table with l_bootstrap_c_tell
    //   - crow table (separate from _c) with l_bootstrap_c_tell
    
    // crowlib C extensions (adds crow.reset, crow.init, and other C functions)
    l_crowlib_init(L);

    // track all user-created globals 
    luaL_dostring(L,
        "_user={}\n"
        "local function trace(t,k,v)\n"
            "_user[k]=true\n"
            "rawset(t,k,v)\n"
        "end\n"
        "setmetatable(_G,{ __newindex = trace })\n"
        );

    // perform two full garbage collection cycles for full cleanup
    lua_full_gc(L);
}


int l_bootstrap_dofile(lua_State* L)
{
    const char* l_name = luaL_checkstring(L, 1);
    int l_len = strlen(l_name);
    if(l_len > 32) printf("FIXME bootstrap: filepath >32bytes!\r\n");

    // simple C version of "luapath_to_cpath"
    // l_name is a lua native path: "lua/asl.lua"
    char cname[32]; // 32bytes is more than enough for any path
    int p=0; // pointer into cname
    for(int i=0; i<l_len; i++){
        switch(l_name[i]){
            case '/':{ cname[p++] = '_'; } break;
            case '.':{ cname[p++] = 0; } goto strcomplete;
            default:{ cname[p++] = l_name[i]; } break;
        }
    }
    // goto fail; // no match was found, so error out (silently?)

strcomplete:
    lua_pop( L, 1 );
    switch( _open_lib( L, Lua_libs, cname ) ){
        case -1: goto fail;
        case 1: lua_full_gc(L); return 1;
        default: break;
    }
    // switch( _open_lib( L, Lua_ii_libs, cname ) ){
    //     case -1: goto fail;
    //     case 1: lua_full_gc(L); return 1;
    //     default: break;
    // }
    printf("can't open library: %s\n", (char*)cname);

fail:
    lua_pushnil(L);
    return 1;
}






/////////// private defns

static int _open_lib( lua_State *L, const struct lua_lib_locator* lib, const char* name )
{
    uint8_t i = 0;
    while( lib[i].addr_of_luacode != NULL ){
        if( !strcmp( name, lib[i].name ) ){ // if the strings match
            if( luaL_loadbuffer(L, (const char*)lib[i].addr_of_luacode
                                 , lib[i].len
                                 , lib[i].name) ){
                printf("can't load library: %s\n", (char*)lib[i].name );
                printf( "%s\n", (char*)lua_tostring( L, -1 ) );
                lua_pop( L, 1 );
                return -1; // error
            }
            if( lua_pcall(L, 0, LUA_MULTRET, 0) ){
                printf("can't exec library: %s\n", (char*)lib[i].name );
                printf( "%s\n", (char*)lua_tostring( L, -1 ) );
                lua_pop( L, 1 );
                return -1; // error
            }
            return 1; // table is left on the stack as retval
        }
        i++;
    }
    return 0; // not found
}

static void lua_full_gc(lua_State* L){
    lua_gc(L, LUA_GCCOLLECT, 1);
    lua_gc(L, LUA_GCCOLLECT, 1);
}
