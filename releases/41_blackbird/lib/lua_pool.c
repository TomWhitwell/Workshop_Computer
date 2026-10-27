#include "lua_pool.h"

#include <stdlib.h>
#include <string.h>

// Pages are LUA_POOL_PAGE_SIZE bytes from malloc: a header, then equal slots
// of one size class. Classes are multiples of 8 bytes up to
// LUA_POOL_MAX_SIZE. Each class keeps a doubly linked list of pages that have
// a free slot; new objects come from the page at its head until that fills,
// then from the fullest remaining page, and pages that regain a free slot go
// to the back. Filling full pages first lets sparse ones drain and go back to
// the C heap, instead of every page staying pinned by a few live objects. A table of all pages, sorted by address, maps a pointer to its
// page on free (binary search), so pages can come from anywhere in the heap.
// Pages come from an arena malloc'd once by lua_pool_init(), before Lua
// starts, so they sit together at the bottom of the heap. Pages taken from
// malloc anywhere would each pin their spot for as long as any object on them
// lived, scattering holes through the heap: measured, that left a 5.5KB
// largest free block after the bowery corpus. Only if the arena is used up do
// pages come from malloc; those go back when empty, except one spare per class
// (kept to avoid allocating and freeing a page repeatedly at a boundary), and
// lua_pool_trim() releases those too. Empty arena pages stay in the arena, for
// any size class.

#define LUA_POOL_PAGE_SIZE 1024
#define LUA_POOL_CLASSES (LUA_POOL_MAX_SIZE / 8)
#define LUA_POOL_MAX_PAGES 256   // 256KB of pages: more than the whole heap
#define LUA_POOL_ARENA_PAGES 64  // the boot state uses ~61

typedef struct pool_page {
    struct pool_page* next;  // in its class's list of pages with free slots
    struct pool_page* prev;
    void* free;              // free slot list (next pointer stored in the slot)
    uint16_t cls;
    uint16_t used;           // slots handed out
} pool_page_t;

#define SLOTS_START sizeof(pool_page_t)
#define SLOT_SIZE(cls) (((cls) + 1) * 8)
#define SLOTS_PER_PAGE(cls) ((LUA_POOL_PAGE_SIZE - SLOTS_START) / SLOT_SIZE(cls))

static pool_page_t* g_partial[LUA_POOL_CLASSES];  // pages with a free slot
static uint8_t g_spare[LUA_POOL_CLASSES];         // empty pages kept (0 or 1)
static pool_page_t* g_pages[LUA_POOL_MAX_PAGES];  // all pages, sorted by address
static uint32_t g_npages = 0;
static uint32_t g_slot_bytes = 0;
static uint8_t* g_arena = NULL;
static pool_page_t* g_arena_free = NULL;  // unused arena pages, linked through ->next

void lua_pool_init(void) {
    if (g_arena) return;
    g_arena = (uint8_t*)malloc(LUA_POOL_ARENA_PAGES * LUA_POOL_PAGE_SIZE);
    if (!g_arena) return;  // run without an arena: pages come from malloc
    for (int i = LUA_POOL_ARENA_PAGES - 1; i >= 0; i--) {
        pool_page_t* pg = (pool_page_t*)(g_arena + i * LUA_POOL_PAGE_SIZE);
        pg->next = g_arena_free;
        g_arena_free = pg;
    }
}

static bool in_arena(const pool_page_t* pg) {
    return g_arena && (const uint8_t*)pg >= g_arena &&
           (const uint8_t*)pg < g_arena + LUA_POOL_ARENA_PAGES * LUA_POOL_PAGE_SIZE;
}
static uint32_t g_capacity_bytes = 0;

static inline uint32_t size_class(size_t size) {
    return (uint32_t)((size - 1) >> 3);
}

// Index of the page containing p, or -1
static int find_page(const void* p) {
    int lo = 0, hi = (int)g_npages - 1;
    uintptr_t a = (uintptr_t)p;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uintptr_t start = (uintptr_t)g_pages[mid];
        if (a < start) {
            hi = mid - 1;
        } else if (a >= start + LUA_POOL_PAGE_SIZE) {
            lo = mid + 1;
        } else {
            return mid;
        }
    }
    return -1;
}

static void list_push(pool_page_t* pg) {
    pg->prev = NULL;
    pg->next = g_partial[pg->cls];
    if (pg->next) pg->next->prev = pg;
    g_partial[pg->cls] = pg;
}

static void list_remove(pool_page_t* pg) {
    if (pg->prev) pg->prev->next = pg->next;
    else g_partial[pg->cls] = pg->next;
    if (pg->next) pg->next->prev = pg->prev;
    pg->next = pg->prev = NULL;
}

