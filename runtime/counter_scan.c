// LARECOMP_COUNTER_SCAN=<frame> (debug): finds the game's steady counters - floats that change by the same amount
// every frame (animation clocks, timers) - in its globals and heap, over the presented frames <frame> .. +3, and logs
// them by step ("[scan]"). Comparing a 30 and a 60 fps run shows which clocks step per frame instead of per time:
// those keep their step at 60 fps, so what they drive runs twice as fast.
//   frame F: a hash of each page in use; F+1: a copy of the pages that changed; F+2: the floats that changed since
//   F+1 (their step); F+3: those that changed by the same step again.
// Windows only: pages the game never touched are skipped (QueryWorkingSetEx), so the scan doesn't commit the heap.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hle.h"
#include "rt.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

#define PAGE 4096ull
#define MAIN_DATA_LO 0x7000872000ull  // (the game module's data and bss: tools/nso.py)
#define MAIN_DATA_HI 0x700266e000ull

uint64_t kernel_heap_size(void);

typedef struct { uint64_t lo, hi; } Range;
static Range g_ranges[2];
static int g_nranges;
static uint64_t *g_hash;   // per page (all ranges, in order); 0: not resident
static uint8_t *g_copy;    // F+1 copies of the changed pages
static uint64_t *g_copy_addr;
static size_t g_ncopy;
typedef struct { uint64_t addr; float last, step; } Cand;
static Cand *g_cand;
static size_t g_ncand;

static uint64_t page_hash(const uint64_t *p) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < (int)(PAGE / 8); i++) h = (h ^ p[i]) * 1099511628211ull;
    return h | 1;
}

static void resident(uint64_t lo, uint64_t n, bool *out) {  // which of n pages from lo are in the working set
    PSAPI_WORKING_SET_EX_INFORMATION *wi = malloc(sizeof *wi * 4096);
    for (uint64_t i = 0; i < n; i += 4096) {
        uint64_t k = n - i < 4096 ? n - i : 4096;
        for (uint64_t j = 0; j < k; j++) wi[j].VirtualAddress = (void *)(uintptr_t)(lo + (i + j) * PAGE);
        if (!QueryWorkingSetEx(GetCurrentProcess(), wi, (DWORD)(sizeof *wi * k))) {
            for (uint64_t j = 0; j < k; j++) out[i + j] = false;
            continue;
        }
        for (uint64_t j = 0; j < k; j++) out[i + j] = wi[j].VirtualAttributes.Valid;
    }
    free(wi);
}

static uint64_t total_pages(void) {
    uint64_t n = 0;
    for (int r = 0; r < g_nranges; r++) n += (g_ranges[r].hi - g_ranges[r].lo) / PAGE;
    return n;
}

static int by_step(const void *a, const void *b) {
    float x = ((const Cand *)a)->step, y = ((const Cand *)b)->step;
    return x < y ? -1 : x > y;
}

// LARECOMP_PEEK=<hex addr>[,...] (debug): logs each listed float when its value changes (checked every frame)
static void peek_frame(uint64_t frame) {
    static int n = -1;
    static uint64_t addr[16];
    static uint32_t last[16];
    if (n < 0) {
        n = 0;
        for (const char *p = getenv("LARECOMP_PEEK"); p && *p && n < 16;) {
            addr[n++] = strtoull(p, (char **)&p, 16);
            while (*p == ',') p++;
        }
        for (int i = 0; i < n; i++) last[i] = ~*(const uint32_t *)(uintptr_t)addr[i];
    }
    for (int i = 0; i < n; i++) {
        uint32_t v = *(const uint32_t *)(uintptr_t)addr[i];
        if (v == last[i]) continue;
        last[i] = v;
        float f;
        memcpy(&f, &v, 4);
        LOG("[peek] frame %llu %llx = %g (%08x)\n", (unsigned long long)frame, (unsigned long long)addr[i], f, v);
    }
}

