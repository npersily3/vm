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

VOID acquire_srw_exclusive(sharedLock* lock, PTHREAD_INFO info) {
    AcquireSRWLockExclusive(&lock->sharedLock);
#if DBG
    lock->threadId = info->ThreadId;
#endif
}
VOID release_srw_exclusive(sharedLock* lock) {
    ReleaseSRWLockExclusive(&lock->sharedLock);

#if DBG
    lock->threadId = 0;
#endif
}

VOID acquire_srw_shared(sharedLock* lock) {
    AcquireSRWLockShared(&lock->sharedLock);
#if DBG
    // Note: Multiple threads can hold shared locks, so we can't set threadId
    // You might want to use a different debug mechanism for shared locks
#endif
}

VOID release_srw_shared(sharedLock* lock) {
    ReleaseSRWLockShared(&lock->sharedLock);
}