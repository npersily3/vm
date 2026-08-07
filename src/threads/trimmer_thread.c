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
#include "threads/ager_thread.h"
#include "threads/user_thread.h"
#include "utils/pte_regions_utils.h"
#include "utils/stats.h"

/**
 *@file trimmer_thread.c
 *@brief This file contains protocal for unmapping active pages and adding them to a modified list.
 *@author Noah Persily
*/


#if CORRECTNESS

volatile PULONG64 correctness;

#endif

ULONG64 trimRegion(PTE_REGION *currentRegion, PTHREAD_INFO threadContext) {
    pfn *pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];
    ULONG64 trimmedPagesInRegion;
    pte *currentPTE;
    pfn *page;
    ULONG64 age;

    currentPTE = getFirstPTEInRegion(currentRegion);

    pte oldPTEContents;
    pte newPTEContents;
    pte pteAtTimeOfWrite;

    trimmedPagesInRegion = 0;
    ULONG64 pteIndex = 0;
    // for every pte
    for (; pteIndex < vm.config.pte_entries_per_pagetable; pteIndex++) {
        // when we find a valid pte, invalidate it and store its info in stack variables

        oldPTEContents.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

        while (true) {
            newPTEContents.entireFormat = oldPTEContents.entireFormat;

            if (oldPTEContents.validFormat.valid == 1 && oldPTEContents.validFormat.lock == 0) {
                age = oldPTEContents.validFormat.age;

                // if it is valid  we must clear the age but we cannot clear the access bit because that is unfair aging
                if (oldPTEContents.validFormat.access == 1) {
                    newPTEContents.validFormat.access = 1;
                    newPTEContents.validFormat.age = 0;

                    InterlockedDecrement64(&vm.pte.globalNumOfAge[age].count);
                    InterlockedIncrement64(&vm.pte.globalNumOfAge[0].count);

                    // walked all the way out here and got nothing: the pte was touched since aging
                    STAT_INC(threadContext, trimSkippedAccessed);

                    pteAtTimeOfWrite = writePTE(currentPTE, newPTEContents, oldPTEContents);


                    // we can break regardless of the result, because the only guy who can change it simultaneously is the access bit setter
                    // and we know that the access bit is already set, so no change will occur so that we can break regardless
                    break;
                }

                // this is for the case when the pte is valid and unnaccessed
                newPTEContents.transitionFormat.mustBeZero = 0;
                newPTEContents.transitionFormat.isTransition = 1;
                newPTEContents.transitionFormat.age = 0;
                pteAtTimeOfWrite = writePTE(currentPTE, newPTEContents, oldPTEContents);

                if (pteAtTimeOfWrite.entireFormat != oldPTEContents.entireFormat) {
                    oldPTEContents.entireFormat = pteAtTimeOfWrite.entireFormat;
                    continue;
                }

                // case where write succeeds

                InterlockedDecrement64(&vm.pte.globalNumOfAge[age].count);

                // the age this pte had reached when we took its page. Bunched at MAX_AGE means the
                // ager is doing its job; bunched at 0-1 means we are trimming pages that are still hot
                STAT_INC(threadContext, trimmedAge[age]);

                page = getPFNfromFrameNumber(oldPTEContents.transitionFormat.frameNumber);
                page->location = MODIFIED_LIST;
                virtualAddresses[trimmedPagesInRegion] = (ULONG64) pte_to_va(currentPTE);
                pages[trimmedPagesInRegion] = page;
                trimmedPagesInRegion++;
            }

            break;
        }
        currentPTE++;
    }
    // batched unmap and add to modified list
    unmapBatch(virtualAddresses, trimmedPagesInRegion);

#if CORRECTNESS

    // trigger tlb flush
    //VirtualProtect(correctness, PAGE_SIZE * CORRECTNESS_SIZE, PAGE_READONLY, NULL);

    volatile ULONG64 counter;

    for (int i = 0; i < CORRECTNESS_SIZE; i++) {
        counter = correctness[i * PAGE_SIZE / sizeof(ULONG64)];
    }
#endif


    addBatchToModifiedList(pages, trimmedPagesInRegion, threadContext);

    STAT_SAMPLE(threadContext, trimBatch, trimmedPagesInRegion);
    if (trimmedPagesInRegion == 0) {
        STAT_INC(threadContext, trimEmptyRegions);
    }

    return trimmedPagesInRegion;
}


