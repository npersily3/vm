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

pfn* removeBatchFromList(pListHead headToRemove, pListHead headToAdd, PTHREAD_INFO threadInfo) {

    acquire_srw_shared(&headToRemove->sharedLock);
    enterPageLock(&headToRemove->page, threadInfo);

    ULONG64 number_of_pages_removed = 0;
    pfn* page;

    for (; number_of_pages_removed < NUMBER_OF_PAGES_TO_TRIM_FROM_STAND_BY; number_of_pages_removed++) {

        page = container_of(headToRemove->entry.Flink, pfn, entry);

        if (&page->entry == &headToRemove->entry) {
            break;
        }

        enterPageLock(page, threadInfo);
    }

    if (number_of_pages_removed == 0) {
        return LIST_IS_EMPTY;
    }

    InterlockedAdd64(&headToRemove->length, -1* (number_of_pages_removed));

    release_srw_shared(&headToRemove->sharedLock);
    leavePageLock(&headToRemove->page, threadInfo);

    headToAdd->entry.Flink = headToRemove->entry.Flink;
    headToAdd->entry.Blink = page->entry.Blink;
    headToAdd->length = number_of_pages_removed;

    return SUCCESS;
}

// Called with page's pageLock held
VOID removeFromMiddleOfList(pListHead head,pfn* page, PTHREAD_INFO threadInfo) {




    pfn* Flink;
    pfn* Blink;
    boolean obtainedPageLocks;

    obtainedPageLocks = FALSE;
    boolean holdingSharedLock = TRUE;


    acquire_srw_shared(&head->sharedLock);

    for (int i = 0; i < 5; ++i) {

        // reaching into the neighbors is safe because we have the pagelock
        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);

        if (TryEnterCriticalSection(&Flink->lock) == TRUE) {
            // set blink to null if there is only one page prevents double acquisitions
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
        // we need to recalculate these because they could have changed inbetween the locks
        // another thread that acquires exclusively could have changed our page
        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);
    }

#if DBG
 //   validateList(head);
    ASSERT(Flink->entry.Blink == &page->entry)
    ASSERT(Blink->entry.Flink == &page->entry)
#endif



    if (Blink == NULL) {
        ASSERT(head->length == 0)

        head->entry.Flink = &head->entry;
        head->entry.Blink = &head->entry;
    } else {
        ASSERT(head->length > 0)

        LIST_ENTRY* prevEntry = &Blink->entry;
        LIST_ENTRY* nextEntry = &Flink->entry;
        prevEntry->Flink = nextEntry;
        nextEntry->Blink = prevEntry;
    }


    InterlockedDecrement64(&head->length);


#if DBG
  //  validateList(head);
#endif


    if (obtainedPageLocks == TRUE) {

        leavePageLock(Flink, threadInfo);
        if (Blink != NULL) {
            leavePageLock(Blink, threadInfo);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }
    // we must still hold the pagelock other wise a concurrent operation on the same page would corrupt data
}


pfn* RemoveFromHeadofPageList(pListHead head, PTHREAD_INFO threadInfo) {




    pfn* pageToRemove;
    pfn* flinkOfPageToRemove;
    boolean obtainedPageLocks;
    boolean holdingSharedLock;

    holdingSharedLock = TRUE;
    obtainedPageLocks = FALSE;



    acquire_srw_shared(&head->sharedLock);


    for (int i = 0; i < 5; ++i) {

        enterPageLock(&head->page, threadInfo);
        pageToRemove = container_of(head->entry.Flink, pfn, entry);

        if (&pageToRemove->entry == &head->entry) {

            leavePageLock(&head->page, threadInfo);
            release_srw_shared(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

        if (TryEnterCriticalSection(&pageToRemove->lock) == FALSE) {
            leavePageLock(&head->page, threadInfo);
            continue;
        }

        // this is safe only because I hold the neighbors pagelock
        flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);

        // if the list has one entry do not double acquire the listhead pagelock
        if (&flinkOfPageToRemove->entry == &head->entry) {
            flinkOfPageToRemove = NULL;
            obtainedPageLocks = TRUE;
            break;
        }


        if (TryEnterCriticalSection(&flinkOfPageToRemove->lock) == TRUE) {
            obtainedPageLocks = TRUE;
            break;
        }

        leavePageLock(pageToRemove, threadInfo);
        leavePageLock(&head->page, threadInfo);
    }


    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);


        holdingSharedLock = FALSE;

        // keep trying to acquire the page lock then the list lock exclusive to maintain our order
        while (TRUE) {

            pageToRemove = container_of(head->entry.Flink, pfn, entry);

            if (&pageToRemove->entry == &head->entry) {
                return LIST_IS_EMPTY;
            }

            enterPageLock(pageToRemove, threadInfo);

            acquire_srw_exclusive(&head->sharedLock, threadInfo);

            if (&pageToRemove->entry == head->entry.Flink) {
                break;
            }
//fggf
            release_srw_exclusive(&head->sharedLock);
            leavePageLock(pageToRemove, threadInfo);
        }

        flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);

        if (&flinkOfPageToRemove->entry == &head->entry) {
           flinkOfPageToRemove = NULL;
        }
    }

    ASSERT(pageToRemove != NULL);


