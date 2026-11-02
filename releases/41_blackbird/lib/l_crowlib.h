#pragma once

#include "lua.h" // Fixed path for our Lua build
#include "lauxlib.h"
#include "lualib.h"
#include "events_lockfree.h"  // For metro_event_lockfree_t type

// initialize the default crow environment variables & data structures
void l_crowlib_init(lua_State* L);

// destroys user init() function and replaces it with a void fn
void l_crowlib_emptyinit(lua_State* L);

// execute crow.reset() which reverts state of all modules to default
int l_crowlib_crow_reset( lua_State* L );

// The script's globals: _G's __newindex records each new one in _user
// (install once, at boot); clear_user_globals deletes them all and starts a
// fresh _user. Neither compiles Lua, and the clear doesn't allocate (only the
// new _user table), so they work even when the Lua heap is full.
void l_crowlib_install_user_tracer( lua_State* L );
void l_crowlib_clear_user_globals( lua_State* L );

// Install ii.self's default actions (volts, slew, ...), if ii is loaded
void l_crowlib_ii_follow_defaults( lua_State* L );

int l_crowlib_justvolts(lua_State *L);
int l_crowlib_just12(lua_State *L);
int l_crowlib_hztovolts(lua_State *L);

// L_queue_* functions for event posting
void L_queue_metro( int id, int state );
void L_queue_clock_resume( int coro_id );
// L_queue_clock_start and L_queue_clock_stop removed - using direct calls instead

// Lock-free event handler functions
void L_handle_metro_lockfree( metro_event_lockfree_t* event );
void L_handle_input_lockfree( input_event_lockfree_t* event );
void L_handle_asl_done_lockfree( asl_done_event_lockfree_t* event );
void L_handle_clock_resume_lockfree( clock_event_lockfree_t* event );

// Diagnostics for high-frequency scheduling callbacks
uint32_t metro_cb_worst_us(void);
uint32_t metro_cb_overrun_count(void);
uint32_t metro_cb_last_us(void);
void metro_cb_reset_stats(void);

uint32_t clock_resume_cb_worst_us(void);
uint32_t clock_resume_cb_last_us(void);
void clock_resume_cb_reset_stats(void);
