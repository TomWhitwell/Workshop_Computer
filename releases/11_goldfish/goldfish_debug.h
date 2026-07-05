/*
 * goldfish_debug.h — compile-time switch for diagnostic instrumentation.
 *
 * The performance/health counters (ProcessSample timing, record encode-loop
 * timing, page-ring occupancy/drops, head underruns, ADPCM decoder peak, ...)
 * are read live over GDB during development. They add code and a little runtime
 * cost, so they are gated behind GOLDFISH_DEBUG and compiled out of releases.
 *
 * Build a debug/instrumented image with:  -DGOLDFISH_DEBUG=1
 * (or flip the default below). Release builds leave it 0.
 *
 * Wrap instrumentation with GF_DBG(...):
 *     GF_DBG(uint32_t t0 = timer_hw->timerawl;)
 *     GF_DBG(if (d > g_max) g_max = d;)
 * In release GF_DBG(...) expands to nothing.
 */
#ifndef GOLDFISH_DEBUG_H
#define GOLDFISH_DEBUG_H

#ifndef GOLDFISH_DEBUG
#define GOLDFISH_DEBUG 0
#endif

#if GOLDFISH_DEBUG
#define GF_DBG(...) __VA_ARGS__
#else
#define GF_DBG(...)
#endif

#endif /* GOLDFISH_DEBUG_H */
