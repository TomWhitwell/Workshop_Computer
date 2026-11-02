#pragma once

// Small-object pool for the Lua allocator.
//
// Lua's allocations are mostly tiny (measured on blackbird: 82% of live objects
// at boot, and 98.8% of allocations while running bowery scripts, are <= 64
// bytes). Mixed into the C heap, the short-lived ones leave holes between
// long-lived ones, until plenty of memory is free but no large block is (28KB
// free with a 4KB largest block was seen), and big scripts fail to compile.
// Here, objects <= LUA_POOL_MAX_SIZE live in 1KB pages of equal-sized slots,
// one size class per page, so their churn never fragments the C heap, and
// they cost no per-object malloc header (8 bytes each).
//
// Core 0 only (Lua's core): no locking.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUA_POOL_MAX_SIZE 64

// Reserve the pool's arena. Call once, before the Lua state is created.
void lua_pool_init(void);

// A slot for `size` bytes (1..LUA_POOL_MAX_SIZE), or NULL if no page could be
// had (the caller can fall back to malloc).
void* lua_pool_alloc(size_t size);

// If p is in a pool page, free it and return true; otherwise return false
// (it came from malloc).
bool lua_pool_free(void* p);

// True if p is in a pool page.
bool lua_pool_owns(const void* p);

// True if p is in a pool page of the size class for `size`, so a realloc to
// `size` can keep p.
bool lua_pool_fits(void* p, size_t size);

// Release cached empty pages back to the C heap (call before giving up on a
// large allocation).
void lua_pool_trim(void);

typedef struct {
    uint32_t pages;          // pages held
    uint32_t slot_bytes;     // bytes in slots handed out
    uint32_t capacity_bytes; // bytes in all slots of all pages
    uint32_t arena_free_pages; // arena pages not in use
} lua_pool_stats_t;

void lua_pool_get_stats(lua_pool_stats_t* out);

#ifdef __cplusplus
}
#endif
