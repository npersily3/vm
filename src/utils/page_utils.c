//
// Created by nrper on 7/17/2025.
//
/**
 *@file page_utils.c
 *@brief This file contains functions used to manipulate lists. It makes use of slim-read-write locks to accomplish this task with little contention.
 *@author Noah Persily
*/
#include "../../include/utils/page_utils.h"
#include "../../include/variables/globals.h"
#include "utils/thread_utils.h"
#include "variables/macros.h"

VOID writePFN(pfn* pfnAddress,  pfn pfnNewContents) {
#if DBG
    recordPFNAccess(pfnAddress, pfnNewContents);
#endif

    *pfnAddress = pfnNewContents;
}
VOID recordPFNAccess(pfn* pfnAddress, pfn pfnNewContents) {

}


VOID
pfnInbounds(pfn* trimmed) {
    if (trimmed < vm.pfn.start || trimmed >= vm.pfn.end) {
        DebugBreak();
    }
}
// Simple pfn utils
ULONG64 getFrameNumber(pfn* pfn) {
    return (ULONG64)(pfn - vm.pfn.start);
}

pfn* getPFNfromFrameNumber(ULONG64 frameNumber) {
    return vm.pfn.start + frameNumber;
}

volatile ULONG64 prunecount;
volatile ULONG64 pagesremoved;

/**
 *@brief This function removes a batch of pages off of one list onto another instead of removing them one by one.
 * @param headToRemove A list head to remove pages from
 * @param headToAdd A list head to add the pages removed
 * @param threadInfo The info of the calling thread
 * @return The number of pages removed
 * @post All the pages added to the local list need to be unlocked
 */
#if 1
// todo Right now I am making a conscious design choice to back up immediately, I assume there is a smart way to back up, but right now I am just going to quit immeadiately
ULONG64 removeBatchFromList(pListHead headToRemove, pListHead headToAdd, PTHREAD_INFO threadInfo, ULONG64 number_of_pages) {

    pfn* firstPage;
    pfn* lastPage;
    boolean obtainedExclusive;

    volatile pfn* lockedPages[32] = {0};
    obtainedExclusive = FALSE;
    ULONG64 pageLocksNeeded = 0;



#if DBG

    validateList(headToAdd);

#endif

#define USE_SRW_LOCKS 1


#if USE_SRW_LOCKS
    acquire_srw_shared(&headToRemove->sharedLock);

    if (tryEnterPageLock(&headToRemove->page, threadInfo) == FALSE) {
        release_srw_shared(&headToRemove->sharedLock);
        acquire_srw_exclusive(&headToRemove->sharedLock, threadInfo);
        obtainedExclusive = TRUE;
    }
#else
    acquire_srw_exclusive(&headToRemove->sharedLock, threadInfo);
    obtainedExclusive = TRUE;
#endif


    ULONG64 number_of_pages_removed = 0;
    pfn* page;
    pageLocksNeeded = number_of_pages + 1;

    InterlockedIncrement64((volatile LONG64 *) &prunecount);

    page = container_of(headToRemove->entry.Flink, pfn, entry);

    // lock all the pages you can up until the threshold

    for (; number_of_pages_removed < pageLocksNeeded; number_of_pages_removed++) {

        if (&page->entry == &headToRemove->entry) {
            break;
        }


        // right now, just back up if you cannot get the lock
        if (tryEnterPageLock(page, threadInfo) == FALSE) {
            break;
        }
#if DBG
        page->lockedDuringPrune = TRUE;
        if (number_of_pages_removed < 32) {
            lockedPages[number_of_pages_removed] = page;
        }
#endif
        page = container_of(page->entry.Flink, pfn, entry);
    }


#if 0
    if (number_of_pages_removed == 1) {
        if (&page->entry != &headToRemove->entry) {
            // this is for the case where we only locked one page, but there were more on the list, so we could not safely update their blinks

        } else {
            //case where there is only one page on the list
            page->entry.Flink = &headToAdd->entry;
            page->entry.Blink = &headToAdd->entry;

            headToAdd->length = 1;
            headToAdd->entry.Flink = &page->entry;
            headToAdd->entry.Blink = &page->entry;

            headToRemove->length = 0;
            headToRemove->entry.Flink = &headToRemove->entry;
            headToRemove->entry.Blink = &headToRemove->entry;

        }
        leavePageLock(page, threadInfo);
    }
#else
    if (number_of_pages_removed == 1) {
        lastPage = container_of(page->entry.Blink, pfn, entry);
        leavePageLock(lastPage, threadInfo);

        number_of_pages_removed = 0;

    }
#endif

    if (number_of_pages_removed > 1) {

        number_of_pages_removed--;

        // right now, just back up if you cannot get the lock






        // i can edit the flinks and blinks here because all the pages have been lock
        //



            // the head and tail of the local list

            // page is the new first page of the list we are removing from
            // the other page locals pertain to the new list
            firstPage = container_of(headToRemove->entry.Flink, pfn, entry);

            page = container_of(page->entry.Blink, pfn, entry);
            lastPage = container_of(page->entry.Blink, pfn, entry);

            headToAdd->entry.Flink = &firstPage->entry;
            headToAdd->entry.Blink = &lastPage->entry;

            firstPage->entry.Blink = &headToAdd->entry;
            lastPage->entry.Flink = &headToAdd->entry;

            headToAdd->length = number_of_pages_removed;

            headToRemove->entry.Flink = &page->entry;

            if (&headToRemove->entry == &page->entry) {
                headToRemove->entry.Blink = &headToRemove->entry;
            } else {
                page->entry.Blink = &headToRemove->entry;
            }
#if DBG
        InterlockedAdd64( (volatile LONG64 *) &pagesremoved,(LONG64) number_of_pages_removed);
#endif


        InterlockedAdd64(&headToRemove->length, (LONG64)
             (0 - number_of_pages_removed));
        }


#if DBG

    for (int i = 0; i < number_of_pages_removed; ++i) {

    }

    validateList(headToAdd);

#endif

    if (obtainedExclusive == TRUE) {
        release_srw_exclusive(&headToRemove->sharedLock);
    } else {
        if (number_of_pages_removed > 1) {
            leavePageLock(page, threadInfo);
        }

        leavePageLock(&headToRemove->page, threadInfo);
        release_srw_shared(&headToRemove->sharedLock);
    }
    return number_of_pages_removed ;
}
#endif


