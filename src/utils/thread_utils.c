//
// Created by nrper on 7/17/2025.
//

#include "../../include/utils/thread_utils.h"

#include "../../include/variables/structures.h"

void acquireLock(PULONG64 lock) {

    ULONG64 oldValueComparator;


    while (TRUE) {

        oldValueComparator =  InterlockedCompareExchange(lock, LOCK_HELD, LOCK_FREE);

        if (oldValueComparator == LOCK_FREE) {
            break;
        }
    }
}
void releaseLock(PULONG64 lock) {

    InterlockedExchange(lock, LOCK_FREE);
}
BOOL tryAcquireLock(PULONG64 lock) {

    ULONG64 oldValue;

    oldValue =  InterlockedCompareExchange(lock, LOCK_HELD, LOCK_FREE);

    if (oldValue == LOCK_FREE) {
        return FALSE;
    }
    return TRUE;
}
