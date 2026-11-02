#include "l_crowlib.h"

#include <math.h>

#include "l_bootstrap.h" // l_bootstrap_dofile
#include "l_ii_mod.h"       // l_ii_mod_preload
#include "random.h"   // Random_Get()
#include "lib/ii.h"         // ii_*()
#include "lib/ashapes.h"    // AShaper_get_state
#include "lib/caw.h"        // Caw_printf()
#include "lib/metro.h"       // Metro_get_period_seconds
#include "lib/clock.h"       // clock_cancel_coro_all()
#include "pico/time.h"       // time_us_32 for diagnostics
// #include "lib/io.h"         // IO_GetADC() - not used in emulator
// Declare get_input_state_simple function for compatibility (implemented in main.cpp)
extern float get_input_state_simple(int channel); // returns input voltage in volts
#include "lib/events_lockfree.h"  // Lock-free event queues
#include "lib/slopes.h"     // S_reset()
#include "ll_timers.h"       // Timer_Set_Block_Size()
#include "clock_ll.h"        // ll_cleanup()
#include "lib/events_lockfree.h" // events_lockfree_clear()
#include "fastmath.h"
#include <string.h>

#define L_CL_MIDDLEC 		(261.63f)
#define L_CL_MIDDLEC_INV 	(1.0f/L_CL_MIDDLEC)
#define L_CL_JIVOLT 		(1.0f/logf(2.f))


static int _ii_follow_reset( lua_State* L );
static int _random_arity_n( lua_State* L );
static int _tell_get_out( lua_State* L );
static int _tell_get_cv( lua_State* L );
static int _lua_void_function( lua_State* L );
static int _delay( lua_State* L );
static void _install_ii_stub(lua_State* L);

static int _ii_stub_call(lua_State* L);
static int _ii_stub_index(lua_State* L);

// Forward declarations for L_handle_* functions
void L_handle_clock_resume_lockfree(clock_event_lockfree_t* event);

// function() end
// useful as a do-nothing callback
static int _lua_void_function( lua_State* L ){
	lua_settop(L, 0);
	return 0;
}

// no-op callable table for environments without ii support
static int _ii_stub_call(lua_State* L) {
    lua_settop(L, 0);
    return 0;
}

static int _ii_stub_index(lua_State* L) {
    // args: (self, key)
    // create and memoize a new stub table so chained lookups work:
    // ii.jf.mode(1) -> ii.jf (table) -> .mode (table) -> (1) (no-op)
    lua_newtable(L);
    luaL_getmetatable(L, "ii_stub_mt");
    lua_setmetatable(L, -2);

    lua_pushvalue(L, 2);   // key
    lua_pushvalue(L, -2);  // value (child stub)
    lua_rawset(L, 1);
    return 1;
}

static void _install_ii_stub(lua_State* L) {
    if (luaL_newmetatable(L, "ii_stub_mt")) {
        lua_pushcfunction(L, _ii_stub_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, _ii_stub_call);
        lua_setfield(L, -2, "__call");
    }
    lua_pop(L, 1); // pop metatable

    lua_newtable(L);
    luaL_getmetatable(L, "ii_stub_mt");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "ii");
    lua_settop(L, 0);
}

static void _load_lib(lua_State* L, char* filename, char* luaname){
	lua_pushfstring(L, "lua/%s.lua", filename);
    l_bootstrap_dofile(L);
    lua_setglobal(L, luaname);
    lua_settop(L, 0);
}