PTE_REGION *getOldestRegion(PTHREAD_INFO threadContext) {
    LONG64 age;
    age = MAX_AGE;
    PTE_REGION *oldestRegion = NULL;

    for (; age >= 0; age--) {
        oldestRegion = RemoveFromHeadofRegionList(&vm.pte.ageList[age], threadContext);


        if (oldestRegion != NULL) {
            // which age list actually had a victim. Always landing on age 0 means we are out of slack
            STAT_INC(threadContext, victimFromAge[age]);
            return oldestRegion;
        }
    }

    STAT_INC(threadContext, victimNotFound);
    return NULL;
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
    SetThreadDescription(GetCurrentThread(), L"Trimmer");

    LARGE_INTEGER start, end;
    ULONG64 counter;

    PTHREAD_INFO threadContext;
    threadContext = (PTHREAD_INFO) info;
    PTE_REGION *currentRegion;

    ULONG64 trimmedPagesInRegion;
    ULONG64 totalTrimmedPages;


    HANDLE events[2];
    DWORD returnEvent;
    events[0] = vm.events.trimmingStart;
    events[1] = vm.events.systemShutdown;


    currentRegion = vm.pte.regions_base;

    ULONG64 numToTrimLocal;


#if CORRECTNESS

    correctness = VirtualAlloc(NULL, PAGE_SIZE * CORRECTNESS_SIZE, MEM_RESERVE | MEM_COMMIT,PAGE_READWRITE);

    if (correctness == NULL) {
        DebugBreak();
    }

    for (int i = 0; i < CORRECTNESS_SIZE; i++) {
        correctness[i * PAGE_SIZE / sizeof(ULONG64)] = 0;
    }

#endif

    boolean signaledWriter;

    while (TRUE) {
        totalTrimmedPages = 0;
        counter = 0;
        signaledWriter = FALSE;


        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }
        InterlockedExchange64(&vm.misc.trimmerPending, 0);
        QueryPerformanceCounter(&start);

        numToTrimLocal = ReadULong64NoFence(&vm.pte.numToTrim);

        // decrements numToTrimLocal by what it recalls, so if the local caches covered the whole
        // quota the trim loop below is simply skipped
        recallPagesFromLocalList(threadContext, &numToTrimLocal);


        // if we have trimmed enough, or have combed through everything
        while (totalTrimmedPages < numToTrimLocal) {
            // this function exits with the lock held
            currentRegion = getOldestRegion(threadContext);

            //nothing to trim
            if (currentRegion == NULL) {
                STAT_INC(threadContext, quotaShort);
                break;
            }


            LONG64 finalAge;

            // get a region, trim it, and move it to the tail of the age list
            trimmedPagesInRegion = trimRegion(currentRegion, threadContext);
            totalTrimmedPages += trimmedPagesInRegion;

            // wake the writer as soon as the first batch lands, instead of waiting for our whole
            // quota, so trimming and writing run concurrently (pipelined) rather than sequentially
            if (trimmedPagesInRegion > 0) {
                SetEvent(vm.events.writingStart);
                signaledWriter = TRUE;
            }

            // after I trim everything put it back on the appropriate age list, unless it has no
            // valid PTEs left (getRegionAge == -1)
            finalAge = getRegionAge(currentRegion);
            if (finalAge != -1) {
                addRegionToTail(&vm.pte.ageList[finalAge], currentRegion, threadContext);
            }


            leavePTERegionLock(currentRegion, threadContext);

            InterlockedAdd64(&vm.pfn.numActivePages, 0 - trimmedPagesInRegion);


            if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
                return 0;
            }
        }

        QueryPerformanceCounter(&end);

        recordWork(threadContext, end.QuadPart - start.QuadPart, totalTrimmedPages);

        if (totalTrimmedPages >= numToTrimLocal) {
            STAT_INC(threadContext, quotaMet);
        }

        InterlockedIncrement64((volatile LONG64 *) &vm.misc.numTrims);

        // fallback: if we never trimmed anything this cycle, the writer still needs a chance to
        // drain whatever's already on the modified list from before
        if (signaledWriter == FALSE) {
            SetEvent(vm.events.writingStart);
        }
    }
}

/**
 * @brief This function recalls pages from the local lists and adds them to the free lists.
 * @param trimThreadContext Thread info of the caller
 * @param numNeeded How many pages are still wanted. Decremented by every page recalled, so the
 *        caller sees exactly how much quota is left. Recalling past the quota is pure loss: each
 *        extra page just pushes that user thread back onto the slow free-list path on its next fault.
 */
VOID recallPagesFromLocalList(PTHREAD_INFO trimThreadContext, PULONG64 numNeeded) {
    PTHREAD_INFO currentThreadContext;
    pfn *page;
    listHead trimmerLocalList;
    PLIST_ENTRY entry;
    pListHead head;


    init_list_head(&trimmerLocalList);

    ULONG64 quotaOnEntry = *numNeeded;
    STAT_INC(trimThreadContext, recallWakeups);

    for (int i = 0; i < vm.config.number_of_user_threads && *numNeeded != 0; i++) {
        currentThreadContext = &vm.threadInfo.user[i];

        head = &currentThreadContext->localList;
        acquire_srw_exclusive(&head->sharedLock, trimThreadContext);


        // make order one, but stop as soon as we have staged what is left of the quota
        while (&head->entry != head->entry.Flink && (ULONG64) trimmerLocalList.length < *numNeeded) {
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
            (*numNeeded)--;
        }
    }

    STAT_ADD(trimThreadContext, recalled, quotaOnEntry - *numNeeded);
}


/**
 * @brief Simple wrapper for a MapUserPhysicalPagesScatter call.
 * @param virtualAddresses An array of virtual addresses to unmap.
 * @param batchSize The size of the array.
 */
VOID unmapBatch(PULONG64 virtualAddresses, ULONG64 batchSize) {
    if (MapUserPhysicalPagesScatter((PVOID) virtualAddresses, batchSize, NULL) == FALSE) {
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
VOID addBatchToModifiedList(pfn **pages, ULONG64 batchSize, PTHREAD_INFO threadContext) {
    pfn *page;

    for (int i = 0; i < batchSize; ++i) {
        page = pages[i];
        enterPageLock(page, threadContext);

        addPageToTail(&vm.lists.modified, page, threadContext);
        leavePageLock(page, threadContext);
    }
}
