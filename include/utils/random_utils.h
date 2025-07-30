//
// Created by nrper on 7/29/2025.
//

#ifndef RANDOM_UTILS_H
#define RANDOM_UTILS_H

#include "variables/structures.h"

VOID InitializeThreadRNG(THREAD_RNG_STATE* rng) ;
ULONG64 GetNextRandom(THREAD_RNG_STATE* rng);


#endif //RANDOM_UTILS_H