#if DBG
   // validateList(head);
#endif

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


#if DBG
   // validateList(head);
#endif

    if (obtainedPageLocks == TRUE) {

        ASSERT(Entry != ListHead)

       leavePageLock(&head->page, threadInfo);

        if (flinkOfPageToRemove != NULL) {
            LeaveCriticalSection(&flinkOfPageToRemove->lock);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }


    ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

    // leave with page lock held

    return pageToRemove;

}

// page lock is held
VOID addPageToTail(pListHead head, pfn* page, PTHREAD_INFO threadInfo) {
    boolean obtainedPageLocks = FALSE;
    boolean holdingSharedLock = TRUE;

    pfn* nextPage;


    acquire_srw_shared(&head->sharedLock);


    for (int i = 0; i < 5; ++i) {
        enterPageLock(&head->page, threadInfo);

        // this page is fine because we hold the heads pseudo page lock and the srw lock shared
        nextPage = container_of(head->entry.Blink, pfn, entry);

        if (&nextPage->entry == &head->entry) {
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


    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);

        holdingSharedLock = FALSE;

        acquire_srw_exclusive(&head->sharedLock, threadInfo);
        //check if zero again
        nextPage = container_of(head->entry.Blink, pfn, entry);

        if (&nextPage->entry == &head->entry) {
            nextPage = NULL;
        }
    }



#if DBG
   // validateList(head);
#endif

    if (nextPage == NULL) {
#if !useSharedLock
        ASSERT(head->length == 0);
#endif

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

#if DBG
  //  validateList(head);
#endif



    if (obtainedPageLocks == TRUE) {


        leavePageLock(&head->page, threadInfo);

        if (nextPage != NULL) {
           leavePageLock(nextPage, threadInfo);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }
}




// this assumes the heads pagelock is held, and will exit with it held
pfn* RemoveFromHeadWithListLockHeld(pListHead head, PTHREAD_INFO threadInfo) {




    pfn* pageToRemove;
    pfn* flinkOfPageToRemove;
    boolean obtainedPageLocks;
    boolean holdingSharedLock;

    holdingSharedLock = TRUE;
    obtainedPageLocks = FALSE;



    acquire_srw_shared(&head->sharedLock);


    for (int i = 0; i < 5; ++i) {


        pageToRemove = container_of(head->entry.Flink, pfn, entry);

        if (&pageToRemove->entry == &head->entry) {


            release_srw_shared(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

        if (TryEnterCriticalSection(&pageToRemove->lock) == FALSE) {

            continue;
        }

        // this is safe only because I hold the neighbors pagelock
        flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);

        // if the list has one entry do not double acquire the listhead pagelock
        if (&flinkOfPageToRemove->entry == &head->entry) {
            flinkOfPageToRemove = NULL;
            obtainedPageLocks = TRUE;
            break;
        }


        if (TryEnterCriticalSection(&flinkOfPageToRemove->lock) == TRUE) {
            obtainedPageLocks = TRUE;
            break;
        }

        leavePageLock(pageToRemove, threadInfo);

    }


    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);


        holdingSharedLock = FALSE;

        // keep trying to acquire the page lock then the list lock exclusive to maintain our order
        while (TRUE) {

            pageToRemove = container_of(head->entry.Flink, pfn, entry);

            if (&pageToRemove->entry == &head->entry) {
                return LIST_IS_EMPTY;
            }

            enterPageLock(pageToRemove, threadInfo);

            acquire_srw_exclusive(&head->sharedLock, threadInfo);

            if (&pageToRemove->entry == head->entry.Flink) {
                break;
            }
//fggf
            release_srw_exclusive(&head->sharedLock);
            leavePageLock(pageToRemove, threadInfo);
        }

        flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);

        if (&flinkOfPageToRemove->entry == &head->entry) {
           flinkOfPageToRemove = NULL;
        }
    }

    ASSERT(pageToRemove != NULL);


#if DBG
   // validateList(head);
#endif

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


#if DBG
   // validateList(head);
#endif

    if (obtainedPageLocks == TRUE) {

        ASSERT(Entry != ListHead)



        if (flinkOfPageToRemove != NULL) {
            LeaveCriticalSection(&flinkOfPageToRemove->lock);
        }

        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }


    ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

    // leave with page lock held

    return pageToRemove;

}