// called after crowlib lua file is loaded
// here we add any additional globals and such
void l_crowlib_init(lua_State* L){

    //////// create a nop function
    lua_pushcfunction(L, _lua_void_function);
    lua_setglobal(L, "nop_fn");

	//////// load all libraries
	_load_lib(L, "input", "Input");
	_load_lib(L, "output", "Output");
	_load_lib(L, "asl", "asl");
	_load_lib(L, "asllib", "asllib");
	_load_lib(L, "metro", "metro");

    // load C funcs into lua env first
    l_ii_mod_preload(L);

    // ii is optional in this firmware/emulator; don't let missing ii abort user init.
    // Only load lua/ii.lua if the backing C hooks exist; otherwise install a no-op stub.
    lua_getglobal(L, "c_ii_load");
    bool have_ii_backend = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (have_ii_backend) {
        _load_lib(L, "ii", "ii");
    }
    lua_getglobal(L, "ii");
    bool have_ii_global = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!have_ii_global) {
        _install_ii_stub(L);
    }

	_load_lib(L, "calibrate", "cal");
	_load_lib(L, "sequins", "sequins");
	_load_lib(L, "public", "public");
	_load_lib(L, "clock", "clock");
	_load_lib(L, "quote", "quote");
	_load_lib(L, "timeline", "timeline");
	_load_lib(L, "hotswap", "hotswap");


	//////// crow.reset
    // Create crow table if it doesn't exist
    lua_getglobal(L, "crow");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "crow");
    } else {
        lua_pop(L, 1); // pop the existing crow table
    }
    
    // Now set crow.reset
    lua_getglobal(L, "crow"); // @1
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "reset"); // set crow.reset, pops function
    
    // Set crow.init (alias for crow.reset)  
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "init"); // set crow.init, pops function
    
    lua_pop(L, 1); // pop crow table


	//////// tell
	// C.tell = tell
    // NOTE: We set up _c.tell (crow.tell) in l_bootstrap.c, not here
    // Commenting out this code that tries to use a non-existent global 'tell' function
    // lua_getglobal(L, "crow"); // @1
    // lua_getglobal(L, "tell"); // @2
    // lua_setfield(L, 1, "tell");
    // lua_settop(L, 0);


	//////// get_out & get_cv
	lua_pushcfunction(L, _tell_get_out);
	lua_setglobal(L, "get_out");
	lua_pushcfunction(L, _tell_get_cv);
	lua_setglobal(L, "get_cv");
    lua_settop(L, 0);


	//////// input

	// -- Input
	// input = {1,2}
	// for chan = 1, #input do
	//   input[chan] = Input.new( chan )
	// end
	lua_createtable(L, 2, 0); // 2 array elements
	lua_setglobal(L, "input"); // -> @0

	lua_getglobal(L, "input"); // @1
	for(int i=1; i<=2; i++){
		lua_getglobal(L, "Input"); // @2
		lua_getfield(L, 2, "new"); // Output.new @3
		lua_pushinteger(L, i); // push the key
		lua_call(L, 1, 1); // Output.new(chan) -> replace key with value -> @3
		lua_pushinteger(L, i); // push the key
		lua_rotate(L, -2, 1); // swap top 2 elements
		lua_settable(L, 1); // output[chan] = result
		lua_settop(L, 1); // discard everything except _G.output
	}
	lua_settop(L, 0);


	//////// output (asl)

	// -- Output
	// output = {1,2,3,4}
	// for chan = 1, #output do
	// 	 output[chan] = Output.new( chan )
	// end
	lua_createtable(L, 4, 0); // 4 array elements
	lua_setglobal(L, "output"); // -> @0

	lua_getglobal(L, "output"); // @1
	for(int i=1; i<=4; i++){
		lua_getglobal(L, "Output"); // @2
		lua_getfield(L, 2, "new"); // Output.new @3
		lua_pushinteger(L, i); // push the key
		lua_call(L, 1, 1); // Output.new(chan) -> replace key with value -> @3
		lua_pushinteger(L, i); // push the key
		lua_rotate(L, -2, 1); // swap top 2 elements
		lua_settable(L, 1); // output[chan] = result
		lua_settop(L, 1); // discard everything except _G.output
	}
	lua_settop(L, 0);


	// LL_get_state = get_state
    lua_getglobal(L, "get_state");
    lua_setglobal(L, "LL_get_state");
	lua_settop(L, 0);


	//////// ii follower default actions

	// install the reset function
	lua_pushcfunction(L, _ii_follow_reset);
	lua_setglobal(L, "ii_follow_reset");

	// call it to reset immediately
	lua_getglobal(L, "ii_follow_reset");
	lua_call(L, 0, 0);
	lua_settop(L, 0);


	//////// ii.pullup(true)
	ii_set_pullups(1);


	//////// RANDOM

    // Install fast math overrides (LUT + integer core) and patch math.*
    // Saves originals as math.ssin/scos/satan/sexp/slog/spow
    fastmath_lua_install(L, 1);

	// hook existing math.random into math.srandom
	lua_getglobal(L, "math"); // 1
	lua_getfield(L, 1, "random"); // 2
	lua_setfield(L, 1, "srandom");
	lua_settop(L, 1); // abandon anything above _G.math
	// _G.math is still at stack position 1
	lua_getfield(L, 1, "randomseed");
	lua_setfield(L, 1, "srandomseed");
	lua_settop(L, 0);

	// set math.random to the c-func for true random
	lua_getglobal(L, "math");
	lua_pushcfunction(L, _random_arity_n);
	lua_setfield(L, -2, "random");
	lua_settop(L, 0);


	//////// DELAY
	// creates a closure, so this is just way easier
	luaL_dostring(L,"function delay(action, time, repeats)\n"
						"local r = repeats or 0\n"
					    "return clock.run(function()\n"
					            "for i=1,1+r do\n"
					                "clock.sleep(time)\n"
					                "action(i)\n"
					            "end\n"
					        "end)\n"
					"end\n");

    l_crowlib_emptyinit(L);

    //////// bb table (create if missing) and add priority controls
    lua_getglobal(L, "bb"); // @1
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "bb");
    } else {
        lua_pop(L, 1);
    }
}

