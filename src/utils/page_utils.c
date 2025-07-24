//
// Created by nrper on 7/17/2025.
//

#include "../../include/utils/page_utils.h"
#include "../../include/variables/globals.h"

VOID
pfnInbounds(pfn* trimmed) {
    if (trimmed < pfnStart || trimmed >= endPFN) {
        DebugBreak();
    }
}
ULONG64 getFrameNumber(pfn* pfn) {
    return (ULONG64)(pfn - pfnStart);
}

pfn* getPFNfromFrameNumber(ULONG64 frameNumber) {
    return pfnStart + frameNumber;
}

VOID removeFromMiddleOfList(pListHead head,pfn* page) {

    AcquireSRWLockShared(&head->sharedLock);

    pfn* Flink;
    pfn* Blink;
    boolean obtainedLocks;

    obtainedLocks = FALSE;



    for (int i = 0; i < 5; ++i) {
        EnterCriticalSection(&page->lock);
        Flink = container_of(page->entry.Flink, pfn, entry.Flink);
        Blink = container_of(page->entry.Blink, pfn, entry.Blink);

        if (TryEnterCriticalSection(&Flink->lock) == TRUE) {
            if (TryEnterCriticalSection(&Blink->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
            LeaveCriticalSection(&Flink->lock);
        }
        LeaveCriticalSection(&page->lock);
    }


    if (obtainedLocks == FALSE) {
        ReleaseSRWLockShared(&head->sharedLock);
        AcquireSRWLockExclusive(&head->sharedLock);

    }

    LIST_ENTRY* prevEntry = &Blink->entry;
    LIST_ENTRY* nextEntry = &Flink->entry;
    prevEntry->Flink = nextEntry;
    nextEntry->Blink = prevEntry;
    InterlockedDecrement64(&head->length);

    if (obtainedLocks == TRUE) {
        LeaveCriticalSection(&page->lock);
        LeaveCriticalSection(&Flink->lock);
        LeaveCriticalSection(&Blink->lock);

        ReleaseSRWLockShared(&head->sharedLock);
    } else {
        ReleaseSRWLockExclusive(&head->sharedLock);
    }

}


pfn* RemoveFromHeadofPageList(pListHead head) {



    if (ReadULong64NoFence(&head->length) == 0) {
        return LIST_IS_EMPTY;
    }
    if (ReadULong64NoFence(&head->length) == 1) {


        InterlockedDecrement64(&head->length);

        AcquireSRWLockExclusive(&head->sharedLock);

        pfn* page;
        page = container_of(head->entry.Flink,pfn, entry);
        head->entry.Flink = & head->entry;
        head->entry.Blink = & head->entry;
        ReleaseSRWLockExclusive(&head->sharedLock);

        return  page;
    }

    AcquireSRWLockShared(&head->sharedLock);

    pfn* currentHeadPage;
    pfn* nextHeadPage;
    boolean obtainedLocks;

    obtainedLocks = FALSE;



    for (int i = 0; i < 5; ++i) {
        EnterCriticalSection(&head->lock);
        currentHeadPage = container_of(head->entry.Flink, pfn, entry);
         nextHeadPage = container_of(currentHeadPage->entry.Flink, pfn, entry);

        if (TryEnterCriticalSection(&currentHeadPage->lock) == TRUE) {
            if (TryEnterCriticalSection(&nextHeadPage->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
            LeaveCriticalSection(&currentHeadPage->lock);
        }
        LeaveCriticalSection(&head->lock);
    }


    if (obtainedLocks == FALSE) {
        ReleaseSRWLockShared(&head->sharedLock);
        AcquireSRWLockExclusive(&head->sharedLock);

        if (ReadULong64NoFence(&head->length) == 0) {
            ReleaseSRWLockExclusive(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

    }

    LIST_ENTRY* ListHead = &head->entry;
    LIST_ENTRY*    Entry = ListHead->Flink;
    LIST_ENTRY* Flink = Entry->Flink;







    ListHead->Flink = Flink;
    Flink->Blink = ListHead;



    InterlockedDecrement64(&head->length);



    if (obtainedLocks == TRUE) {
        LeaveCriticalSection(&head->lock);
        LeaveCriticalSection(&currentHeadPage->lock);
        LeaveCriticalSection(&nextHeadPage->lock);

        ReleaseSRWLockShared(&head->sharedLock);
    } else {
        ReleaseSRWLockExclusive(&head->sharedLock);
    }

    return container_of(Entry, pfn, entry);

}

VOID addPageToTail(pListHead head, pfn* page) {
    boolean obtainedLocks = FALSE;

    pfn* nextPage;
    AcquireSRWLockShared(&head->sharedLock);

    if (head->length == 0) {
        EnterCriticalSection(&head->lock);
        EnterCriticalSection(&page->lock);

        head->entry.Blink = &page->entry;
        head->entry.Flink = &page->entry;

        page->entry.Blink = &page->entry;
        page->entry.Flink = &page->entry;

        LeaveCriticalSection(&page->lock);
        LeaveCriticalSection(&head->lock);
        ReleaseSRWLockExclusive(&head->sharedLock);

        return;
    }


    for (int i = 0; i < 5; ++i) {
        EnterCriticalSection(&head->lock);
        nextPage = container_of(head->entry.Blink, pfn, entry);

        if (TryEnterCriticalSection(&page->lock) == TRUE) {
            if (TryEnterCriticalSection(&nextPage->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
            LeaveCriticalSection(&page->lock);
        }
        LeaveCriticalSection(&head->lock);
    }

    if (obtainedLocks == FALSE) {
        ReleaseSRWLockShared(&head->sharedLock);
        AcquireSRWLockExclusive(&head->sharedLock);
        //check if zero again
        nextPage = container_of(head->entry.Blink, pfn, entry);
    }

    head->entry.Blink = &page->entry;
    nextPage->entry.Flink = &page->entry;
    page->entry.Blink = &nextPage->entry;
    page->entry.Flink = &head->entry;

    InterlockedIncrement64(&head->length);


    if (obtainedLocks == TRUE) {

        LeaveCriticalSection(&nextPage->lock);
        LeaveCriticalSection(&page->lock);
        LeaveCriticalSection(&head->lock);

        ReleaseSRWLockShared(&head->sharedLock);
    } else {
        ReleaseSRWLockExclusive(&head->sharedLock);
    }
}