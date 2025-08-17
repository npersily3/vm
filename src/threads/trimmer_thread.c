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
#include "threads/user_thread.h"

/**
 *@file trimmer_thread.c
 *@brief This file contains protocal for unmapping active pages and adding them to a modified list.
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

#define VERBOSE 1
DWORD page_trimmer(LPVOID info) {

    ULONG64 counter;

    sharedLock* trimmedPageTableLock;
    pfn* page;
    pfn* pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];

    PTHREAD_INFO threadContext;
    threadContext = (PTHREAD_INFO)info;
    PTE_REGION* currentRegion;
    ULONG64 totalTrimmedPages;
    ULONG64 trimmedPagesInRegion;
    pte* currentPTE;

    HANDLE events[2];
    DWORD returnEvent;
    events[0] = vm.events.trimmingStart;
    events[1] = vm.events.systemShutdown;


    currentRegion = vm.pte.RegionsBase;

#if VERBOSE
    ULONG64 start;
    ULONG64 end;


#endif


    while (TRUE) {

        totalTrimmedPages = 0;
        counter = 0;
        trimmedPageTableLock = NULL;

        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

      //  recallPagesFromLocalList();

#if VERBOSE
        start = __rdtsc();
#endif

        while (totalTrimmedPages < BATCH_SIZE && counter < vm.config.number_of_pte_regions) {

            //check for overflow then wrap.
            if ((currentRegion - vm.pte.RegionsBase) == vm.config.number_of_pte_regions) {
                currentRegion = vm.pte.RegionsBase;
            }

            acquire_srw_exclusive(&currentRegion->lock, threadContext);

            if (currentRegion->hasActiveEntry == TRUE) {
                currentPTE = getFirstPTEInRegion(currentRegion);

                trimmedPagesInRegion = 0;
                for (int i = 0; i < vm.config.number_of_ptes_per_region; i++) {

                    // when we find a valid pte invalidate it and store its info
                    if (currentPTE->validFormat.valid == 1) {

                        currentPTE->transitionFormat.mustBeZero = 0;
                        currentPTE->transitionFormat.contentsLocation = MODIFIED_LIST;
                        page = getPFNfromFrameNumber(currentPTE->transitionFormat.frameNumber);

                        virtualAddresses[trimmedPagesInRegion] = (ULONG64) pte_to_va(currentPTE);
                        pages[trimmedPagesInRegion] = page;

                        trimmedPagesInRegion++;
                        totalTrimmedPages++;

                    }

                    currentPTE++;
                }
                unmapBatch(virtualAddresses, trimmedPagesInRegion);
                addBatchToModifiedList(pages, trimmedPagesInRegion, threadContext);
                currentRegion->hasActiveEntry = FALSE;
            }
            release_srw_exclusive(&currentRegion->lock);
            currentRegion++;
            counter++;

        }
#if VERBOSE
        end = __rdtsc();
        printf("trim time: %llu\n", start-end);
#endif

        vm.misc.numTrims ++;

        SetEvent(vm.events.writingStart);


    }
}

ULONG64 recallPagesFromLocalList(VOID) {
    ULONG64 time;
    ULONG64 prevTime;
    LARGE_INTEGER currentTime;
    PTHREAD_INFO currentThreadContext;
    pfn* page;
    ULONG64 counter;

    counter = 0;
    for (int i = 0; i < vm.config.number_of_user_threads; i++) {
        currentThreadContext = &vm.threadInfo.user[i];
        acquire_srw_exclusive(&currentThreadContext->localList.sharedLock, currentThreadContext);

        prevTime = currentThreadContext->localList.timeOfLastAccess;
        QueryPerformanceCounter(&currentTime);
        time = currentTime.QuadPart;
        if ((time - prevTime) > vm.config.time_until_recall_pages) {
            while (&currentThreadContext->localList.entry != currentThreadContext->localList.entry.Flink) {
                page = container_of(currentThreadContext->localList.entry.Flink, pfn, entry);
                addPageToFreeList(page, currentThreadContext);
                counter++;
            }

        }

        release_srw_exclusive(&currentThreadContext->localList.sharedLock);
    }
    return counter;
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