void l_crowlib_emptyinit(lua_State* L){
    //////// set init() to a NOP
    lua_getglobal(L, "nop_fn");
    lua_setglobal(L, "init");
}

// _G.__newindex: a new global, from the script. Record it in _user.
static int _user_tracer( lua_State* L ){
    // @1 _G, @2 key, @3 value
    lua_pushliteral(L, "_user");
    lua_rawget(L, 1);
    if( lua_istable(L, -1) ){
        lua_pushvalue(L, 2);
        lua_pushboolean(L, 1);
        lua_rawset(L, -3); // _user[key] = true
    }
    lua_pop(L, 1);
    lua_rawset(L, 1); // _G[key] = value
    return 0;
}

void l_crowlib_install_user_tracer( lua_State* L ){
    lua_pushglobaltable(L);
    lua_newtable(L);
    lua_setfield(L, -2, "_user");             // _G has no tracer yet: plain set
    if( !lua_getmetatable(L, -1) ){ lua_newtable(L); }
    lua_pushcfunction(L, _user_tracer);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

void l_crowlib_clear_user_globals( lua_State* L ){
    int top = lua_gettop(L);
    lua_pushglobaltable(L);                   // @g
    int g = lua_gettop(L);
    lua_pushliteral(L, "_user");
    lua_rawget(L, g);                         // @u
    int u = lua_gettop(L);
    if( lua_istable(L, u) ){
        lua_pushnil(L);
        while( lua_next(L, u) != 0 ){         // key, value
            lua_pop(L, 1);                    // key
            lua_pushvalue(L, -1);             // key, key (copy: lua_tostring would convert number keys in place)
            lua_pushnil(L);
            lua_rawset(L, g);                 // _G[key] = nil; key stays for lua_next
        }
    }
    lua_pushliteral(L, "_user");
    lua_newtable(L);
    lua_rawset(L, g);
    lua_settop(L, top);
}

int l_crowlib_crow_reset( lua_State* L ){
    // Optional debug: print Lua heap usage around reset.
    // Enable at runtime by setting: bb.debug_reset_mem = true
    bool debug_reset_mem = false;
    lua_getglobal(L, "bb");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "debug_reset_mem");
        debug_reset_mem = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (debug_reset_mem) {
        int kb = lua_gc(L, LUA_GCCOUNT, 0);
        int b = lua_gc(L, LUA_GCCOUNTB, 0);
        printf("[reset] lua heap before: %d.%03d KB\n", kb, b);
    }

    // Free the script's memory first: everything below calls into Lua, and
    // with the heap full (a script that ran out) those calls would fail
    // before the script's globals were cleared
    l_crowlib_clear_user_globals(L);
    lua_gc(L, LUA_GCCOLLECT, 0);

    S_reset();
    
    // Clean up C-side clock list to prevent "cant resume cancelled clock" errors
    ll_cleanup();

    // Cancel any scheduled clock wakeups and reset internal counters
    // (independent of Lua's clock.cleanup implementation)
    clock_cancel_coro_all();

    // Stop all metros at the C level to ensure no timer callbacks continue
    Metro_stop_all();
    
    // Clear any pending events (clock resumes, etc) that might trigger errors
    events_lockfree_clear();

    // Clean up Lua-side clock state (clears threads, resets tempo)
    lua_getglobal(L, "clock");
    if (!lua_isnil(L, -1)) {
        lua_getfield(L, -1, "cleanup");
        if (lua_isfunction(L, -1)) {
            lua_call(L, 0, 0);
        } else {
            lua_pop(L, 1); // pop non-function
        }

        // Hard reset clock tables to drop any lingering coroutine references.
        // Avoid relying on Lua-side iteration semantics during mutation.
        lua_newtable(L);
        lua_setfield(L, -2, "threads");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "id");
        lua_newtable(L);
        lua_setfield(L, -2, "transport");
    }
    lua_pop(L, 1); // pop clock or nil

    lua_getglobal(L, "input"); // @1
