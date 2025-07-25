//
// Created by nrper on 7/17/2025.
//

#include "../../include/utils/page_utils.h"
#include "../../include/variables/globals.h"
#include "utils/thread_utils.h"

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

VOID removeFromMiddleOfList(pListHead head,pfn* page, PTHREAD_INFO threadInfo) {

    acquire_srw_shared(&head->sharedLock);

    pfn* Flink;
    pfn* Blink;
    boolean obtainedLocks;

    obtainedLocks = FALSE;



    for (int i = 0; i < 5; ++i) {

        Flink = container_of(page->entry.Flink, pfn, entry.Flink);
        Blink = container_of(page->entry.Blink, pfn, entry.Blink);

        if (TryEnterCriticalSection(&Flink->lock) == TRUE) {
            if (TryEnterCriticalSection(&Blink->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
            LeaveCriticalSection(&Flink->lock);
        }

    }


    if (obtainedLocks == FALSE) {
        release_srw_shared(&head->sharedLock);
        acquire_srw_exclusive(&head->sharedLock, threadInfo);

    }

    LIST_ENTRY* prevEntry = &Blink->entry;
    LIST_ENTRY* nextEntry = &Flink->entry;
    prevEntry->Flink = nextEntry;
    nextEntry->Blink = prevEntry;
    InterlockedDecrement64(&head->length);

    if (obtainedLocks == TRUE) {

        LeaveCriticalSection(&Flink->lock);
        LeaveCriticalSection(&Blink->lock);

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }

}


pfn* RemoveFromHeadofPageList(pListHead head, PTHREAD_INFO threadInfo) {


    acquire_srw_shared(&head->sharedLock);

    pfn* currentHeadPage;
    pfn* nextHeadPage;
    boolean obtainedLocks;

    obtainedLocks = FALSE;



    for (int i = 0; i < 5; ++i) {
        EnterCriticalSection(&head->pageLock);
        currentHeadPage = container_of(head->entry.Flink, pfn, entry);
         nextHeadPage = container_of(currentHeadPage->entry.Flink, pfn, entry);

        if (&nextHeadPage->entry == &head->entry) {
            nextHeadPage = NULL;
        }


        if (TryEnterCriticalSection(&currentHeadPage->lock) == TRUE) {
            if (nextHeadPage != NULL) {
                if (TryEnterCriticalSection(&nextHeadPage->lock) == TRUE) {
                    obtainedLocks = TRUE;
                    break;
                }
            } else {
                obtainedLocks = TRUE;
                break;
            }

            LeaveCriticalSection(&currentHeadPage->lock);
        }
        LeaveCriticalSection(&head->pageLock);
    }


    if (obtainedLocks == FALSE) {
        release_srw_shared(&head->sharedLock);

        acquire_srw_exclusive(&head->sharedLock, threadInfo);

        if (ReadULong64NoFence(&head->length) == 0) {
            release_srw_exclusive(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

    }

    LIST_ENTRY* ListHead = &head->entry;
    LIST_ENTRY* Entry = ListHead->Flink;
    LIST_ENTRY* Flink = Entry->Flink;


    ListHead->Flink = Flink;
    Flink->Blink = ListHead;



    InterlockedDecrement64(&head->length);



    if (obtainedLocks == TRUE) {

        if (Entry == ListHead) {
            LeaveCriticalSection(&head->pageLock);
            release_srw_shared(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

        LeaveCriticalSection(&head->pageLock);

        LeaveCriticalSection(&nextHeadPage->lock);

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }


    return container_of(Entry, pfn, entry);

}

VOID addPageToTail(pListHead head, pfn* page, PTHREAD_INFO threadInfo) {
    boolean obtainedLocks = FALSE;

    pfn* nextPage;
    acquire_srw_shared(&head->sharedLock);

    if (head->length == 0) {
        EnterCriticalSection(&head->pageLock);
        EnterCriticalSection(&page->lock);

        head->entry.Blink = &page->entry;
        head->entry.Flink = &page->entry;

        page->entry.Blink = &page->entry;
        page->entry.Flink = &page->entry;

        LeaveCriticalSection(&page->lock);
        LeaveCriticalSection(&head->pageLock);
        release_srw_shared(&head->sharedLock);

        return;
    }


    for (int i = 0; i < 5; ++i) {
        if (TryEnterCriticalSection(&head->pageLock) == TRUE) {
            nextPage = container_of(head->entry.Blink, pfn, entry);

            if (TryEnterCriticalSection(&nextPage->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
        }

        LeaveCriticalSection(&head->pageLock);
    }

    if (obtainedLocks == FALSE) {
        release_srw_shared(&head->sharedLock);
        acquire_srw_exclusive(&head->sharedLock, threadInfo);
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
        LeaveCriticalSection(&head->pageLock);

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }
}