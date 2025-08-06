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

/**
 *@file trimmer_thread.c
 *@brief This file containsa protocal for unmapping active pages and adding them to a modified list.
 *@author Noah Persily
*/
/**
 * @brief This is the function that deals with unmapping pages.
 * It will pop a page off the active list and add it to the modified list.
 * It will batch unmap if the consecutive pages are in the same page table entry region.
 *
 * @param info A pointer to a thread info struct. Passed in during the function CreateThread
 * @retval 0 If the program succeeds
 */
DWORD page_trimmer(LPVOID info) {
    ULONG64 BatchIndex;
    BOOL doubleBreak;
    PCRITICAL_SECTION trimmedPageTableLock;
    pfn* page;
    pfn* pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];
    PTE_REGION* region;
    PTHREAD_INFO threadContext;
    threadContext = (PTHREAD_INFO)info;

    HANDLE events[2];
    DWORD returnEvent;
    events[0] = vm.events.trimmingStart;
    events[1] = vm.events.systemShutdown;


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
        while ((vm.lists.modified.length < vm.config.number_of_physical_pages / 4) && BatchIndex < BATCH_SIZE) {

            page = getActivePage(threadContext);

            if (page == NULL) {
                if (BatchIndex == 0) {
                    doubleBreak = TRUE;
                }
                break;
            }

            // if we are not on a streak we need to get a new region a lock the ptes
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

            if (isNextPageInSameRegion(region, threadContext) == FALSE) {
                unmapBatch(virtualAddresses, BatchIndex);
                addBatchToModifiedList(pages, BatchIndex,  threadContext);

                LeaveCriticalSection(trimmedPageTableLock);
                // Setting this to null tells the next loop to get a new region
                trimmedPageTableLock = NULL;
                BatchIndex = 0;

            }
        }
        if (doubleBreak == TRUE) {
            SetEvent(vm.events.writingStart);
            continue;
        }
        if (BatchIndex == 0) {
            SetEvent(vm.events.writingStart);
            continue;
        }

        //this just handles the last batch
        unmapBatch(virtualAddresses, BatchIndex);
        addBatchToModifiedList(pages, BatchIndex, (PTHREAD_INFO) threadContext);

        LeaveCriticalSection(trimmedPageTableLock);

        //nptodo add a condition around this
        SetEvent(vm.events.writingStart);


    }
}

/**
 * @brief This functions pops a page off the active list and returns it unlocked
 * @param threadContext The thread info of the caller.
 * @return Returns an active page. If the list is empty, it returns null.
 */
pfn* getActivePage(PTHREAD_INFO threadContext) {


    pfn* page;

    page = RemoveFromHeadofPageList(&vm.lists.active, threadContext);


    if (page == LIST_IS_EMPTY) {
        return NULL;
    }
    leavePageLock(page, threadContext);
    return page;
}

/**
 * @brief Simple wrapper for a MapUserPhysicalPagesScatter call.
 * @param virtualAddresses An array of virtual addresses to unmap.
 * @param batchSize The size of the array.
 */
VOID unmapBatch (PULONG64 virtualAddresses, ULONG64 batchSize) {

    if (MapUserPhysicalPagesScatter((PVOID)virtualAddresses, batchSize, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not unmap VA %llu\n", virtualAddresses[0]);
        return;
    }
}

/**
 * @brief This function adds a batch of pages to the modified list
 * @param pages An array of pointers to pages.
 * @param batchSize The size of the array.
 * @param threadContext The thread info of the caller
 */
// TODO maybe a assemble a local list and then add it to modified in order to get the lock less
VOID addBatchToModifiedList (pfn** pages, ULONG64 batchSize, PTHREAD_INFO threadContext) {

    pfn* page;

    for (int i = 0; i < batchSize; ++i) {
        page = pages[i];
        enterPageLock(page, threadContext);
        addPageToTail(&vm.lists.modified, page, threadContext);
        leavePageLock(page, threadContext);
    }
}

/**
 * @brief This function peeks into the head of the active list and sees if the next page is in the same pte region.
 * @param region The page table region struct of the batch that is currently being trimmed.
 * @param info The info of the caller.
 * @return Returns true if the next page's corresponding page table entry is in the region passed in.
 */
BOOL isNextPageInSameRegion(PTE_REGION* region, PTHREAD_INFO info) {

    pfn* nextPage;
    PTE_REGION* nextRegion;
    sharedLock* lock;
    lock = &vm.lists.active.sharedLock;

    // Peek ahead
    // TODO make this a no fence on the entry
    // acquire shared
    enterPageLock(&vm.lists.active.page, info);
    nextPage = container_of(&vm.lists.active.entry.Flink, pfn, entry);
    leavePageLock(&vm.lists.active.page, info);


    if (&vm.lists.active.entry == &nextPage->entry) {
        return LIST_IS_EMPTY;
    }

    nextRegion = getPTERegion(nextPage->pte);

    return nextRegion == region;
}