for(int i=1; i<=2; i++){
        lua_settop(L, 1); // _G.input is TOS @1
lua_pushinteger(L, i); // @2
lua_gettable(L, 1); // replace @2 with: input[n]

        // input[n].mode = 'none'
        lua_pushstring(L, "none"); // @3
        lua_setfield(L, 2, "mode"); // pops 'none' -> @2

        // input[n].reset_events(input[n]) -- aka void method call
        lua_getfield(L, 2, "reset_events"); // @3
        lua_pushvalue(L, 2); // @4 copy of input[n]
        lua_call(L, 1, 0);
}
    lua_settop(L, 0);

    // bb.pulsein[1/2] defaults: clear callbacks and modes to keep hardware safe
    lua_getglobal(L, "bb"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "pulsein"); // @2
        if(!lua_isnil(L, 2)){
            for(int i = 1; i <= 2; i++){
                lua_pushinteger(L, i); // @3
                lua_gettable(L, 2); // @3 = bb.pulsein[i]
                if(lua_isnil(L, 3)){
                    lua_settop(L, 2);
                    continue;
                }

                // pulsein[i].mode = 'none'
                lua_pushstring(L, "none");
                lua_setfield(L, 3, "mode");

                // pulsein[i].direction = 'both' (match crow.reset defaults)
                lua_pushstring(L, "both");
                lua_setfield(L, 3, "direction");

                // pulsein[i].division = 1 -- keeps default external clock div
                lua_pushnumber(L, 1.0);
                lua_setfield(L, 3, "division");

                // pulsein[i].change = nil (clears callback)
                lua_pushnil(L);
                lua_setfield(L, 3, "change");

                lua_settop(L, 2); // drop bb.pulsein[i]
            }
        }
    }
    lua_settop(L, 0);

    lua_getglobal(L, "output"); // @1
	for(int i=1; i<=4; i++){
        lua_settop(L, 1); // _G.output is TOS @1
		lua_pushinteger(L, i); // @2
		lua_gettable(L, 1); // replace @2 with: output[n]

        // output[n].slew = 0
        lua_pushnumber(L, 0.0); // @3
        lua_setfield(L, 2, "slew"); // pops 'slew' -> @2
        // output[n].volts = 0
        lua_pushnumber(L, 0.0); // @3
        lua_setfield(L, 2, "volts"); // pops 'volts' -> @2
        // output[n].scale('none')
        lua_getfield(L, 2, "scale");
        lua_pushstring(L, "none");
        lua_call(L, 1, 0);
        // output[n].done = function() end
        lua_getglobal(L, "nop_fn"); // @3
        lua_setfield(L, 2, "done"); // pops nop_fn -> @2
        
        // output[n].action = nil (clear ASL action)
        lua_pushnil(L);
        lua_setfield(L, 2, "action");

        // output[n]:clock('none')
        lua_getfield(L, 2, "clock"); // @3
        lua_pushvalue(L, 2); // @4 copy of output[n]
        lua_pushstring(L, "none");
        lua_call(L, 2, 0);

        // output[n].reset_events(output[n]) -- aka void method call
        lua_getfield(L, 2, "reset_events"); // @3
        lua_pushvalue(L, 2); // @4 copy of output[n]
        lua_call(L, 1, 0);
	}
	lua_settop(L, 0);

    // ii.reset_events(ii.self) - only if ii has been loaded. rawget: ii loads
    // on first use (_G's __index), and a plain read would load it here
    lua_pushglobaltable(L);
    lua_pushstring(L, "ii");
    lua_rawget(L, -2);
    lua_remove(L, 1); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "reset_events"); // @2
        if(!lua_isnil(L, 2)){
            lua_getfield(L, 1, "self"); // @3
            lua_call(L, 1, 0);
        }
        lua_settop(L, 1);
        // ii stays loaded across scripts (crow rebuilds Lua instead), so clear
        // what the script put in the device tables (ii.jf.event = ...): its
        // handlers would outlive it, calling into its cleared globals. Each
        // table's own field is only _name; the rest comes from its metatable.
        lua_pushnil(L);
        while(lua_next(L, 1) != 0){ // @2 key, @3 value
            if(lua_istable(L, 3)){
                lua_pushliteral(L, "_name");
                lua_rawget(L, 3);
                bool is_module = lua_isstring(L, -1);
                lua_pop(L, 1);
                if(is_module){
                    lua_pushnil(L);
                    while(lua_next(L, 3) != 0){ // @4 key, @5 value
                        lua_pop(L, 1);
                        bool keep = lua_type(L, 4) == LUA_TSTRING
                                 && strcmp(lua_tostring(L, 4), "_name") == 0;
                        if(!keep){
                            lua_pushvalue(L, 4);
                            lua_pushnil(L);
                            lua_rawset(L, 3); // module[key] = nil (existing key: allowed mid-traversal)
                        }
                    }
                }
            }
            lua_settop(L, 2); // keep the key for lua_next
        }
    }
    lua_settop(L, 0);

    // ii.self defaults (only if ii has been loaded)
    l_crowlib_ii_follow_defaults(L);

    // ii_follow_reset() -- resets forwarding to output libs (only if exists)
    lua_getglobal(L, "ii_follow_reset");
    if(!lua_isnil(L, 1)){
        lua_call(L, 0, 0);
    }
    lua_settop(L, 0);

    // metro.free_all() - only if metro exists
    lua_getglobal(L, "metro"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "free_all");
        if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }

        // Also call metro.reset() (if present) to clear event closures and defaults.
        // metro.free_all() stops metros but does not clear Metro.metros[i].event.
        lua_getfield(L, 1, "reset");
        if (lua_isfunction(L, -1)) {
            lua_call(L, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_settop(L, 0);

    // if public then public.clear() end
    lua_getglobal(L, "public"); // @1
    if(!lua_isnil(L, 1)){ // if public is not nil
    	lua_getfield(L, 1, "clear");
    	if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }
    }
    lua_settop(L, 0);

    // hotswap.cleanup() - only if hotswap has been loaded. rawget: hotswap
    // loads on first use (_G's __index), and a plain read would load it here
    lua_pushglobaltable(L);
    lua_pushstring(L, "hotswap");
    lua_rawget(L, -2);
    lua_remove(L, 1); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "cleanup");
        if(!lua_isnil(L, 2)){
            lua_call(L, 0, 0);
        }
    }
    lua_settop(L, 0);

    // bb.pulseout[1]:low() and bb.pulseout[2]:low() - reset pulse outputs to low
    lua_getglobal(L, "bb"); // @1
    if(!lua_isnil(L, 1)){
        lua_getfield(L, 1, "pulseout"); // @2
        if(!lua_isnil(L, 2)){
            for(int i = 1; i <= 2; i++){
                lua_pushinteger(L, i); // @3
                lua_gettable(L, 2); // @3 = bb.pulseout[i]
                if(!lua_isnil(L, 3)){
                    lua_getfield(L, 3, "low"); // @4 = bb.pulseout[i].low
                    if(!lua_isnil(L, 4)){
                        lua_pushvalue(L, 3); // @5 = self (bb.pulseout[i])
                        lua_call(L, 1, 0); // bb.pulseout[i]:low()
                    }
                }
                lua_settop(L, 2); // pop back to bb.pulseout
            }
        }
    }
    lua_settop(L, 0);

    // bb.asap = nil - clear user-defined high-frequency callback
    lua_getglobal(L, "bb"); // @1
    if(!lua_isnil(L, 1)){
        lua_pushnil(L); // @2
        lua_setfield(L, 1, "asap"); // bb.asap = nil
    }
    lua_settop(L, 0);

    // Clear callback globals that are installed via the C API (lua_setglobal) and therefore
    // bypass the _G __newindex tracer used for _user tracking.
    // If left uncleared, these can retain closures (and their upvalues) across script runs.
    lua_pushnil(L);
    lua_setglobal(L, "_switch_change_callback");
    lua_pushnil(L);
    lua_setglobal(L, "_pulsein1_change_callback");
    lua_pushnil(L);
    lua_setglobal(L, "_pulsein2_change_callback");
    lua_settop(L, 0);

    // Unload the libraries that load on first use (main.cpp, lazy_lib_index:
    // ~31KB together), so each script starts with the heap of a fresh boot:
    // otherwise any script that used them left them loaded, and a big one
    // after it (nb_drumcrow's) ran out of memory compiling. They load again
    // when a script uses them. ii's global handlers go too, as they'd keep it
    // alive: the follower reads `ii` (loading it) before calling them, and a
    // leader reply finding no handler is dropped. rawset: no _user tracer.
    {
        static const char* const unload[] = { "sequins", "timeline", "hotswap", "ii"
                                             , "ii_LeadRx_handler", "ii_followRx_handler"
                                             , "ii_followRxTx_handler" };
        lua_pushglobaltable(L);
        for (size_t i = 0; i < sizeof(unload) / sizeof(unload[0]); i++) {
            lua_pushstring(L, unload[i]);
            lua_pushnil(L);
            lua_rawset(L, -3);
        }
        lua_settop(L, 0);
    }

    // Clear user globals again: any the module resets above made
    l_crowlib_clear_user_globals(L);

    // Force garbage collection to reclaim memory from previous script
    // Do two full cycles (mirrors existing reload behavior elsewhere).
    lua_gc(L, LUA_GCCOLLECT, 0);
    lua_gc(L, LUA_GCCOLLECT, 0);

    if (debug_reset_mem) {
        int kb = lua_gc(L, LUA_GCCOUNT, 0);
        int b = lua_gc(L, LUA_GCCOUNTB, 0);
        printf("[reset] lua heap after:  %d.%03d KB\n", kb, b);
    }

    return 0;
}

