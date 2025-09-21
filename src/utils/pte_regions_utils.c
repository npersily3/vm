//
// Created by nrper on 9/20/2025.
//

#include "utils/pte_regions_utils.h"

#include "utils/pte_utils.h"
#include "variables/structures.h"
#include "utils/thread_utils.h"


// Called with page's pageLock held
/**
 * @pre Page must be locked
 * @post Page must be unlocked
 * @param head The head to remove from
 * @param page The page being removed
 * @param threadInfo The info of the calling thread to help debug lock errors
 */
VOID removeFromMiddleOfPageTableRegionList(pListHead head,PTE_REGION* region, PTHREAD_INFO threadInfo) {




    PTE_REGION* Flink;
    PTE_REGION* Blink;
    boolean obtainedPageLocks;

    obtainedPageLocks = FALSE;



    acquire_srw_shared(&head->sharedLock);

    // try to only get pagelocks for a certain amount of attempts
    for (int i = 0; i < 5; ++i) {

        // reaching into the neighbors is safe because we have the pagelock
        Flink = container_of(region->entry.Flink, PTE_REGION, entry);
        Blink = container_of(region->entry.Blink, PTE_REGION, entry);

        if (tryEnterPTERegionLock(Flink, threadInfo) == TRUE) {
            // set blink to null if there is only one page prevents double acquisitions
            if (Flink == Blink) {
                Blink = NULL;
                obtainedPageLocks = TRUE;
                break;
            }
            if (tryEnterPTERegionLock(Blink, threadInfo) == TRUE) {
#if DBG
                //   validateList(head);
                ASSERT(Flink->entry.Blink == &region->entry)
                if (Blink != NULL) {
                    ASSERT(Blink->entry.Flink == &region->entry)
                }
#endif
                obtainedPageLocks = TRUE;
                break;
            }
            leavePTERegionLock(Flink, threadInfo);
        }

    }


    // now we need to acquire exclusive
    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);


        acquire_srw_exclusive(&head->sharedLock, threadInfo);
        // we need to recalculate these because they could have changed inbetween the locks
        // another thread that acquires exclusively could have changed our page
        Flink = container_of(region->entry.Flink, PTE_REGION, entry);
        Blink = container_of(region->entry.Blink, PTE_REGION, entry);

        // set blink to null to trigger the right logic
        if (Flink == Blink) {
            Blink = NULL;
        }
    }

#if DBG
 //   validateList(head);
    ASSERT(Flink->entry.Blink == &region->entry)
    if (Blink != NULL) {
        ASSERT(Blink->entry.Flink == &region->entry)
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


        if (Blink != NULL) {
            leavePTERegionLock(Blink, threadInfo);
        }

        leavePTERegionLock(Flink, threadInfo);

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
PTE_REGION* RemoveFromHeadofRegionList(pListHead head, PTHREAD_INFO threadInfo) {




    PTE_REGION* regionToRemove;
    PTE_REGION* flinkOfRegionToRemove;
    boolean obtainedPageLocks;

    obtainedPageLocks = FALSE;



    acquire_srw_shared(&head->sharedLock);


    // try to get page locks
    for (int i = 0; i < 5; ++i) {

        // the pagelock s
        enterPTERegionLock(&head->region, threadInfo);
        
        regionToRemove = container_of(head->entry.Flink, PTE_REGION, entry);

        if (&regionToRemove->entry == &head->entry) {

            leavePTERegionLock(&head->region, threadInfo);
            release_srw_shared(&head->sharedLock);
            return LIST_IS_EMPTY;
        }

        if (tryEnterPTERegionLock(regionToRemove, threadInfo) == FALSE) {
            leavePTERegionLock(&head->region, threadInfo);
            continue;
        }

        // this is safe only because I hold the neighbors pagelock
        flinkOfRegionToRemove = container_of(regionToRemove->entry.Flink, PTE_REGION, entry);

        // if the list has one entry do not double acquire the listhead pagelock
        if (&flinkOfRegionToRemove->entry == &head->entry) {
            flinkOfRegionToRemove = NULL;
            obtainedPageLocks = TRUE;
            break;
        }


        if (tryEnterPTERegionLock(flinkOfRegionToRemove, threadInfo) == TRUE) {
            obtainedPageLocks = TRUE;
            break;
        }

        leavePTERegionLock(regionToRemove, threadInfo);
        leavePTERegionLock(&head->region, threadInfo);
    }

    // switch to exclusive mode
    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);


        // keep trying to acquire the page lock of the page we want to remove, then the list lock exclusive to maintain our order
        while (TRUE) {

            regionToRemove = container_of(head->entry.Flink, PTE_REGION, entry);

            if (&regionToRemove->entry == &head->entry) {
                return LIST_IS_EMPTY;
            }

            enterPTERegionLock(regionToRemove, threadInfo);

            acquire_srw_exclusive(&head->sharedLock, threadInfo);

            if (&regionToRemove->entry == head->entry.Flink) {
                break;
            }

            release_srw_exclusive(&head->sharedLock);
            leavePTERegionLock(regionToRemove, threadInfo);
        }

        flinkOfRegionToRemove = container_of(regionToRemove->entry.Flink, PTE_REGION, entry);
        // dont get listhead twice
        if (&flinkOfRegionToRemove->entry == &head->entry) {
           flinkOfRegionToRemove = NULL;
        }
    }

    ASSERT(regionToRemove != NULL);


