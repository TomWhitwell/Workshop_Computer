// ii as crow does it, ported from crow (monome/crow lib/ii.c, GPL-3.0):
// leader (ii.jf.*, ii.raw, ...) and follower (answering at one of crow's
// addresses, as ii.self). The bus itself is ii_bus.c, values ii_codec.c.

#include "ii.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ii_c_layer.h"  // generated from lib/ii/*.lua (CMakeLists.txt)
#include "ii_codec.h"
#include "ii_bus.h"
#include "caw.h"         // Caw_printf
#include "hardware/sync.h"
#include "../lua/src/lua.h"

#define II_MAX_BROADCAST_LEN 9 // cmd byte + 4*s16 args
#define II_MAX_RECEIVE_LEN 10
#define II_QUEUE_LENGTH 16

typedef struct{
    uint8_t address;
    uint8_t length;
    uint8_t query_length; // 0 for broadcast, >0 is return type byte size
    uint8_t data[II_MAX_BROADCAST_LEN];
    uint8_t arg; // carried through to the reply event
    bool is_raw;
} ii_q_t;

static uint8_t encode_packet( uint8_t* dest, const ii_Cmd_t* c, uint8_t cmd, float* data );
static int follow_request( const uint8_t* msg, uint8_t len, uint8_t* reply );

// Leader queue (ring buffer). In flight: the message at the head, until the
// driver reports how it went.
static ii_q_t   l_iq[II_QUEUE_LENGTH];
static uint8_t  l_head = 0, l_count = 0;
static bool     in_flight = false;
static bool     retrying = false;
// ii.address: 1-4, crow's addresses 0x01-0x04. 4 by default: a real crow
// (1) can share the bus, and Teletype reaches us with CROW.SEL 4
static uint8_t  follower_index = 4;

// How each transaction ended, by ii_bus_status_t (for tests and diagnosis)
volatile uint32_t g_ii_results[6];


////////////////////////
// setup

void ii_init( void ){
    // the UART pads, where ComputerCard put I2C0 (COMPUTERCARD_II_ON_UART_PINS)
    const ii_bus_config_t bus = { .i2c = i2c0, .sda_pin = 0, .scl_pin = 1 };
    ii_bus_init(&bus);
    l_head = l_count = 0;
    in_flight = retrying = false;
    ii_bus_follow( follower_index, follow_request );
}
const char* ii_list_modules( void ){
    return ii_module_list;
}
const char* ii_list_cmds( uint8_t address ){
    return ii_list_commands(address);
}
void ii_set_pullups( uint8_t state ){
    ii_bus_set_pullups(state != 0); // the RP2040's weak internal pull-ups only
}
void ii_set_fastmode( uint8_t state ){
    ii_bus_set_fast(state != 0);
}
uint8_t ii_get_address( void ){
    return follower_index;
}
void ii_set_address( uint8_t index ){
    follower_index = (index >= 1 && index <= 4) ? index : 4;
    ii_bus_follow( follower_index, follow_request );
}


////////////////////////////////
// leader commands

static ii_q_t* queue_push( void ){
    if( l_count >= II_QUEUE_LENGTH ){ return NULL; }
    ii_q_t* q = &l_iq[(l_head + l_count) % II_QUEUE_LENGTH];
    l_count++;
    return q;
}
static void queue_pop( void ){
    if( l_count ){
        l_head = (l_head + 1) % II_QUEUE_LENGTH;
        l_count--;
    }
}

uint8_t ii_leader_enqueue( uint8_t address
                         , uint8_t cmd
                         , float*  data
                         )
{
    const ii_Cmd_t* c = ii_find_command(address, cmd);
    if( c == NULL ){ printf("ii: unknown command\n"); return 1; }
    ii_q_t* q = queue_push();
    if( q == NULL ){ Caw_printf("ii: queue full"); return 1; }

    q->is_raw = false;
    q->address = address;
    q->query_length = ii_type_size( c->return_type );
    q->arg = data[0]; // save a copy of the first argument
    q->length = encode_packet( q->data, c, cmd, data );
    ii_pickle( &q->address, q->data, &q->length );
    return 0;
}