static int justvolts(lua_State* L, float mul);

int l_crowlib_justvolts(lua_State* L){
	return justvolts(L, 1.f);
}

int l_crowlib_just12(lua_State *L){
	return justvolts(L, 12.f);
}

int l_crowlib_hztovolts(lua_State *L){
	// assume numbers, not tables
	float retval = 0.f;
	switch(lua_gettop(L)){
		case 1: // use default middleC reference
			// note we 
			retval = log2f(luaL_checknumber(L, 1) * L_CL_MIDDLEC_INV);
			break;
		case 2: // use provided reference
			retval = log2f(luaL_checknumber(L, 1)/luaL_checknumber(L, 2));
			break;
		default:
			lua_pushliteral(L, "need 1 or 2 args");
			lua_error(L);
			break;
	}
    lua_settop(L, 0);
	lua_pushnumber(L, retval);
	return 1;
}

static int justvolts(lua_State* L, float mul){
	// apply optional offset
	float offset = 0.f;
	switch(lua_gettop(L)){
		case 1: break;
		case 2: {offset = log2f(luaL_checknumber(L, 2))*mul;} break;
		default:
			lua_pushliteral(L, "need 1 or 2 args");
			lua_error(L);
			break;
	}

	// now do the conversion
	int nresults = 0;
	switch(lua_type(L, 1)){
		case LUA_TNUMBER:{
			float result = log2f(lua_tonumber(L, 1))*mul + offset;
			lua_settop(L, 0);
			lua_pushnumber(L, result);
			nresults = 1;
			break;}
		case LUA_TTABLE:{
			// get length of table to convert
			lua_len(L, 1);
			int telems = lua_tonumber(L, -1);
			lua_pop(L, 1);

			// build the new table in C (a copy)
			float newtab[telems+1]; // bottom element is unused
			for(int i=1; i<=telems; i++){
				lua_geti(L, 1, i);
				newtab[i] = log2f(luaL_checknumber(L, -1))*mul + offset;
				lua_pop(L, 1); // pops the number from the stack
			}

			// push the C table into the lua table
			lua_settop(L, 0);
			lua_createtable(L, telems, 0);
			for(int i=1; i<=telems; i++){
				lua_pushnumber(L, newtab[i]);
				lua_seti(L, 1, i);
			}
			nresults = 1;
			break;}
		default:
			lua_pushliteral(L, "unknown voltage type");
			lua_error(L);
			break;
	}
	return nresults;
}

