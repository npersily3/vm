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

#if DBG
    lock->threadId = 0;
#endif

    ReleaseSRWLockExclusive(&lock->sharedLock);

}

VOID acquire_srw_shared(sharedLock* lock) {

    #if useSharedLock
    AcquireSRWLockShared(&lock->sharedLock);
#else
    AcquireSRWLockExclusive(&lock->sharedLock);
#endif
    #if DBG
    // Note: Multiple threads can hold shared locks, so we can't set threadId
    // You might want to use a different debug mechanism for shared locks
#endif
}

VOID release_srw_shared(sharedLock* lock) {

#if useSharedLock
    ReleaseSRWLockShared(&lock->sharedLock);
#else
    ReleaseSRWLockExclusive(&lock->sharedLock);
#endif
}
VOID enterPageLock(pfn* page, PTHREAD_INFO info) {

    ASSERT(info->ThreadId != (ULONG64) page->lock.OwningThread)

    ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)
    EnterCriticalSection(&page->lock);
    ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)

    ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
}
VOID leavePageLock(pfn* page, PTHREAD_INFO info) {
    ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
    ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)

    LeaveCriticalSection(&page->lock);



}