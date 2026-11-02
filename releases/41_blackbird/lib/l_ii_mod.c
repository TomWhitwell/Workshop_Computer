// ii device modules and ii_* Lua functions, ported from crow (monome/crow
// lib/l_ii_mod.c and the ii part of lib/lualink.c, GPL-3.0)

#include "l_ii_mod.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ii_mod_gen.h" // generated from lib/ii/*.lua (CMakeLists.txt)
#include "caw.h"
#include "ii.h"

////////////////////////////////////////////////
// set as commands are executed
static uint8_t active_address = 0x0;
static uint8_t active_cmd = 0x0;
static ii_box_t* active_box = NULL;


////////////////////////////////////////////////
// search functions: peer into the generated structures

static ii_box_t* find_mod_struct_by_address(uint8_t addr){
    for(int i=0; i<ii_mod_count; i++){
        uint8_t* addrs = ii_mods[i]->addresses;
        for(int a=0; a<II_MAX_ADDRESSES; a++){
            if(addrs[a] == addr){
                return ii_mods[i];
            }
        }
    }
    return NULL;
}

static ii_box_t* find_mod_struct_by_name(const char* name){
    for(int i=0; i<ii_mod_count; i++){
        if(strcmp(ii_mods[i]->name, name) == 0){
            return ii_mods[i];
        }
    }
    return NULL;
}

static const char* find_cmd_name(ii_box_t* box, uint8_t cmd){
    // search backward as getters are 2nd half of command list
    const ii_mod_cmds_t* cmds = box->commands;
    for(int i=box->command_count-1; i>=0; i--){
        if(cmds[i].cmd == cmd){
            return cmds[i].name;
        }
    }
    return NULL;
}

static int query_to_cmd(ii_box_t* box, const char* str){
    // search backward as getters are 2nd half of command list
    const ii_mod_cmds_t* cmds = box->commands;
    for(int i=box->command_count-1; i>=0; i--){
        if(strcmp(cmds[i].name, str) == 0){
            return cmds[i].cmd;
        }
    }
    return 0xFF;
}

static int string_to_cmd(ii_box_t* box, const char* str){
    const ii_mod_cmds_t* cmds = box->commands;
    for(int i=0; i<box->command_count; i++){
        if(strcmp(cmds[i].name, str) == 0){
            return cmds[i].cmd;
        }
    }
    return 0xFF;
}

static ii_box_t* find_mod_struct_from_table(lua_State* L, int self_stack_index){
    lua_getfield(L, self_stack_index, "_name"); // push the device name onto TOS
    const char* name = lua_tostring(L, -1);
    ii_box_t* box = name ? find_mod_struct_by_name(name) : NULL;
    lua_pop(L, 1);
    return box;
}

static int find_addr_ix(ii_box_t* box, int addr_now){
    // returns the 1-based index into .addresses that is currently in .addr
    uint8_t* addrs = box->addresses;
    for(int i=0; i<II_MAX_ADDRESSES; i++){
        if(addr_now == addrs[i]){
            return i+1; // lua is 1-based
        }
    }
    return -1;
}


////////////////////////////////////////////////
// ii.mod[n]: choose one of a range of devices

static int l_ii_setaddress( lua_State* L ){
    uint8_t addr_ix = (uint8_t)luaL_checkinteger(L, 2) - 1; // lua is 1-based
    if(addr_ix >= II_MAX_ADDRESSES) addr_ix = 0;

    ii_box_t* box = find_mod_struct_from_table(L, 1);
    if(box == NULL) return luaL_error(L, "unknown ii module");

    int new_addr = box->addresses[addr_ix];
    if(new_addr){ // protect against 0 (ie. invalid addresses)
        box->addr = new_addr;
    }

    lua_settop(L, 1); // returns self
    return 1;
}


/////////////////////////////////////////////////////
// __index of each module: help, event, get, or a command

