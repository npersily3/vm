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
#include "initialization/init.h"
#include "threads/user_thread.h"

/**
 *@file trimmer_thread.c
 *@brief This file contains protocal for unmapping active pages and adding them to a modified list.
 *@author Noah Persily
*/

VOID trimRegion(PTE_REGION* currentRegion, PTHREAD_INFO threadContext) {

    pfn* pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];
    ULONG64 totalTrimmedPages;
    ULONG64 trimmedPagesInRegion;
    pte* currentPTE;
    pfn* page;
    ULONG64 age;

     currentPTE = getFirstPTEInRegion(currentRegion);

    trimmedPagesInRegion = 0;
    ULONG64 pteIndex = 0;

    for (; pteIndex < vm.config.number_of_ptes_per_region; pteIndex++) {

        // when we find a valid pte, invalidate it and store its info in stack variables
        pte localPTE;
        localPTE.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

        if (localPTE.validFormat.valid == 1) {

             age = localPTE.validFormat.age;

            ASSERT(currentRegion->numOfAge[age] != 0)


            if (localPTE.validFormat.access == 1) {
                localPTE.validFormat.access = 0;
                localPTE.validFormat.age = 0;
                currentRegion->numOfAge[age]--;
                currentRegion->numOfAge[0]++;

                writePTE(currentPTE, localPTE);


                currentPTE++;
                continue;
            }


            localPTE.transitionFormat.mustBeZero = 0;
            localPTE.transitionFormat.isTransition = 1;
            localPTE.transitionFormat.age = 0;
            writePTE(currentPTE, localPTE);
            currentRegion->numOfAge[age]--;
            currentRegion->numOfAge[0]++;



            page = getPFNfromFrameNumber(localPTE.transitionFormat.frameNumber);
            page->location = MODIFIED_LIST;

            virtualAddresses[trimmedPagesInRegion] = (ULONG64) pte_to_va(currentPTE);
            pages[trimmedPagesInRegion] = page;

            trimmedPagesInRegion++;
            totalTrimmedPages++;

        }

        currentPTE++;
    }
    currentRegion->hasActiveEntry = FALSE;

    unmapBatch(virtualAddresses, trimmedPagesInRegion);
    addBatchToModifiedList(pages, trimmedPagesInRegion, threadContext);


    InterlockedAdd64(&vm.pfn.numActivePages,0-trimmedPagesInRegion);
}





/**
 * @brief This is the function that deals with unmapping pages.
 * It will first retrieve all the pages from local caches and add them to the free list.
 * Then, it will comb through page table regions, batch unmapping all valid entries and adding them to the modified list.
 * It will batch unmap if the consecutive pages are in the same page table entry region.
 *
 * @param info A pointer to a thread info struct. Passed in during the function CreateThread
 * @retval 0 If the program succeeds
 */


#define VERBOSE 0
DWORD page_trimmer(LPVOID info) {

    ULONG64 counter;


    sharedLock* trimmedPageTableLock;
    pfn* page;
    pfn* pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];
    ULONG64 age;

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

      recallPagesFromLocalList(threadContext);

#if VERBOSE
        start = __rdtsc();
#endif

        // if we have trimmed enough, or have combed through everything
        while (totalTrimmedPages < BATCH_SIZE && counter < vm.config.number_of_pte_regions) {

            //check for overflow then wrap.
            if ((currentRegion - vm.pte.RegionsBase) == vm.config.number_of_pte_regions) {
                currentRegion = vm.pte.RegionsBase;
            }

            enterPTERegionLock(currentRegion, threadContext);

            // check to see if there are any active entries in this region
            if (currentRegion->hasActiveEntry == TRUE) {
                currentPTE = getFirstPTEInRegion(currentRegion);

                trimmedPagesInRegion = 0;
                ULONG64 pteIndex = 0;
                for (; pteIndex < vm.config.number_of_ptes_per_region; pteIndex++) {

                    // when we find a valid pte, invalidate it and store its info in stack variables
                    pte localPTE;
                    localPTE.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

                    if (localPTE.validFormat.valid == 1) {

                         age = localPTE.validFormat.age;

                        ASSERT(currentRegion->numOfAge[age] != 0)


                        if (localPTE.validFormat.access == 1) {
                            localPTE.validFormat.access = 0;
                            localPTE.validFormat.age = 0;
                            currentRegion->numOfAge[age]--;
                            currentRegion->numOfAge[0]++;

                            writePTE(currentPTE, localPTE);


                            currentPTE++;
                            continue;
                        }


                        localPTE.transitionFormat.mustBeZero = 0;
                        localPTE.transitionFormat.isTransition = 1;
                        localPTE.transitionFormat.age = 0;
                        writePTE(currentPTE, localPTE);
                        currentRegion->numOfAge[age]--;
                        currentRegion->numOfAge[0]++;



                        page = getPFNfromFrameNumber(localPTE.transitionFormat.frameNumber);
                        page->location = MODIFIED_LIST;

                        virtualAddresses[trimmedPagesInRegion] = (ULONG64) pte_to_va(currentPTE);
                        pages[trimmedPagesInRegion] = page;

                        trimmedPagesInRegion++;
                        totalTrimmedPages++;

                    }

                    currentPTE++;
                }
                currentRegion->hasActiveEntry = FALSE;



                unmapBatch(virtualAddresses, trimmedPagesInRegion);
                addBatchToModifiedList(pages, trimmedPagesInRegion, threadContext);

                // Need to have this after because I could fault it back in before it is on the modified list
               leavePTERegionLock(currentRegion, threadContext);

                InterlockedAdd64(&vm.pfn.numActivePages,0-trimmedPagesInRegion);
            } else {


               leavePTERegionLock(currentRegion, threadContext);
                currentRegion++;
                counter++;
            }


        }
#if VERBOSE
        end = __rdtsc();
        printf("trim time: %llu\n", start-end);
#endif

        vm.misc.numTrims ++;

        SetEvent(vm.events.writingStart);
    }
}

/**
 * @brief This function recalls pages from the local lists and adds them to the free lists.
 * @param trimThreadContext Thread info of the caller
 * @return The number of pages that were recalled from the local lists.
 */
ULONG64 recallPagesFromLocalList(PTHREAD_INFO trimThreadContext) {

    PTHREAD_INFO currentThreadContext;
    pfn* page;
    ULONG64 counter;
    listHead trimmerLocalList;
    PLIST_ENTRY entry;
    pListHead head;

    counter = 0;
    init_list_head(&trimmerLocalList);

    for (int i = 0; i < vm.config.number_of_user_threads; i++) {


        currentThreadContext = &vm.threadInfo.user[i];

        head = &currentThreadContext->localList;
        acquire_srw_exclusive(&head->sharedLock, trimThreadContext);



        // make order one
        while (&head->entry != head->entry.Flink) {
             entry = RemoveHeadList(head);
            page = container_of(entry, pfn, entry);
            InsertHeadList(&trimmerLocalList, &page->entry);
        }

        release_srw_exclusive(&head->sharedLock);

        //get info
        // get list lock

        //assemble local list

        // release list lock

        // add pages off of local to free lists


        head = &trimmerLocalList;
        while (&head->entry != head->entry.Flink) {
            entry = RemoveHeadList(head);
            page = container_of(entry, pfn, entry);
            addPageToFreeList(page, trimThreadContext);
            counter++;
        }
    }
    return counter;
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

