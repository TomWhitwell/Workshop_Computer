#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum{ CLOCK_SOURCE_INTERNAL = 0
            , CLOCK_SOURCE_MIDI     = 1
            , CLOCK_SOURCE_LINK     = 2
            , CLOCK_SOURCE_CROW     = 3
            , CLOCK_SOURCE_LIST_LENGTH
} clock_source_t;

void clock_init( int max_clocks );

// MUST ONLY BE CALLED WHEN time_now CHANGES
// Designed to be called on a 1ms tick.
void clock_update(uint32_t time_now);

bool clock_schedule_resume_sleep( int coro_id, float seconds );
bool clock_schedule_resume_sync( int coro_id, float beats );
bool clock_schedule_resume_beatsync( int coro_id, float beats );
void clock_update_reference( float beats, float beat_duration );
void clock_update_reference_from( float beats, float beat_duration, clock_source_t source);
void clock_start_from( clock_source_t source );
void clock_stop_from( clock_source_t source );
void clock_set_source( clock_source_t source );
void clock_set_source_manual(clock_source_t source);
void clock_set_source_auto_mode(void);
clock_source_t clock_get_source(void);
bool clock_is_source_manual(void);

float clock_get_time_beats(void);
float clock_get_time_seconds(void);
float clock_get_tempo(void);

void clock_cancel_coro( int coro_id );
void clock_cancel_coro_all( void );

// Stats
uint32_t clock_get_schedule_failures(void);
uint32_t clock_get_schedule_successes(void);
uint32_t clock_get_max_active_threads(void);
uint32_t clock_get_pool_capacity(void);
void clock_reset_stats(void);

// Sample-based timing functions for improved precision
void clock_set_sample_counter(uint64_t samples);
uint64_t clock_get_sample_counter(void);
void clock_increment_sample_counter(void);
uint32_t clock_ms_since_boot(void);


///////////////////////////////////
// internal

void clock_internal_init(void);
void clock_internal_set_tempo( float bpm );
void clock_internal_start( float new_beat, bool transport_start );
void clock_internal_stop(void);


///////////////////////////////////
// input

// TODO add arg to choose input channel
void clock_crow_init(void);
void clock_input_handler( int id, float freq ); // Called from Detect lib
void clock_crow_handle_clock(void);
void clock_crow_in_div( float div );
