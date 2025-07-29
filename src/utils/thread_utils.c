//
// Created by nrper on 7/17/2025.
//

#include "../../include/utils/thread_utils.h"

#include "../../include/variables/structures.h"
#include "variables/globals.h"

VOID validateList(pListHead head) {
    LIST_ENTRY* currentEntry;
    ULONG64 forwardLength = 0;
    ULONG64 backwardLength = 0;


    const ULONG64 MAX_EXPECTED_LENGTH = NUMBER_OF_PHYSICAL_PAGES + 10; // Safety limit

    // Check empty list case
    if (head->length == 0) {
        ASSERT (head->entry.Blink == &head->entry && head->entry.Flink == &head->entry);
        return;
    }


    // Validate forward traversal with cycle detection
    currentEntry = head->entry.Flink;
    while (currentEntry != &head->entry && forwardLength < MAX_EXPECTED_LENGTH) {
        // Check for cross-list corruption
        ASSERT(currentEntry != &headModifiedList.entry &&
               currentEntry != &headStandByList.entry &&
               currentEntry != &headActiveList.entry &&
               currentEntry != &headFreeList.entry &&
               currentEntry != &headToBeZeroedList.entry);

        // Validate bidirectional linking
        if (currentEntry->Blink->Flink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }
        if (currentEntry->Flink->Blink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }

        currentEntry = currentEntry->Flink;
        forwardLength++;
    }

    // Check for infinite loop
    if (forwardLength >= MAX_EXPECTED_LENGTH) {
        ASSERT(FALSE); // Possible infinite loop detected
        return ;
    }

    // Validate backward traversal
    currentEntry = head->entry.Blink;
    while (currentEntry != &head->entry && backwardLength < MAX_EXPECTED_LENGTH) {

        if (currentEntry->Blink->Flink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }
        if (currentEntry->Flink->Blink != currentEntry) {
            ASSERT(FALSE); // Broken bidirectional link
            return;
        }

        currentEntry = currentEntry->Blink;

        backwardLength++;
    }

    // Verify all lengths match
    BOOL valid = (forwardLength == backwardLength && forwardLength == head->length);
    ASSERT(valid);
    return;
}

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

    validateList(container_of(lock, listHead, sharedLock));
    lock->threadId = info->ThreadId;
#endif
}
VOID release_srw_exclusive(sharedLock* lock) {

#if DBG

    validateList(container_of(lock, listHead, sharedLock));
    lock->threadId = -1;
#endif

    ReleaseSRWLockExclusive(&lock->sharedLock);

}

VOID acquire_srw_shared(sharedLock* lock) {

#if useSharedLock

    AcquireSRWLockShared(&lock->sharedLock);

    #if DBG
        InterlockedIncrement(&lock->numHeldShared);
    #endif


#else
    AcquireSRWLockExclusive(&lock->sharedLock);

#endif

}

VOID release_srw_shared(sharedLock* lock) {

#if useSharedLock
    ReleaseSRWLockShared(&lock->sharedLock);
    InterlockedDecrement(&lock->numHeldShared);
#else
    ReleaseSRWLockExclusive(&lock->sharedLock);
#endif
}


VOID enterPageLock(pfn* page, PTHREAD_INFO info) {

    ASSERT(info->ThreadId != (ULONG64) page->lock.OwningThread)

    //ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)
    EnterCriticalSection(&page->lock);
    //ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)

    ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
}
VOID leavePageLock(pfn* page, PTHREAD_INFO info) {
    ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
    //ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)

    LeaveCriticalSection(&page->lock);



}