static void list_push_back(pool_page_t* pg) {
    pool_page_t* tail = g_partial[pg->cls];
    if (!tail) {
        list_push(pg);
        return;
    }
    while (tail->next) tail = tail->next;
    tail->next = pg;
    pg->prev = tail;
    pg->next = NULL;
}

// Move the class's fullest partial page to the head of its list
static void select_fullest(uint32_t cls) {
    pool_page_t* best = g_partial[cls];
    if (!best) return;
    for (pool_page_t* pg = best->next; pg; pg = pg->next) {
        if (pg->used > best->used) best = pg;
    }
    if (best != g_partial[cls]) {
        list_remove(best);
        list_push(best);
    }
}

static pool_page_t* page_new(uint32_t cls) {
    if (g_npages >= LUA_POOL_MAX_PAGES) return NULL;
    pool_page_t* pg = g_arena_free;
    if (pg) {
        g_arena_free = pg->next;
    } else if (!(pg = (pool_page_t*)malloc(LUA_POOL_PAGE_SIZE))) {
        return NULL;
    }

    pg->cls = (uint16_t)cls;
    pg->used = 0;
    // Thread the free list through the slots, first slot first
    uint32_t size = SLOT_SIZE(cls), n = SLOTS_PER_PAGE(cls);
    uint8_t* slots = (uint8_t*)pg + SLOTS_START;
    for (uint32_t i = 0; i < n; i++) {
        *(void**)(slots + i * size) = (i + 1 < n) ? (void*)(slots + (i + 1) * size) : NULL;
    }
    pg->free = slots;

    // Insert into the sorted page table
    uint32_t i = g_npages;
    while (i > 0 && (uintptr_t)g_pages[i - 1] > (uintptr_t)pg) {
        g_pages[i] = g_pages[i - 1];
        i--;
    }
    g_pages[i] = pg;
    g_npages++;
    g_capacity_bytes += n * size;
    list_push(pg);
    return pg;
}

static void page_release(int index) {
    pool_page_t* pg = g_pages[index];
    list_remove(pg);
    memmove(&g_pages[index], &g_pages[index + 1], (g_npages - index - 1) * sizeof(g_pages[0]));
    g_npages--;
    g_capacity_bytes -= SLOTS_PER_PAGE(pg->cls) * SLOT_SIZE(pg->cls);
    if (in_arena(pg)) {
        pg->next = g_arena_free;
        g_arena_free = pg;
    } else {
        free(pg);
    }
}

void* lua_pool_alloc(size_t size) {
    if (size == 0 || size > LUA_POOL_MAX_SIZE) return NULL;
    uint32_t cls = size_class(size);
    pool_page_t* pg = g_partial[cls];
    if (!pg && !(pg = page_new(cls))) return NULL;

    if (pg->used == 0 && g_spare[cls]) g_spare[cls] = 0;  // the spare is in use again
    void* slot = pg->free;
    pg->free = *(void**)slot;
    pg->used++;
    if (!pg->free) {  // now full: continue from the fullest remaining page
        list_remove(pg);
        select_fullest(cls);
    }
    g_slot_bytes += SLOT_SIZE(cls);
    return slot;
}

bool lua_pool_free(void* p) {
    int index = find_page(p);
    if (index < 0) return false;
    pool_page_t* pg = g_pages[index];
    uint32_t cls = pg->cls;

    if (!pg->free) list_push_back(pg);  // was full: has a free slot again
    *(void**)p = pg->free;
    pg->free = p;
    pg->used--;
    g_slot_bytes -= SLOT_SIZE(cls);

    if (pg->used == 0) {
        if (in_arena(pg)) page_release(index);  // back to the arena, for any class
        else if (g_spare[cls]) page_release(index);
        else g_spare[cls] = 1;
    }
    return true;
}

bool lua_pool_owns(const void* p) {
    return find_page(p) >= 0;
}

bool lua_pool_fits(void* p, size_t size) {
    if (size == 0 || size > LUA_POOL_MAX_SIZE) return false;
    int index = find_page(p);
    return index >= 0 && g_pages[index]->cls == size_class(size);
}

void lua_pool_trim(void) {
    for (int i = (int)g_npages - 1; i >= 0; i--) {
        pool_page_t* pg = g_pages[i];
        if (pg->used == 0 && !in_arena(pg)) {
            g_spare[pg->cls] = 0;
            page_release(i);
        }
    }
}

void lua_pool_get_stats(lua_pool_stats_t* out) {
    out->pages = g_npages;
    out->slot_bytes = g_slot_bytes;
    out->capacity_bytes = g_capacity_bytes;
    out->arena_free_pages = 0;
    for (pool_page_t* pg = g_arena_free; pg; pg = pg->next) out->arena_free_pages++;
}
