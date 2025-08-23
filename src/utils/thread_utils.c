//
// Created by nrper on 7/17/2025.
//

#include "../../include/utils/thread_utils.h"

#include <stdio.h>

#include "../../include/variables/structures.h"
#include "variables/globals.h"

#if DBG
        BOOLEAN
RemoveEntryList(
        __in PLIST_ENTRY Entry
)
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Flink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;
    return (BOOLEAN)(Flink == Blink);
}



        VOID
InsertTailListDebug(
        __inout PLIST_ENTRY ListHead,
        __inout __drv_aliasesMem PLIST_ENTRY Entry
)
{
    PLIST_ENTRY Blink;

    Blink = ListHead->Blink;
    Entry->Flink = ListHead;
    Entry->Blink = Blink;
    Blink->Flink = Entry;
    ListHead->Blink = Entry;
}
#endif


// Basic validation function of a list
VOID validateList(pListHead head) {
    LIST_ENTRY* currentEntry;
    ULONG64 forwardLength = 0;
    ULONG64 backwardLength = 0;


    const ULONG64 MAX_EXPECTED_LENGTH = vm.config.number_of_physical_pages + 10; // Safety limit

    // Check empty list case
    if (head->length == 0) {
        ASSERT (head->entry.Blink == &head->entry && head->entry.Flink == &head->entry);
        return;
    }


    // Validate forward traversal with cycle detection
    currentEntry = head->entry.Flink;
    while (currentEntry != &head->entry && forwardLength < MAX_EXPECTED_LENGTH) {
        // Check for cross-list corruption
        ASSERT(currentEntry != &vm.lists.modified.entry &&
               currentEntry != &vm.lists.standby.entry &&
               currentEntry != &vm.lists.active.entry);

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


// my own implementation of a critical section using interlocks
void acquireLock(PULONG64 lock) {

    ULONG64 oldValueComparator;


    while (TRUE) {

        oldValueComparator =  InterlockedCompareExchange((volatile LONG *) lock, LOCK_HELD, LOCK_FREE);

        if (oldValueComparator == LOCK_FREE) {
            break;
        }
    }
}
void releaseLock(PULONG64 lock) {

    InterlockedExchange((volatile LONG *) lock, LOCK_FREE);
}
BOOL tryAcquireLock(PULONG64 lock) {

    ULONG64 oldValue;

    oldValue =  InterlockedCompareExchange( (volatile LONG *) lock, LOCK_HELD, LOCK_FREE);

    if (oldValue == LOCK_FREE) {
        return FALSE;
    }
    return TRUE;
}


// Wrappers for acquiring srw locks. where my debug versions trigger if indicated to
VOID acquire_srw_shared(sharedLock* lock) {

    AcquireSRWLockShared(&lock->sharedLock);

}

VOID release_srw_shared(sharedLock* lock) {

    ReleaseSRWLockShared(&lock->sharedLock);

}

VOID acquire_srw_exclusive(sharedLock* lock, PTHREAD_INFO info) {
#if DBG
    debug_acquire_srw_exclusive(lock, info);
#else
    AcquireSRWLockExclusive(&lock->sharedLock);
#endif
}

VOID release_srw_exclusive(sharedLock* lock) {
#if DBG
    debug_release_srw_exclusive(lock);
#else
    ReleaseSRWLockExclusive(&lock->sharedLock);
#endif
}

// Debug-only implementation functions
// key differences are storing each shared holder in a list and the thread id of an exclusive owner
#if DBG


VOID debug_acquire_srw_exclusive(sharedLock* lock, PTHREAD_INFO info) {
    AcquireSRWLockExclusive(&lock->sharedLock);
   // lock->threadId = info->ThreadId;
}

VOID debug_release_srw_exclusive(sharedLock* lock) {
    //lock->threadId = -1;
    ReleaseSRWLockExclusive(&lock->sharedLock);
}
#endif

//For now, I turned off the asserts because of how the recall woorks
// wrapper for pagelocks to help with debugging
VOID enterPageLock(pfn* page, PTHREAD_INFO info) {

//    ASSERT(info->ThreadId != (ULONG64) page->lock.OwningThread)

    //ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)
    EnterCriticalSection(&page->lock);
    //ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)

  //  ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
}

boolean tryEnterPageLock(pfn* page, PTHREAD_INFO info) {

    bool result;

   // ASSERT(info->ThreadId != (ULONG64) page->lock.OwningThread)
    result = TryEnterCriticalSection(&page->lock);

    if (result) {
   //     ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
    }

    return result;
}


VOID leavePageLock(pfn* page, PTHREAD_INFO info) {
 //   ASSERT(info->ThreadId == (ULONG64) page->lock.OwningThread)
    //ASSERT((ULONG64) page->lock.DebugInfo == MAXULONG_PTR)

    LeaveCriticalSection(&page->lock);



}