#if DBG
   // validateList(head);
#endif
    // actual list operation

    LIST_ENTRY* ListHead = &head->entry;
    LIST_ENTRY* Entry = &regionToRemove->entry;

    if (flinkOfRegionToRemove == NULL) {
        ListHead->Flink = ListHead;
        ListHead->Blink = ListHead;
    } else {
        LIST_ENTRY* Flink = &flinkOfRegionToRemove->entry;

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

        if (flinkOfRegionToRemove != NULL) {
            leavePageLock(flinkOfRegionToRemove, threadInfo);
        }

       leavePTERegionLock(&head->region, threadInfo);



        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }


    ASSERT((ULONG64)head->page.lock.OwningThread != threadInfo->ThreadId);

    // leave with page lock held

    return regionToRemove;

}

/**
 *
 * @param head Head to add to
 * @param page Page that is beeing added
 * @param threadInfo Thread info of the caller
 * @pre Page must be locked
 * @post Page must be locked
 */
VOID addRegionToTail(pListHead head, PTE_REGION* region, PTHREAD_INFO threadInfo) {
    boolean obtainedPageLocks = FALSE;


    PTE_REGION* nextRegion;


    acquire_srw_shared(&head->sharedLock);


    for (int i = 0; i < 5; ++i) {
        enterPTERegionLock(&head->region, threadInfo);

        // this page is fine because we hold the heads pseudo page lock and the srw lock shared
        nextRegion = container_of(head->entry.Blink, PTE_REGION, entry);

        if (&nextRegion->entry == &head->entry) {
            nextRegion = NULL;
            obtainedPageLocks = TRUE;
            break;
        }


        if (tryEnterPTERegionLock(nextRegion, threadInfo) == TRUE) {
            obtainedPageLocks = TRUE;
            break;
        }
        leavePTERegionLock(&head->region, threadInfo);

    }


    if (obtainedPageLocks == FALSE) {


        release_srw_shared(&head->sharedLock);



        acquire_srw_exclusive(&head->sharedLock, threadInfo);
        //check if zero again
        nextRegion = container_of(head->entry.Blink, PTE_REGION, entry);

        if (&nextRegion->entry == &head->entry) {
            nextRegion = NULL;
        }
    }



#if DBG
   // validateList(head);
#endif

    // Actual list operations

    if (nextRegion == NULL) {
#if !useSharedLock
        ASSERT(head->length == 0);
#endif

        head->entry.Flink = &region->entry;
        head->entry.Blink = &region->entry;
        region->entry.Flink = &head->entry;
        region->entry.Blink = &head->entry;
    } else {


        head->entry.Blink = &region->entry;
        nextRegion->entry.Flink = &region->entry;
        region->entry.Blink = &nextRegion->entry;
        region->entry.Flink = &head->entry;
    }


    InterlockedIncrement64(&head->length);

#if DBG
  //  validateList(head);
#endif


// Release locks
    if (obtainedPageLocks == TRUE) {


        if (nextRegion != NULL) {
            leavePTERegionLock(nextRegion, threadInfo);
        }

        leavePTERegionLock(&head->page, threadInfo);


        release_srw_shared(&head->sharedLock);
    } else {
        release_srw_exclusive(&head->sharedLock);
    }
}