uint8_t ii_leader_enqueue_bytes( uint8_t  address
                               , uint8_t* data
                               , uint8_t  tx_len
                               , uint8_t  rx_len
                               )
{
    ii_q_t* q = queue_push();
    if( q == NULL ){ Caw_printf("ii: queue full"); return 1; }

    if( tx_len > II_MAX_BROADCAST_LEN ){ tx_len = II_MAX_BROADCAST_LEN; }
    if( rx_len > II_MAX_RECEIVE_LEN ){ rx_len = II_MAX_RECEIVE_LEN; }
    if( tx_len + rx_len > II_BUS_MAX_BYTES ){ rx_len = II_BUS_MAX_BYTES - tx_len; }

    q->is_raw = true;
    q->address = address;
    q->arg = tx_len > 1 ? data[1] : 0;
    q->query_length = rx_len;
    q->length = tx_len;
    memcpy( q->data, data, tx_len );
    return 0;
}

// A reply arrived: hand it to Lua (ii.lua: ii_LeadRx_handler -> ii.<dev>.event)
static void lead_callback( ii_q_t* q, uint8_t* rx_data )
{
    uint8_t address = q->address;
    uint8_t command = q->data[0];
    if( !q->is_raw ){ ii_unpickle( &address, &command, rx_data ); }
    ii_Type_t return_type = ii_s32T;
    const ii_Cmd_t* cmd = ii_find_command(address, command);
    if( cmd != NULL ){
        return_type = cmd->return_type;
    }
    float value = ii_decode( rx_data, return_type );

    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    if( L == NULL ){ return; }
    lua_getglobal(L, "ii_LeadRx_handler");
    if( lua_type(L, -1) != LUA_TFUNCTION ){ lua_pop(L, 1); return; }
    lua_pushinteger(L, address);
    lua_pushinteger(L, command);
    lua_pushinteger(L, q->arg);
    lua_pushnumber(L, value);
    if( lua_pcall(L, 4, 0, 0) != LUA_OK ){
        Caw_printf("lua runtime error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void ii_leader_process( void )
{
    uint8_t rx[II_BUS_MAX_BYTES];

    if( in_flight ){
        ii_bus_status_t s = ii_bus_poll(rx);
        if( s == II_BUS_BUSY ){ return; }
        in_flight = false;
        if( (unsigned)s < 6 ){ g_ii_results[s] = g_ii_results[s] + 1; }
        ii_q_t* q = &l_iq[l_head];
        switch( s ){
            case II_BUS_DONE:
                retrying = false;
                if( q->query_length ){ lead_callback( q, rx ); }
                queue_pop();
                break;
            case II_BUS_NAK: // nobody at that address: drop it quietly, as crow does
                retrying = false;
                queue_pop();
                break;
            default: // timeout or bus error: retry once, then abandon
                if( retrying ){
                    retrying = false;
                    queue_pop();
                    Caw_printf( (s == II_BUS_TIMEOUT)
                        ? "ii: bus stuck or lines low. check ii devices and connections."
                        : "ii: bus error." );
                } else {
                    retrying = true;
                }
                break;
        }
        return; // start the next one on the next pass
    }

    if( l_count == 0 || !ii_bus_idle() ){ return; }
    ii_q_t* q = &l_iq[l_head];
    if( ii_bus_start( q->address, q->data, q->length, q->query_length ) ){
        in_flight = true;
    }
}



static uint8_t encode_packet( uint8_t* dest
                            , const ii_Cmd_t* c
                            , uint8_t cmd
                            , float* data
                            )
{
    uint8_t len = 0;
    dest[len++] = cmd; // first byte is command
    for( int i=0; i<(c->args); i++ ){
        len += ii_encode( &dest[len], c->argtype[i], *data++ );
    }
    return len;
}


////////////////////////////////////////////
// follower: answering as crow (ii.self)

// blackbird's input and output voltages, for the built-in getters (main.cpp).
// Called from the I2C interrupt: they only read cached values.
extern float bb_ii_input_volts( int index );   // 0-based
extern float bb_ii_output_volts( int index );

#define II_CROW_GET_INPUT  (II_GET_OFFSET + 3)
#define II_CROW_GET_OUTPUT (II_GET_OFFSET + 4)

// How follower messages were handled (for tests and diagnosis):
// commands, queries answered in the interrupt, queries answered by Lua,
// two-step getters, unknown commands
volatile uint32_t g_ii_follow_stats[5];

// A getter that Lua must answer, held (bus stretched) for the main loop
static uint8_t       query_msg[16];
static volatile uint8_t query_len = 0;

static const ii_Cmd_t* follower_command( uint8_t cmd ){
    return ii_find_command( follower_index, cmd ); // crow's descriptor
}

static void decode_args( const ii_Cmd_t* c, const uint8_t* data, float* args ){
    int pos = 0;
    for( int i = 0; i < c->args; i++ ){
        args[i] = ii_decode( &data[pos], c->argtype[i] );
        pos += ii_type_size( c->argtype[i] );
    }
}

// The built-in getters (input, output): no Lua, so fine in the interrupt.
// Encoded answer length, or -1 if it isn't one of them.
static int builtin_get( const ii_Cmd_t* c, const uint8_t* msg, uint8_t* reply ){
    if( c->cmd != II_CROW_GET_INPUT && c->cmd != II_CROW_GET_OUTPUT ){ return -1; }
    float args[4] = {0};
    decode_args( c, &msg[1], args );
    int ch = (int)args[0] - 1; // ii is 1-based
    float v = (c->cmd == II_CROW_GET_INPUT) ? bb_ii_input_volts(ch) : bb_ii_output_volts(ch);
    return ii_encode( reply, c->return_type, v );
}

// I2C interrupt: a leader is reading from us. `msg` is the getter it wrote,
// in this transfer or (Teletype's two-step) just before
static int follow_request( const uint8_t* msg, uint8_t len, uint8_t* reply ){
    if( len == 0 ){ return 0; }
    const ii_Cmd_t* c = follower_command( msg[0] );
    if( c == NULL || c->cmd < II_GET_OFFSET ){ g_ii_follow_stats[4]++; return 0; }
    int n = builtin_get( c, msg, reply );
    if( n >= 0 ){ g_ii_follow_stats[1]++; return n; }
    memcpy( query_msg, msg, len ); // Lua answers, from the main loop
    query_len = len;
    return II_BUS_DEFER;
}

static bool lua_follower_ready( lua_State* L ){
    // ii.lua loads on first use: reading `ii` loads it (and its handlers)
    lua_getglobal( L, "ii" );
    bool ok = lua_istable( L, -1 );
    lua_pop( L, 1 );
    return ok;
}

// ii.self.<name>(args) for a command
static void lua_follower_command( const ii_Cmd_t* c, const uint8_t* msg ){
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    if( L == NULL || !lua_follower_ready( L ) ){ return; }
    float args[4] = {0};
    decode_args( c, &msg[1], args );
    lua_getglobal( L, "ii_followRx_handler" );
    lua_pushinteger( L, c->cmd );
    for( int i = 0; i < c->args; i++ ){ lua_pushnumber( L, args[i] ); }
    if( lua_pcall( L, 1 + c->args, 0, 0 ) != LUA_OK ){
        Caw_printf( "lua runtime error: %s", lua_tostring( L, -1 ) );
        lua_pop( L, 1 );
    }
}

// A getter's answer: built in, or ii.self.query (Lua). Encoded length.
static int follower_get( const ii_Cmd_t* c, const uint8_t* msg, uint8_t* reply ){
    int n = builtin_get( c, msg, reply );
    if( n >= 0 ){ return n; }
    float value = 0.0f;
    extern lua_State* get_lua_state(void);
    lua_State* L = get_lua_state();
    if( L != NULL && lua_follower_ready( L ) ){
        float args[4] = {0};
        decode_args( c, &msg[1], args );
        lua_getglobal( L, "ii_followRxTx_handler" );
        lua_pushinteger( L, c->cmd );
        for( int i = 0; i < c->args; i++ ){ lua_pushnumber( L, args[i] ); }
        if( lua_pcall( L, 1 + c->args, 1, 0 ) != LUA_OK ){
            Caw_printf( "lua runtime error: %s", lua_tostring( L, -1 ) );
        } else if( lua_isnumber( L, -1 ) ){
            value = (float)lua_tonumber( L, -1 );
        }
        lua_pop( L, 1 );
    }
    return ii_encode( reply, c->return_type, value );
}

void ii_follower_process( void ){
    uint8_t reply[II_BUS_MAX_BYTES];

    // A getter written and read in one transfer, held for Lua's answer
    if( query_len ){
        const ii_Cmd_t* c = follower_command( query_msg[0] );
        int n = c ? follower_get( c, query_msg, reply ) : 0;
        query_len = 0;
        g_ii_follow_stats[2]++;
        ii_bus_reply( reply, (uint8_t)n );
    }

    uint8_t msg[II_BUS_MAX_BYTES];
    uint8_t len;
    while( (len = ii_bus_receive( msg )) ){
        const ii_Cmd_t* c = follower_command( msg[0] );
        if( c == NULL ){ g_ii_follow_stats[4]++; continue; }
        if( c->cmd < II_GET_OFFSET ){
            g_ii_follow_stats[0]++;
            lua_follower_command( c, msg );
        } else {
            // a getter written on its own: the read that follows is answered
            // from it (follow_request), so nothing to do here
            g_ii_follow_stats[3]++;
        }
    }
}
