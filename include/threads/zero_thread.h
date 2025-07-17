//
// Created by nrper on 7/17/2025.
//

#ifndef ZERO_THREAD_H
#define ZERO_THREAD_H

#include "../variables/structures.h"

DWORD zeroingThread (LPVOID threadContext);
BOOL zeroMultiplePages (PULONG64 frameNumbers, ULONG64 batchSize) ;

#endif //ZERO_THREAD_H