/// true random

static int _random_arity_n( lua_State* L )
{
    int nargs = lua_gettop(L);
    switch(nargs){
        case 0:{
            float r = Random_Float();
            lua_settop(L, 0);
            lua_pushnumber(L, r);
            break;}
        case 1:{
            int r = Random_Int(1, luaL_checknumber(L, 1));
            lua_settop(L, 0);
            lua_pushinteger(L, r);
            break;}
        default:{
            int r = Random_Int(luaL_checknumber(L, 1)
                              ,luaL_checknumber(L, 2));
            lua_settop(L, 0);
            lua_pushinteger(L, r);
            break;}
    }
    return 1;
}

// ii follower default actions

// function(chan,val) output[chan].volts = val end
static int _ii_self_volts( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float val = luaL_checknumber(L, 2);
	lua_settop(L, 0);
	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2
	lua_pushnumber(L, val); // 3
	lua_setfield(L, 2, "volts");
	lua_settop(L, 0);
	return 0;
}

// function(chan,val) output[chan].volts = val end
static int _ii_self_slew( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float slew = luaL_checknumber(L, 2);
	lua_settop(L, 0);
	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2
	lua_pushnumber(L, slew); // 3
	lua_setfield(L, 2, "slew");
	lua_settop(L, 0);
	return 0;
}

// function() crow.reset() end
static int _ii_self_reset( lua_State* L ){
	lua_getglobal(L, "crow"); // 1
	lua_getfield(L, 1, "reset");
	lua_call(L, 0, 0);
	lua_settop(L, 0);
	return 0;
}

// function(chan,ms,volts,pol) output[chan](pulse(ms,volts,pol)) end
static int _ii_self_pulse( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float ms = luaL_checknumber(L, 2);
	float volts = luaL_checknumber(L, 3);
	float pol = luaL_checknumber(L, 4);
	lua_settop(L, 0);

	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2

	lua_getglobal(L, "pulse"); // 3
	lua_pushnumber(L, ms);
	lua_pushnumber(L, volts);
	lua_pushnumber(L, pol);
	lua_call(L, 3, 1); // calls 'ramp' and leaves asl table @3
	lua_call(L, 1, 0); // calls output[chan]({asl-table})
	lua_settop(L, 0);
	return 0;
}