// Called with page's pageLock held
/**
 * @pre Page must be locked
 * @post Page must be unlocked
 * @param head The head to remove from
 * @param page The page being removed
 * @param threadInfo The info of the calling thread to help debug lock errors
 */
VOID removeFromMiddleOfList(pListHead head,pfn* page, PTHREAD_INFO threadInfo) {




    pfn* Flink;
    pfn* Blink;
    boolean obtainedPageLocks;

    obtainedPageLocks = FALSE;



    acquire_srw_shared(&head->sharedLock);

    // try to only get pagelocks for a certain amount of attempts
    for (int i = 0; i < 5; ++i) {

        // reaching into the neighbors is safe because we have the pagelock
        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);

        if (tryEnterPageLock(Flink, threadInfo) == TRUE) {
            // set blink to null if there is only one page prevents double acquisitions
            if (Flink == Blink) {
                Blink = NULL;
                obtainedPageLocks = TRUE;
                break;
            }
            if (tryEnterPageLock(Blink, threadInfo) == TRUE) {
#if DBG
                //   validateList(head);
                ASSERT(Flink->entry.Blink == &page->entry)
                if (Blink != NULL) {
                    ASSERT(Blink->entry.Flink == &page->entry)
                }
#endif
                obtainedPageLocks = TRUE;
                break;
            }
            leavePageLock(Flink, threadInfo);
        }

    }


    // now we need to acquire exclusive
    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);


        acquire_srw_exclusive(&head->sharedLock, threadInfo);
        // we need to recalculate these because they could have changed inbetween the locks
        // another thread that acquires exclusively could have changed our page
        Flink = container_of(page->entry.Flink, pfn, entry);
        Blink = container_of(page->entry.Blink, pfn, entry);

        // set blink to null to trigger the right logic
        if (Flink == Blink) {
            Blink = NULL;
        }
    }

#if DBG
 //   validateList(head);
    ASSERT(Flink->entry.Blink == &page->entry)
    if (Blink != NULL) {
        ASSERT(Blink->entry.Flink == &page->entry)
    }
#endif



// actual list operation
    if (Blink == NULL) {
        ASSERT(head->length == 1)

        head->entry.Flink = &head->entry;
        head->entry.Blink = &head->entry;
    } else {
        ASSERT(head->length > 1)

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

/**
 * @post Page must be unlocked by the caller
 * @param head The head to remove from
 * @param threadInfo The info of the caller
 * @return The page at the head of list passed in
 * @retval Returns 0 if the list is empty
 */
pfn* RemoveFromHeadofPageList(pListHead head, PTHREAD_INFO threadInfo) {




    pfn* pageToRemove;
    pfn* flinkOfPageToRemove;
    boolean obtainedPageLocks;

    obtainedPageLocks = FALSE;



    acquire_srw_shared(&head->sharedLock);


    // try to get page locks
    for (int i = 0; i < 5; ++i) {

        enterPageLock(&head->page, threadInfo);
        pageToRemove = container_of(head->entry.Flink, pfn, entry);

        if (&pageToRemove->entry == &head->entry) {

            leavePageLock(&head->page, threadInfo);
            release_srw_shared(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

        if (tryEnterPageLock(pageToRemove, threadInfo) == FALSE) {
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


        if (tryEnterPageLock(flinkOfPageToRemove, threadInfo) == TRUE) {
            obtainedPageLocks = TRUE;
            break;
        }

        leavePageLock(pageToRemove, threadInfo);
        leavePageLock(&head->page, threadInfo);
    }

    // switch to exclusive mode
    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);


        // keep trying to acquire the page lock of the page we want to remove, then the list lock exclusive to maintain our order
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

            release_srw_exclusive(&head->sharedLock);
            leavePageLock(pageToRemove, threadInfo);
        }

        flinkOfPageToRemove = container_of(pageToRemove->entry.Flink, pfn, entry);
        // dont get listhead twice
        if (&flinkOfPageToRemove->entry == &head->entry) {
           flinkOfPageToRemove = NULL;
        }
    }

    ASSERT(pageToRemove != NULL);


#if DBG
   // validateList(head);
#endif
    // actual list operation

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

    // release the lock if neccesary
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

/**
 *
 * @param head Head to add to
 * @param page Page that is beeing added
 * @param threadInfo Thread info of the caller
 * @pre Page must be locked
 * @post Page must be locked
 */
VOID addPageToTail(pListHead head, pfn* page, PTHREAD_INFO threadInfo) {
    boolean obtainedPageLocks = FALSE;


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


        if (tryEnterPageLock(nextPage, threadInfo) == TRUE) {
            obtainedPageLocks = TRUE;
            break;
        }
        leavePageLock(&head->page, threadInfo);

    }


    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);



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

    // Actual list operations

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


// Release locks
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

