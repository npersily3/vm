//
// Created by nrper on 7/16/2025.
//

#include "../../include/threads/trimmer_thread.h"

#include <stdio.h>

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/utils/page_utils.h"
#include "../../include/utils/thread_utils.h"


DWORD page_trimmer(LPVOID threadContext) {
    ULONG64 BatchIndex;
    BOOL doubleBreak;
    PCRITICAL_SECTION trimmedPageTableLock;
    pfn* page;
    pfn* pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];
    PTE_REGION* region;

    HANDLE events[2];
    DWORD returnEvent;
    events[0] = trimmingStartEvent;
    events[1] = systemShutdownEvent;


    while (TRUE) {
        BatchIndex = 0;
        doubleBreak = FALSE;
        trimmedPageTableLock = NULL;

        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

        // while there is still stuff to trim and the arrays are not at capacity
        while (headModifiedList.length < NUMBER_OF_PHYSICAL_PAGES / 4 && BatchIndex < BATCH_SIZE) {
            page = getActivePage((PTHREAD_INFO) threadContext);

            if (page == NULL) {
                if (BatchIndex == 0) {
                    doubleBreak = TRUE;
                }
                break;
            }

            // if we are not on a streak
            if (trimmedPageTableLock == NULL) {

                region = getPTERegion(page->pte);
                trimmedPageTableLock = &region->lock;
                EnterCriticalSection(trimmedPageTableLock);
            } else {
                ASSERT(region == getPTERegion(page->pte))
            }


            virtualAddresses[BatchIndex] = (ULONG64) pte_to_va(page->pte);
            pages[BatchIndex] = page;
            pages[BatchIndex]->pte->transitionFormat.mustBeZero = 0;
            pages[BatchIndex]->pte->transitionFormat.contentsLocation = MODIFIED_LIST;
            BatchIndex++;

            if (isNextPageInSameRegion(region, (PTHREAD_INFO) threadContext) == FALSE) {
                unmapBatch(virtualAddresses, BatchIndex);
                addBatchToModifiedList(pages, BatchIndex, (PTHREAD_INFO) threadContext);

                LeaveCriticalSection(trimmedPageTableLock);
                trimmedPageTableLock = NULL;
                BatchIndex = 0;

            }
        }
        if (doubleBreak == TRUE) {
            SetEvent(writingStartEvent);
            continue;
        }
        if (BatchIndex == 0) {
            SetEvent(writingStartEvent);
            continue;
        }


        unmapBatch(virtualAddresses, BatchIndex);
        addBatchToModifiedList(pages, BatchIndex, (PTHREAD_INFO) threadContext);

        LeaveCriticalSection(trimmedPageTableLock);

        //nptodo add a condition around this
        SetEvent(writingStartEvent);


    }
}

pfn* getActivePage(PTHREAD_INFO threadContext) {


    pfn* page;

    page = RemoveFromHeadofPageList(&headActiveList, threadContext);


    if (page == LIST_IS_EMPTY) {
        return NULL;
    }
    leavePageLock(page, threadContext);
    return page;
}

BOOL isNextPageInSameRegion(PTE_REGION* region, PTHREAD_INFO info) {

    pfn* nextPage;
    PTE_REGION* nextRegion;
    sharedLock* lock;
    lock = &headActiveList.sharedLock;

    acquire_srw_exclusive(lock, info);
    if (headActiveList.length == 0) {
        release_srw_exclusive(lock);
        return FALSE;
    }
    nextPage = container_of(headActiveList.entry.Flink, pfn, entry);
    release_srw_exclusive(lock);

    nextRegion = getPTERegion(nextPage->pte);

    return nextRegion == region;
}
VOID unmapBatch (PULONG64 virtualAddresses, ULONG64 batchSize) {

    if (MapUserPhysicalPagesScatter((PVOID)virtualAddresses, batchSize, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
        return;
    }
}

VOID addBatchToModifiedList (pfn** pages, ULONG64 batchSize, PTHREAD_INFO threadContext) {

    pfn* page;


//    AcquireSRWLockExclusive(&headModifiedList.sharedLock);

    for (int i = 0; i < batchSize; ++i) {
        page = pages[i];
        enterPageLock(page, threadContext);
        addPageToTail(&headModifiedList, page, threadContext);
        leavePageLock(page, threadContext);
    //    InsertTailList(&headModifiedList, &page->entry);

    }
  //  ReleaseSRWLockExclusive(&headModifiedList.sharedLock);
}