static int __index_help( lua_State* L ){
    Caw_stream_constchar( ii_list_cmds(active_address) );
    lua_settop(L, 0);
    return 0;
}

static int __index_event( lua_State* L ){
    // the default event handler: tell the host
    double data = luaL_checknumber(L, 2);
    lua_getfield(L, 1, "name");
    const char* name = luaL_checkstring(L, -1);
    lua_getfield(L, 1, "device");
    int device = luaL_checkinteger(L, -1);
    lua_getfield(L, 1, "arg");
    double arg = luaL_checknumber(L, -1);

    Caw_printf( "^^ii.%s({name=[[%s]],device=%i,arg=%g},%g)"
        , active_box->name
        , name
        , device
        , arg
        , data);

    lua_settop(L, 0);
    return 0;
}

static int __index_get( lua_State* L ){
    const char* cmd_str = luaL_checkstring(L, 1);
    int query_cmd = query_to_cmd(active_box, cmd_str);
    if(query_cmd == 0xFF)
        return luaL_error(L, "getter not found for '%s'", cmd_str);
    float data[4] = {0,0,0,0};
    int nargs = lua_gettop(L) - 1; // less the 'string'
    if(nargs > 4) nargs = 4; // limit to 4 ii arguments
    for(int i=0; i<nargs; i++ ){
        data[i] = luaL_checknumber(L, i+2);
    }
    if( ii_leader_enqueue( active_address, query_cmd, data ) ){
        printf("ii_lead failed\n");
    }
    lua_settop(L, 0);
    return 0;
}

static int __index_command( lua_State* L ){
    float data[4] = {0,0,0,0};
    int nargs = lua_gettop(L);
    if(nargs > 4) nargs = 4; // limit to 4 ii arguments
    for(int i=0; i<nargs; i++ ){
        data[i] = luaL_checknumber(L, i+1);
    }
    if( ii_leader_enqueue( active_address, active_cmd, data ) ){
        printf("ii_lead failed\n");
    }
    lua_settop(L, 0);
    return 0;
}

static int l_ii_index( lua_State* L ){
    // @1: self (eg. ii.jf{} table)
    // @2: command-string (eg. event, help, get or a cmd-name)
    active_box = find_mod_struct_from_table(L, 1);
    if(active_box == NULL) return luaL_error(L, "unknown ii module");
    active_address = active_box->addr; // the currently selected address

    const char* name = lua_tostring(L, 2);
    if(name == NULL) return luaL_error(L, "ii: expected a command name");
    lua_settop(L, 0);

    if(strcmp(name, "help") == 0){
        active_address = active_box->addresses[0]; // help is by first address
        lua_pushcfunction(L, __index_help);
    } else if(strcmp(name, "event") == 0){
        lua_pushcfunction(L, __index_event);
    } else if(strcmp(name, "get") == 0){
        lua_pushcfunction(L, __index_get);
    } else {
        active_cmd = string_to_cmd(active_box, name);
        if(active_cmd == 0xFF){
            return luaL_error(L, "can't find: ii.%s.%s", active_box->name, name);
        }
        lua_pushcfunction(L, __index_command);
    }
    return 1;
}

static int l_ii_cmd_from_ix( lua_State* L ){
    uint8_t addr = luaL_checkinteger(L, 1);
    uint8_t cmd = luaL_checkinteger(L, 2);
    lua_settop(L,0);
    ii_box_t* box = find_mod_struct_by_address(addr);
    if(box == NULL)
        return luaL_error(L, "couldn't find address");
    lua_pushstring(L, box->name);
    const char* cmd_name = find_cmd_name(box, cmd);
    if(cmd_name == NULL)
        return luaL_error(L, "couldn't find cmd");
    lua_pushstring(L, cmd_name);
    lua_pushinteger(L, find_addr_ix(box, addr));
    return 3;
}

