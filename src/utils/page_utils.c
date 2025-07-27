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

        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);

        if (TryEnterCriticalSection(&Flink->lock) == TRUE) {
            if (Flink == Blink) {
                Blink = NULL;
                obtainedLocks = TRUE;
                break;
            }
            if (TryEnterCriticalSection(&Blink->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
            leavePageLock(Flink, threadInfo);
        }

    }


    if (obtainedLocks == FALSE) {
        release_srw_shared(&head->sharedLock);
        acquire_srw_exclusive(&head->sharedLock, threadInfo);
    }
    if (Blink == NULL) {
        head->entry.Flink = &head->entry;
        head->entry.Blink = &head->entry;
    } else {
        LIST_ENTRY* prevEntry = &Blink->entry;
        LIST_ENTRY* nextEntry = &Flink->entry;
        prevEntry->Flink = nextEntry;
        nextEntry->Blink = prevEntry;
    }


    InterlockedDecrement64(&head->length);

    if (obtainedLocks == TRUE) {

        leavePageLock(Flink, threadInfo);
        if (Blink != NULL) {
            leavePageLock(Blink, threadInfo);
        }

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
        enterPageLock(&head->page, threadInfo);
        currentHeadPage = container_of(head->entry.Flink, pfn, entry);
         nextHeadPage = container_of(currentHeadPage->entry.Flink, pfn, entry);

        if (&nextHeadPage->entry == &head->entry) {
            nextHeadPage = NULL;
        }

        if (&currentHeadPage->entry == &head->entry) {
            currentHeadPage = NULL;
            obtainedLocks = TRUE;
            break;
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

            leavePageLock(currentHeadPage, threadInfo);
        }
        leavePageLock(&head->page, threadInfo);
    }


    if (obtainedLocks == FALSE) {

        currentHeadPage = container_of(head->entry.Flink, pfn, entry);

        release_srw_shared(&head->sharedLock);

        while (TRUE) {

            if (&currentHeadPage->entry == &head->entry) {
                return LIST_IS_EMPTY;
            }

            enterPageLock(currentHeadPage, threadInfo);

            acquire_srw_exclusive(&head->sharedLock, threadInfo);


            if (&currentHeadPage->entry == head->entry.Flink) {
                break;
            }

            currentHeadPage = container_of(head->entry.Flink, pfn, entry);



            release_srw_exclusive(&head->sharedLock);
            leavePageLock(currentHeadPage, threadInfo);
        }

        if (ReadULong64NoFence(&head->length) == 0) {
            release_srw_exclusive(&head->sharedLock);

            ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

            return LIST_IS_EMPTY;
        }

    }
    if (&currentHeadPage == NULL) {
        leavePageLock(&head->page, threadInfo);
        release_srw_shared(&head->sharedLock);

        return LIST_IS_EMPTY;
    }

    LIST_ENTRY* ListHead = &head->entry;
    LIST_ENTRY* Entry = ListHead->Flink;
    LIST_ENTRY* Flink = Entry->Flink;


    ListHead->Flink = Flink;
    Flink->Blink = ListHead;



    InterlockedDecrement64(&head->length);



    if (obtainedLocks == TRUE) {

        if (Entry == ListHead) {
            leavePageLock(&head->page, threadInfo);
            release_srw_shared(&head->sharedLock);

            ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

            return LIST_IS_EMPTY;
        }

       leavePageLock(&head->page, threadInfo);

        if (nextHeadPage != NULL) {
            LeaveCriticalSection(&nextHeadPage->lock);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }


    ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

    return container_of(Entry, pfn, entry);

}

VOID addPageToTail(pListHead head, pfn* page, PTHREAD_INFO threadInfo) {
    boolean obtainedLocks = FALSE;

    pfn* nextPage;
    acquire_srw_shared(&head->sharedLock);




    for (int i = 0; i < 5; ++i) {
        if (TryEnterCriticalSection(&head->page.lock) == TRUE) {
            nextPage = container_of(head->entry.Blink, pfn, entry);

            if ((PVOID)nextPage == (PVOID)head) {
                nextPage = NULL;

                obtainedLocks = TRUE;
                break;
            }


            if (TryEnterCriticalSection(&nextPage->lock) == TRUE) {
                obtainedLocks = TRUE;
                break;
            }
            leavePageLock(&head->page, threadInfo);
        }


    }

    if (obtainedLocks == FALSE) {
        release_srw_shared(&head->sharedLock);
        acquire_srw_exclusive(&head->sharedLock, threadInfo);
        //check if zero again
        nextPage = container_of(head->entry.Blink, pfn, entry);

        if ((PVOID) nextPage == (PVOID) head) {
            nextPage = NULL;
        }
    }

    if (nextPage == NULL) {
        head->entry.Flink = &page->entry;
        head->entry.Blink = &page->entry;
        page->entry.Flink = &head->entry;
        page->entry.Blink = &head->entry;
    } else {
        head->entry.Blink = &page->entry;
        nextPage->entry.Flink = &page->entry;
        page->entry.Blink = &nextPage->entry;
        page->entry.Flink = &head->entry;
    }



    InterlockedIncrement64(&head->length);


    if (obtainedLocks == TRUE) {


        leavePageLock(&head->page, threadInfo);

        if (nextPage != NULL) {
            LeaveCriticalSection(&nextPage->lock);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }
}