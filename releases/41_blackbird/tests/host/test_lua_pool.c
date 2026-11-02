// Host-side stress test for lib/lua_pool.c: random allocs, frees and reallocs
// through the same pool-or-malloc logic as lua_custom_alloc (main.cpp), with
// every live block's contents and the absence of overlaps checked as it goes.
// Run with: make host-test
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua_pool.h"

// Mirrors lua_realloc_block in main.cpp
static void* realloc_block(void* ptr, size_t osize, size_t nsize) {
    if (ptr == NULL) {
        void* p = lua_pool_alloc(nsize);
        return p ? p : malloc(nsize);
    }
    if (lua_pool_fits(ptr, nsize)) return ptr;
    int old_in_pool = osize <= LUA_POOL_MAX_SIZE && lua_pool_owns(ptr);
    if (!old_in_pool && nsize > LUA_POOL_MAX_SIZE) return realloc(ptr, nsize);
    void* p = (nsize <= LUA_POOL_MAX_SIZE) ? lua_pool_alloc(nsize) : NULL;
    if (!p) p = malloc(nsize);
    if (!p) return NULL;
    memcpy(p, ptr, osize < nsize ? osize : nsize);
    if (old_in_pool) lua_pool_free(ptr);
    else free(ptr);
    return p;
}

static void free_block(void* p) {
    if (!lua_pool_free(p)) free(p);
}

#define N 4000
static uint8_t* blocks[N];
static size_t sizes[N];
static uint8_t tags[N];

static size_t random_size(void) {
    // Mostly small, like Lua: ~90% <= 64 bytes
    return (rand() % 10) ? 1 + rand() % 64 : 65 + rand() % 3000;
}

static void fill(int i) { memset(blocks[i], tags[i], sizes[i]); }

static void check(int i) {
    for (size_t k = 0; k < sizes[i]; k++) {
        if (blocks[i][k] != tags[i]) {
            fprintf(stderr, "block %d (size %zu) corrupted at byte %zu\n", i, sizes[i], k);
            exit(1);
        }
    }
}

static int cmp_ptr(const void* a, const void* b) {
    uintptr_t x = (uintptr_t)blocks[*(const int*)a], y = (uintptr_t)blocks[*(const int*)b];
    return (x > y) - (x < y);
}

static void check_no_overlap(void) {
    static int order[N];
    int n = 0;
    for (int i = 0; i < N; i++) if (blocks[i]) order[n++] = i;
    qsort(order, n, sizeof(int), cmp_ptr);
    for (int k = 1; k < n; k++) {
        int a = order[k - 1], b = order[k];
        if ((uintptr_t)blocks[a] + sizes[a] > (uintptr_t)blocks[b]) {
            fprintf(stderr, "blocks %d and %d overlap\n", a, b);
            exit(1);
        }
    }
}

int main(void) {
    srand(1);
    lua_pool_init();
    long ops = 0;
    for (int round = 0; round < 400; round++) {
        for (int step = 0; step < 5000; step++, ops++) {
            int i = rand() % N;
            if (!blocks[i]) {
                sizes[i] = random_size();
                tags[i] = (uint8_t)(rand() | 1);
                blocks[i] = realloc_block(NULL, 0, sizes[i]);
                assert(blocks[i]);
                fill(i);
            } else if (rand() % 3 == 0) {
                check(i);
                size_t old = sizes[i], nsize = random_size();
                uint8_t* p = realloc_block(blocks[i], old, nsize);
                assert(p);
                blocks[i] = p;
                sizes[i] = nsize;
                // the kept prefix must have survived the move
                for (size_t k = 0; k < (old < nsize ? old : nsize); k++) assert(p[k] == tags[i]);
                fill(i);
            } else {
                check(i);
                free_block(blocks[i]);
                blocks[i] = NULL;
            }
        }
        for (int i = 0; i < N; i++) if (blocks[i]) check(i);
        check_no_overlap();
        if (round % 100 == 99) lua_pool_trim();
    }
    for (int i = 0; i < N; i++) if (blocks[i]) { check(i); free_block(blocks[i]); blocks[i] = NULL; }
    lua_pool_trim();
    lua_pool_stats_t st;
    lua_pool_get_stats(&st);
    if (st.pages != 0 || st.slot_bytes != 0 || st.arena_free_pages != 64) {
        fprintf(stderr, "leak: %u pages, %u slot bytes after freeing everything\n", st.pages, st.slot_bytes);
        return 1;
    }
    printf("lua_pool: %ld ops, contents and overlaps checked, no leaks\n", ops);
    return 0;
}
