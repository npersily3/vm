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
    boolean obtainedPageLocks;

    obtainedPageLocks = FALSE;
    boolean holdingSharedLock = TRUE;



    for (int i = 0; i < 5; ++i) {

        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);

        if (TryEnterCriticalSection(&Flink->lock) == TRUE) {
            if (Flink == Blink) {
                Blink = NULL;
                obtainedPageLocks = TRUE;
                break;
            }
            if (TryEnterCriticalSection(&Blink->lock) == TRUE) {
                obtainedPageLocks = TRUE;
                break;
            }
            leavePageLock(Flink, threadInfo);
        }

    }


    if (obtainedPageLocks == FALSE) {
        release_srw_shared(&head->sharedLock);
        holdingSharedLock = FALSE;

        acquire_srw_exclusive(&head->sharedLock, threadInfo);
        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);
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

    if (obtainedPageLocks == TRUE) {

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

    pfn* pageToRemove;
    pfn* flinkOfPageToRemove;
    boolean obtainedPageLocks;
    boolean holdingSharedLock;

    holdingSharedLock = TRUE;
    obtainedPageLocks = FALSE;



    for (int i = 0; i < 5; ++i) {
        enterPageLock(&head->page, threadInfo);
        pageToRemove = container_of(head->entry.Flink, pfn, entry);
         flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);

        if (&flinkOfPageToRemove->entry == &head->entry) {
            flinkOfPageToRemove = NULL;
        }

        if (&pageToRemove->entry == &head->entry) {
            pageToRemove = NULL;
            obtainedPageLocks = TRUE;
            break;
        }


        if (TryEnterCriticalSection(&pageToRemove->lock) == TRUE) {
            if (flinkOfPageToRemove != NULL) {
                if (TryEnterCriticalSection(&flinkOfPageToRemove->lock) == TRUE) {
                    obtainedPageLocks = TRUE;
                    break;
                }
            } else {
                obtainedPageLocks = TRUE;
                break;
            }

            leavePageLock(pageToRemove, threadInfo);
        }
        leavePageLock(&head->page, threadInfo);
    }


    if (obtainedPageLocks == FALSE) {

        pageToRemove = container_of(head->entry.Flink, pfn, entry);

        release_srw_shared(&head->sharedLock);
        holdingSharedLock = FALSE;

        // keep trying to acquire the page lock then the list lock exclusive to maintain our order
        while (TRUE) {

            if (&pageToRemove->entry == &head->entry) {
                return LIST_IS_EMPTY;
            }

            enterPageLock(pageToRemove, threadInfo);

            acquire_srw_exclusive(&head->sharedLock, threadInfo);


            if (&pageToRemove->entry == head->entry.Flink) {
                break;
            }

            pageToRemove = container_of(head->entry.Flink, pfn, entry);



            release_srw_exclusive(&head->sharedLock);
            leavePageLock(pageToRemove, threadInfo);
        }

        if (ReadULong64NoFence(&head->length) == 0) {
            release_srw_exclusive(&head->sharedLock);

            ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

            return LIST_IS_EMPTY;
        }
        flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);

        // nptodo do I need this check if i am now longer going to acquire the page lock
        if (&flinkOfPageToRemove->entry == &head->entry) {
           flinkOfPageToRemove = NULL;
        }

    }
    if (pageToRemove == NULL) {


        leavePageLock(&head->page, threadInfo);

        if (holdingSharedLock == TRUE) {
            release_srw_shared(&head->sharedLock);
        }   else {
            release_srw_exclusive(&head->sharedLock);
        }
        return LIST_IS_EMPTY;
    }



    LIST_ENTRY* ListHead = &head->entry;
    LIST_ENTRY* Entry = &pageToRemove->entry;

    if (flinkOfPageToRemove == NULL) {
        ListHead->Flink = ListHead;
        ListHead->Blink = ListHead;
    } else {
        LIST_ENTRY* Flink = &flinkOfPageToRemove->entry;

        ListHead->Flink = Flink;
        Flink->Blink = ListHead;
    }




    InterlockedDecrement64(&head->length);



    if (obtainedPageLocks == TRUE) {

        if (Entry == ListHead) {
            leavePageLock(&head->page, threadInfo);
            release_srw_shared(&head->sharedLock);

            ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

            return LIST_IS_EMPTY;
        }

       leavePageLock(&head->page, threadInfo);

        if (flinkOfPageToRemove != NULL) {
            LeaveCriticalSection(&flinkOfPageToRemove->lock);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }


    ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

    return container_of(Entry, pfn, entry);

}

VOID addPageToTail(pListHead head, pfn* page, PTHREAD_INFO threadInfo) {
    boolean obtainedPageLocks = FALSE;
    boolean holdingSharedLock = TRUE;

    pfn* nextPage;
    acquire_srw_shared(&head->sharedLock);




    for (int i = 0; i < 5; ++i) {
        if (TryEnterCriticalSection(&head->page.lock) == TRUE) {
            nextPage = container_of(head->entry.Blink, pfn, entry);

            if ((PVOID)nextPage == (PVOID)head) {
                nextPage = NULL;

                obtainedPageLocks = TRUE;
                break;
            }


            if (TryEnterCriticalSection(&nextPage->lock) == TRUE) {
                obtainedPageLocks = TRUE;
                break;
            }
            leavePageLock(&head->page, threadInfo);
        }


    }

    if (obtainedPageLocks == FALSE) {
        release_srw_shared(&head->sharedLock);
        holdingSharedLock = FALSE;

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


    if (obtainedPageLocks == TRUE) {


        leavePageLock(&head->page, threadInfo);

        if (nextPage != NULL) {
            LeaveCriticalSection(&nextPage->lock);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }
}