// function(chan,atk,rel,volts) output[chan](ar(atk,rel,volts)) end
static int _ii_self_ar( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float atk = luaL_checknumber(L, 2);
	float rel = luaL_checknumber(L, 3);
	float volts = luaL_checknumber(L, 4);
	lua_settop(L, 0);

	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2

	lua_getglobal(L, "ar"); // 3
	lua_pushnumber(L, atk);
	lua_pushnumber(L, rel);
	lua_pushnumber(L, volts);
	lua_call(L, 3, 1); // calls 'ar' and leaves asl table @3
	lua_call(L, 1, 0); // calls output[chan]({asl-table})
	lua_settop(L, 0);
	return 0;
}


// -- convert freq to seconds where freq==0 is 1Hz
// function(chan,freq,level,skew) output[chan](ramp(math.pow(2,-freq),skew,level)) end
static int _ii_self_lfo( lua_State* L ){
	int chan = luaL_checknumber(L, 1);
	float freq = luaL_checknumber(L, 2);
	float level = luaL_checknumber(L, 3);
	float skew = luaL_checknumber(L, 4);
	lua_settop(L, 0);

	lua_getglobal(L, "output"); // 1
	lua_pushnumber(L, chan); // 2
	lua_gettable(L, -2); // output[chan] onto stack @2

	lua_getglobal(L, "ramp"); // 3
	lua_pushnumber(L, powf(2.0, -freq));
	lua_pushnumber(L, skew);
	lua_pushnumber(L, level);
	lua_call(L, 3, 1); // calls 'ramp' and leaves asl table @3
	lua_call(L, 1, 0); // calls output[chan]({asl-table})
	lua_settop(L, 0);
	return 0;
}

// ii.self's default actions (crow's: volts, slew, ... act on the outputs).
// Only if ii is loaded (it loads on first use, and installs these then).
void l_crowlib_ii_follow_defaults( lua_State* L ){
	int top = lua_gettop(L);
	lua_pushglobaltable(L);
	lua_pushliteral(L, "ii");
	lua_rawget(L, -2);
	if( lua_istable(L, -1) ){
		lua_settop(L, top);
		lua_pushcfunction(L, _ii_follow_reset);
		lua_call(L, 0, 0);
	}
	lua_settop(L, top);
}

static int _ii_follow_reset( lua_State* L ){
	lua_settop(L, 0);
	lua_getglobal(L, "ii"); // @1
	lua_getfield(L, 1, "self"); // @2

	lua_pushcfunction(L, _ii_self_volts); // @3
	lua_setfield(L, 2, "volts");
	lua_pushcfunction(L, _ii_self_slew);
	lua_setfield(L, 2, "slew");
	lua_pushcfunction(L, _ii_self_reset);
	lua_setfield(L, 2, "reset");
	lua_pushcfunction(L, _ii_self_pulse);
	lua_setfield(L, 2, "pulse");
	lua_pushcfunction(L, _ii_self_ar);
	lua_setfield(L, 2, "ar");
	lua_pushcfunction(L, _ii_self_lfo);
	lua_setfield(L, 2, "lfo");

	lua_settop(L, 0);
	return 0;
}


// C.tell( 'output', channel, get_state( channel ))
static int _tell_get_out( lua_State* L ){
	int chan = luaL_checknumber(L, -1);
    Caw_printf( "^^output(%i,%f)", chan, (float)AShaper_get_state(chan-1));
    lua_settop(L, 0);
    return 0;
}

// C.tell( 'stream', channel, io_get_input( channel ))
static int _tell_get_cv( lua_State* L ){
int chan = luaL_checknumber(L, -1);
    Caw_printf( "^^stream(%i,%f)", chan, get_input_state_simple(chan-1));
    lua_settop(L, 0);
    return 0;
}

// Lock-free metro queuing function
// Runs on core 1, so must stay in RAM and must not printf.
void __not_in_flash_func(L_queue_metro)( int id, int state )
{
    // Queue via lock-free system (never blocks Core 1)
    if (!metro_lockfree_post(id, state)) {
        // Queue full - drop event (should never happen with proper queue sizing)
        static volatile uint32_t drop_count = 0;
        drop_count++;
    }
}

// =============================================================================
// Diagnostics for metro and clock callbacks
// =============================================================================
static uint32_t g_metro_cb_worst_us = 0;
static uint32_t g_metro_cb_last_us = 0;
static uint32_t g_metro_cb_overruns = 0;

uint32_t metro_cb_worst_us(void)      { return g_metro_cb_worst_us; }
uint32_t metro_cb_last_us(void)       { return g_metro_cb_last_us; }
uint32_t metro_cb_overrun_count(void) { return g_metro_cb_overruns; }
void metro_cb_reset_stats(void) {
    g_metro_cb_worst_us = 0;
    g_metro_cb_last_us = 0;
    g_metro_cb_overruns = 0;
}

