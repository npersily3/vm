//
// Created by nrper on 7/29/2025.
//

#include "variables/structures.h"
#include "utils/random_utils.h"

// AI generated better randomness

// Initialize RNG state with non-deterministic seed
VOID InitializeThreadRNG(THREAD_RNG_STATE* rng) {
    LARGE_INTEGER perfCounter;
    ULONG64 rdtsc = __rdtsc();
    ULONG64 processId = GetCurrentProcessId();
    ULONG64 threadId = GetCurrentThreadId();

    QueryPerformanceCounter(&perfCounter);

    // Combine multiple entropy sources for non-deterministic seed
    rng->state = rdtsc ^ perfCounter.QuadPart ^
                (processId << 32) ^ (threadId << 16) ^
                ((ULONG64)rng << 8);  // Use stack address as additional entropy

    // Ensure state is never zero (would break XOR shift)
    if (rng->state == 0) {
        rng->state = 0x123456789ABCDEF1ULL;
    }

    rng->counter = 0;

    // Warm up the generator to improve distribution
    for (int i = 0; i < 32; i++) {
        GetNextRandom(rng);
    }
}

// High-quality XOR shift generator
ULONG64 GetNextRandom(THREAD_RNG_STATE* rng) {
    ULONG64 x = rng->state;

    // High-quality XOR shift with good statistical properties
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;

    rng->state = x;
    rng->counter++;

    // Occasionally reseed with fresh entropy to maintain non-determinism
    if ((rng->counter & 0xFFFF) == 0) {
        x ^= __rdtsc();  // Mix in fresh entropy periodically
        rng->state = x;
    }

    return x;
}

