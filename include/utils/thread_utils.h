//
// Created by nrper on 7/17/2025.
//

#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#include "../variables/structures.h"

void acquireLock(PULONG64 lock);
void releaseLock(PULONG64 lock);
BOOL tryAcquireLock(PULONG64 lock);



#endif //THREAD_UTILS_H
