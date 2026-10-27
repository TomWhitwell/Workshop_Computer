#pragma once

#include <stdbool.h>

// lua libs for destructuring lua tables in C
#include "../lua/src/lua.h"
#include "../lua/src/lauxlib.h"
#include "../lua/src/lualib.h"

#include "slopes.h" // S_toward, Q16 types

#define TO_COUNT   16   // 28bytes
#define SEQ_COUNT  8    // 16bytes
#define SEQ_LENGTH 8    // 4bytes
#define DYN_COUNT  40   // 8bytes

typedef enum{ ToLiteral
            , ToRecur
            , ToIf
            , ToEnter
            , ToHeld
            , ToWait
            , ToUnheld
            , ToLock
            , ToOpen
} ToControl;

typedef union{
    float   f;       // a number: volts, seconds, or anything ASL computes
    int     dyn;     // index into the dynamics array
    uint16_t var[2]; // 2 indexes into dynamic table
    int     seq;     // reference to a Sequence object
    Shape_t shape;
} ElemO; // 4bytes

typedef enum{ ElemT_Float   // float, as crow: ASL runs on core 0, which can afford it. As
                            // Q16.16, times were rounded to 15us: a 1kHz lfo's 0.5ms
                            // segments became 0.5035ms (0.7% flat), 4kHz ones 2.3% sharp
            , ElemT_Shape
            , ElemT_Dynamic
            , ElemT_Mutable
        // arithmetic ops
            , ElemT_Negate
            , ElemT_Add
            , ElemT_Sub
            , ElemT_Mul
            , ElemT_Div
            , ElemT_Mod
            , ElemT_Mutate
} ElemT;

typedef struct{
    ElemO obj;
    ElemT type;
} Elem; // 8bytes

typedef struct{
    Elem a;
    Elem b;
    Elem c;
    ToControl ctrl;
} To; // 28bytes

typedef struct{
    To* stage[SEQ_LENGTH];
    int length;
    int pc;
    int parent; // seq_ix, like a tree 'parent' link
} Sequence;

// TODO rename 'dynamics' to 'elements' and use it as a general abstraction (eg with To*)
typedef struct{
    To tos[TO_COUNT];
    int to_ix;

    Sequence* seq_current;
    Sequence seqs[SEQ_COUNT];
    int seq_ix;
    int seq_select;

    Elem dynamics[DYN_COUNT];
    int dyn_ix;

    bool holding;
    bool locked;

    // Slopes sent to the slope engine (see casl.c, next_action)
    uint16_t seq_sent;     // tag of the last one sent (started or queued)
    bool queued_valid;     // one is queued to follow the current slope...
    uint16_t queued_seq;   // ...tagged this,
    bool queued_lookahead; // and will want the one after queued once it starts,
    bool queued_done;      // or ends the sequence (asl done when it starts)
} Casl;

Casl* casl_init( int index );
void casl_describe( int index, lua_State* L );
bool casl_describing( void );        // casl_describe is parsing (see casl.c)
void casl_describing_clear( void );
void casl_describe_to_literal( int index, float volts, float seconds, Shape_t shape );
void casl_action( int index, int action );

// dynamic vars
int casl_defdynamic( int index );
void casl_cleardynamics( int index );
void casl_setdynamic( int index, int dynamic_ix, float val );
float casl_getdynamic( int index, int dynamic_ix );