void counter_scan_frame(uint64_t frame) {
    peek_frame(frame);
    static long at = -2;
    if (at == -2) { const char *e = getenv("LARECOMP_COUNTER_SCAN"); at = e ? atol(e) : -1; }
    if (at < 0 || frame < (uint64_t)at || frame > (uint64_t)at + 3) return;
    int stage = (int)(frame - (uint64_t)at);
    if (stage == 0) {
        g_ranges[0] = (Range){MAIN_DATA_LO, MAIN_DATA_HI};
        g_ranges[1] = (Range){HEAP_BASE, HEAP_BASE + (kernel_heap_size() & ~(PAGE - 1))};
        g_nranges = 2;
        uint64_t n = total_pages(), idx = 0, used = 0;
        g_hash = calloc(n, sizeof *g_hash);
        bool *res = malloc(n);
        for (int r = 0; r < g_nranges; r++) {
            uint64_t pages = (g_ranges[r].hi - g_ranges[r].lo) / PAGE;
            resident(g_ranges[r].lo, pages, res + idx);
            for (uint64_t p = 0; p < pages; p++, idx++)
                if (res[idx]) g_hash[idx] = page_hash((const uint64_t *)(uintptr_t)(g_ranges[r].lo + p * PAGE)), used++;
        }
        free(res);
        LOG("[scan] frame %llu: %llu of %llu pages in use\n", (unsigned long long)frame, (unsigned long long)used,
            (unsigned long long)n);
    } else if (stage == 1) {
        uint64_t idx = 0;
        size_t cap = 0;
        for (int r = 0; r < g_nranges; r++)
            for (uint64_t a = g_ranges[r].lo; a < g_ranges[r].hi; a += PAGE, idx++) {
                if (!g_hash[idx] || page_hash((const uint64_t *)(uintptr_t)a) == g_hash[idx]) continue;
                if (g_ncopy == cap) {
                    cap = cap ? cap * 2 : 4096;
                    g_copy = realloc(g_copy, cap * PAGE);
                    g_copy_addr = realloc(g_copy_addr, cap * sizeof *g_copy_addr);
                }
                memcpy(g_copy + g_ncopy * PAGE, (const void *)(uintptr_t)a, PAGE);
                g_copy_addr[g_ncopy++] = a;
            }
        LOG("[scan] frame %llu: %zu pages changed\n", (unsigned long long)frame, g_ncopy);
    } else if (stage == 2) {
        size_t cap = 0;
        for (size_t i = 0; i < g_ncopy; i++) {
            const float *o = (const float *)(g_copy + i * PAGE), *n = (const float *)(uintptr_t)g_copy_addr[i];
            for (int k = 0; k < (int)(PAGE / 4); k++) {
                float a = o[k], b = n[k], d = b - a;
                if (!isfinite(a) || !isfinite(b) || a == b || fabsf(d) < 1e-4f || fabsf(d) > 1000.0f) continue;
                if (fabsf(a) > 1e7f || fabsf(b) > 1e7f) continue;
                if (g_ncand == cap) g_cand = realloc(g_cand, (cap = cap ? cap * 2 : 65536) * sizeof *g_cand);
                g_cand[g_ncand++] = (Cand){g_copy_addr[i] + 4 * k, b, d};
            }
        }
        LOG("[scan] frame %llu: %zu floats changed\n", (unsigned long long)frame, g_ncand);
    } else {
        size_t keep = 0;
        for (size_t i = 0; i < g_ncand; i++) {
            float b = *(const float *)(uintptr_t)g_cand[i].addr, d = b - g_cand[i].last;
            if (fabsf(d - g_cand[i].step) <= 1e-5f * fmaxf(1.0f, fabsf(b))) g_cand[keep++] = g_cand[i];
        }
        qsort(g_cand, keep, sizeof *g_cand, by_step);
        LOG("[scan] frame %llu: %zu steady counters (step: count, first addresses)\n", (unsigned long long)frame, keep);
        for (size_t i = 0; i < keep;) {
            size_t j = i;
            while (j < keep && fabsf(g_cand[j].step - g_cand[i].step) <= 1e-6f * fmaxf(1.0f, fabsf(g_cand[i].step))) j++;
            char addrs[400] = "";
            for (size_t k = i; k < j && k < i + 8; k++) {
                char t[48];
                snprintf(t, sizeof t, " %llx=%g", (unsigned long long)g_cand[k].addr, *(const float *)(uintptr_t)g_cand[k].addr);
                strncat(addrs, t, sizeof addrs - strlen(addrs) - 1);
            }
            LOG("[scan]   step %.7g: %zu%s\n", g_cand[i].step, j - i, addrs);
            i = j;
        }
        free(g_hash), free(g_copy), free(g_copy_addr), free(g_cand);
        g_hash = NULL, g_copy = NULL, g_copy_addr = NULL, g_cand = NULL, g_ncopy = g_ncand = 0;
    }
}
#else
void counter_scan_frame(uint64_t frame) { (void)frame; }
#endif