static uint32_t g_clock_resume_cb_worst_us = 0;
static uint32_t g_clock_resume_cb_last_us = 0;

uint32_t clock_resume_cb_worst_us(void) { return g_clock_resume_cb_worst_us; }
uint32_t clock_resume_cb_last_us(void)  { return g_clock_resume_cb_last_us; }
void clock_resume_cb_reset_stats(void) {
    g_clock_resume_cb_worst_us = 0;
    g_clock_resume_cb_last_us = 0;
}

// Forward declarations for output batching (defined in main.cpp)
extern void output_batch_begin(void);
extern void output_batch_flush(void);

// New lock-free metro handler - processes events from lock-free queue
void L_handle_metro_lockfree( metro_event_lockfree_t* event )
{
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_metro_lockfree: no Lua state available\n");
        return;
    }
    
    // ===============================================
    // OPTIMIZATION 2: Enable batching for metro callbacks
    // ===============================================
    output_batch_begin();
    
    int metro_id = event->metro_id;
    int stage = event->stage;
    
    // Call the global metro_handler function in Lua like real crow
    uint32_t start_us = time_us_32();

    lua_getglobal(L, "metro_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, metro_id + 1);  // First argument: metro ID (Lua's metro[1] is C's 0)
        lua_pushinteger(L, stage);     // Second argument: stage/count
        
        // Protected call to prevent crashes
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("metro_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        // metro_handler not defined - this is normal if no metros are active
        lua_pop(L, 1);
    }
    
    // ===============================================
    // OPTIMIZATION 2: Flush batched outputs
    // ===============================================
    output_batch_flush();

    // Diagnostics: measure callback duration and flag overruns
    uint32_t elapsed_us = time_us_32() - start_us;
    g_metro_cb_last_us = elapsed_us;
    if (elapsed_us > g_metro_cb_worst_us) {
        g_metro_cb_worst_us = elapsed_us;
    }

    // Overrun detection: compare to configured metro period if available
    float period_s = Metro_get_period_seconds(metro_id);
    if (period_s > 0.0f) {
        uint32_t period_us = (uint32_t)(period_s * 1e6f + 0.5f);
        if (elapsed_us > period_us) {
            g_metro_cb_overruns++;
        }
    }
}

// Runs on core 1, so must stay in RAM and must not printf.
void __not_in_flash_func(L_queue_clock_resume)( int coro_id )
{
    if (!clock_lockfree_post(coro_id)) {
        // Queue full - drop event (should never happen with proper queue sizing)
        static volatile uint32_t drop_count = 0;
        drop_count++;
    }
}

// L_queue_clock_start and L_queue_clock_stop removed - clock start/stop now use direct calls
// (Core 0 -> Core 0, no inter-core communication needed)

static void handle_clock_resume_common(int coro_id) {
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();

    if (!L) {
        printf("L_handle_clock_resume: no Lua state available\n");
        return;
    }

    uint32_t start_us = time_us_32();

    lua_getglobal(L, "clock_resume_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, coro_id);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("clock_resume_handler error: %s\n", error ? error : "unknown");
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }

    uint32_t elapsed_us = time_us_32() - start_us;
    g_clock_resume_cb_last_us = elapsed_us;
    if (elapsed_us > g_clock_resume_cb_worst_us) {
        g_clock_resume_cb_worst_us = elapsed_us;
    }
}

void L_handle_clock_resume_lockfree(clock_event_lockfree_t* event)
{
    handle_clock_resume_common(event->coro_id);
}

void L_handle_asl_done_lockfree( asl_done_event_lockfree_t* event )
{
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    
    if (!L) {
        printf("L_handle_asl_done_lockfree: no Lua state available\n");
        return;
    }
    
    int channel = event->channel + 1; // Convert to 1-based for Lua
    
    // Crow-style ASL completion callback: output[channel].done(), if set.
    // Called directly rather than by compiling a chunk: that cost ~8ms per
    // completion on RP2040 (mostly GC work triggered by the compiler's
    // allocations), capping the main loop near 90Hz whenever outputs changed.
    int top = lua_gettop(L);
    lua_getglobal(L, "output");
    if (lua_istable(L, -1)) {
        lua_geti(L, -1, channel);
        if (lua_toboolean(L, -1)) {
            lua_getfield(L, -1, "done");
            if (lua_toboolean(L, -1) && lua_pcall(L, 0, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                printf("ASL done callback error: %s\n", error ? error : "unknown");
            }
        }
    }
    lua_settop(L, top);
}