static int l_ii_load_mods( lua_State* L ){
    // ii.<name> = ii.newmod('<name>') for each module. @1 is the ii table
    for(int i=0; i<ii_mod_count; i++){
        const char* name = ii_mods[i]->name;
        lua_getfield(L, 1, "newmod");
        lua_pushstring(L, name);
        lua_call(L, 1, 1);
        lua_setfield(L, 1, name);
    }
    lua_settop(L, 0);
    return 0;
}


/////////////////////////////////////////////////////
// ii_* functions (crow's lualink.c)

static int _ii_list_modules( lua_State *L ){
    Caw_stream_constchar( ii_list_modules() );
    return 0;
}

static int _ii_list_commands( lua_State *L ){
    uint8_t address = luaL_checkinteger(L, 1);
    Caw_stream_constchar( ii_list_cmds(address) );
    lua_pop(L, 1);
    return 0;
}

static int _ii_pullup( lua_State *L ){
    int state = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : (int)luaL_checkinteger(L, 1);
    ii_set_pullups(!!state);
    lua_settop(L, 0);
    return 0;
}

static int _ii_lead( lua_State *L ){
    float data[4] = {0,0,0,0};
    int nargs = lua_gettop(L);
    if( nargs > 2 && nargs <= 6 ){
        for( int i=0; i<(nargs-2); i++ ){
            data[i] = luaL_checknumber(L, i+3);
        }
    }
    if( ii_leader_enqueue( luaL_checkinteger(L, 1) // address
                         , luaL_checkinteger(L, 2) // command
                         , data
                         ) ){ printf("ii_lead failed\n"); }
    lua_settop(L, 0);
    return 0;
}

static int _ii_lead_bytes( lua_State *L ){
    int nargs = lua_gettop(L);
    uint8_t rx_len = 0;
    if(nargs < 2 || nargs > 3){ return 0; }
    if(nargs == 3){
        rx_len = (uint8_t)luaL_checkinteger(L, 3);
    }
    uint8_t address = luaL_checkinteger(L, 1);
    size_t len;
    uint8_t *data = (uint8_t *)luaL_checklstring(L, 2, &len);
    if( ii_leader_enqueue_bytes( address, data, (uint8_t)len, rx_len ) ){
        printf("ii_lead_bytes failed\n");
    }
    lua_settop(L, 0);
    return 0;
}

static int _ii_address( lua_State *L ){
    ii_set_address( luaL_checkinteger(L, 1) );
    lua_settop(L, 0);
    return 0;
}

static int _ii_get_address( lua_State *L ){
    lua_pushinteger( L, ii_get_address() );
    return 1;
}

static int _i2c_fastmode( lua_State *L ){
    // crow: true/1 = fast (400kHz), false/0 = standard (100kHz)
    int state = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : (int)lua_tointeger(L, 1);
    ii_set_fastmode(state == 1);
    lua_settop(L, 0);
    return 0;
}

static const struct luaL_Reg lib_ii_mod[]=
    { { "c_ii_setaddress"  , l_ii_setaddress   }
    , { "c_ii_index"       , l_ii_index        }
    , { "c_ii_cmd"         , l_ii_cmd_from_ix  }
    , { "c_ii_load"        , l_ii_load_mods    }
    , { "ii_list_modules"  , _ii_list_modules  }
    , { "ii_list_commands" , _ii_list_commands }
    , { "ii_pullup"        , _ii_pullup        }
    , { "ii_lead"          , _ii_lead          }
    , { "ii_lead_bytes"    , _ii_lead_bytes    }
    , { "ii_set_add"       , _ii_address       }
    , { "ii_get_add"       , _ii_get_address   }
    , { "i2c_fastmode"     , _i2c_fastmode     }
    , { NULL               , NULL              }
    };

void l_ii_mod_preload(lua_State* L){
    for(int fn = 0; lib_ii_mod[fn].func != NULL; fn++){
        lua_register( L, lib_ii_mod[fn].name, lib_ii_mod[fn].func );
    }
}
