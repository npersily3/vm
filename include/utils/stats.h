//
// Created by nrper on 7/24/2026.
//
// Recording side of the statistics instrumentation. The counters themselves and the STATS toggle
// live in variables/structures.h; the report lives in src/utils/stats.c.
//
// Every macro takes the caller's THREAD_INFO so the increment lands on that thread's private,
// 512-byte-aligned line -- no interlocked ops, no sharing. With STATS 0 they all expand to nothing.
//
// Because the arguments are discarded when STATS is 0, NEVER pass an expression with a side effect
// to one of these.
//
#ifndef STATS_H
#define STATS_H

#include "variables/structures.h"

VOID printStats(ULONG64 elapsedMs);

#if STATS

#define STAT(info, field)         (((PTHREAD_INFO)(info))->stats.field)
#define STAT_INC(info, field)     (STAT(info, field)++)
#define STAT_ADD(info, field, n)  (STAT(info, field) += (ULONG64)(n))

// Records n into a log2 histogram alongside its running sum and count, so the mean and the shape
// both come out of one call site.
#define STAT_SAMPLE(info, prefix, n)                             \
    do {                                                         \
        ULONG64 _n = (ULONG64)(n);                               \
        STAT(info, prefix##Sum) += _n;                           \
        STAT(info, prefix##Count)++;                             \
        STAT(info, prefix##Hist[statsBucket(_n)])++;              \
    } while (0)

#define STAT_MAX(info, field, n)                                 \
    do {                                                         \
        ULONG64 _n = (ULONG64)(n);                               \
        if (_n > STAT(info, field)) {                             \
            STAT(info, field) = _n;                              \
        }                                                        \
    } while (0)

// Clamps a tree layer to what the per-layer arrays can hold. A config deeper than STATS_MAX_LAYERS
// piles its deepest layers into the last row rather than running off the end.
#define STAT_LAYER(layer) \
    ((ULONG64)(layer) < STATS_MAX_LAYERS ? (ULONG64)(layer) : STATS_MAX_LAYERS - 1)

/**
 * @brief Maps a sample onto its log2 histogram bucket. Bucket 0 is exactly zero -- worth its own
 *        slot because "the batch was empty" is a different event from "the batch was small".
 *        Bucket i > 0 covers [2^(i-1), 2^i).
 */
__forceinline
ULONG64 statsBucket(ULONG64 n) {
    ULONG64 bucket = 0;

    while (n != 0 && bucket < STATS_BUCKETS - 1) {
        bucket++;
        n >>= 1;
    }

    return bucket;
}

#else

#define STAT_INC(info, field)          ((void)0)
#define STAT_ADD(info, field, n)       ((void)0)
#define STAT_SAMPLE(info, prefix, n)   ((void)0)
#define STAT_MAX(info, field, n)       ((void)0)
#define STAT_LAYER(layer)              (0)

#endif

#endif //STATS_H
