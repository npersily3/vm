//
// Created by nrper on 7/16/2025.
//

#include "../../include/threads/user_thread.h"
#include <stdio.h>
#include <utils/random_utils.h>

#include "../../include/variables/globals.h"
#include "../../include/variables/structures.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/utils/page_utils.h"
#include "../../include/disk/disk.h"
#include "../../include/utils/thread_utils.h"
#include "initialization/init.h"
#include "utils/pte_regions_utils.h"

#if spinEvents

/**
 * * @brief This function spins while waiting so waits show up as time on a performance trace
 */
VOID spinWhileWaiting(VOID) {

    DWORD status;

    while (TRUE) {
        status = WaitForSingleObject(vm.events.writingEnd, 0);
        if (status != WAIT_TIMEOUT) {
            return;
        }
    }

}
#endif

/**
 * @brief Adds a page to any available freelist.
 * @param page The page you are inserting into the list
 * @param threadInfo The thread info of a caller. It is mainly used for debug functions
* @pre The page  must be locked.
 * @post The caller must release the page lock.
 */


VOID addPageToFreeList(pfn *page, PTHREAD_INFO threadInfo) {
    ULONG64 localFreeListIndex;


    // randomly add to a free list
    localFreeListIndex = GetNextRandom(&threadInfo->rng) % vm.config.number_of_free_lists;
    InterlockedIncrement64((volatile LONG64 *) &vm.lists.free.length);

    // iterate through the freelists starting at the index calculated above and try to lock it
    while (TRUE) {
        // once you lock a free list insert it at the head
        if (tryEnterPageLock(&vm.lists.free.heads[localFreeListIndex].page, threadInfo) == TRUE) {
#if DBG
            // since there are no remove from middle calls, we can validate the list
            validateList(&vm.lists.free.heads[localFreeListIndex]);
#endif


            InsertHeadList(&vm.lists.free.heads[localFreeListIndex], &page->entry);

#if DBG
            validateList(&vm.lists.free.heads[localFreeListIndex]);
#endif

            leavePageLock(&vm.lists.free.heads[localFreeListIndex].page, threadInfo);

            return;
        }
        // increment and wrap
        localFreeListIndex = (localFreeListIndex + 1) % vm.config.number_of_free_lists;
    }
}

/**
 * @brief Removes a batch of victims from the standby list and puts them on the free list.
 * @param threadInfo The thread info of a caller. It is mainly used for debug functions.
 */
VOID batchVictimsFromStandByList(PTHREAD_INFO threadInfo) {
    listHead localList;
    pfn *page;
    pfn *nextPage;
    pte localPTE;
    pte *currentPTE;


    init_list_head(&localList);


    // adds pages onto the local list from the standby list but does not update them
    // returns with all the pages locked
    if (removeBatchFromList(&vm.lists.standby, &localList, threadInfo, vm.config.number_of_pages_to_trim_from_stand_by)
        == LIST_IS_EMPTY) {
        return;
    }

#if DBG
    validateList(&localList);
#endif


    page = container_of(localList.entry.Flink, pfn, entry);
    // For every page on the local list

    while (&page->entry != &localList.entry) {
        // We can do this without a pte lock because the page lock protects it from rescues
        currentPTE = page->pte;
        localPTE.entireFormat = 0;
        localPTE.invalidFormat.isTransition = 0;
        localPTE.invalidFormat.diskIndex = page->diskIndex;

#if DBG
        page->diskIndex = 0;
#endif

        writePTE(currentPTE, localPTE);

        // cannot have this line because we need to store the contents
        // set_disk_space_free(page->diskIndex);

        // add it to the list and manage locks
        nextPage = container_of(page->entry.Flink, pfn, entry);
        addPageToFreeList(page, threadInfo);
        leavePageLock(page, threadInfo);
        page = nextPage;
    }
    ASSERT(threadInfo->pagelocksHeld == 0)
}

/**
 * @brief This function decides whether to prune or not based on a global boolean that is kept track of with interlocked operations.
 *
 * @param threadInfo  The thread info of the caller. It is mainly used for debug functions.
 *
 */
VOID checkIfPrune(PTHREAD_INFO threadInfo) {
    // if there is not a pruning mission already happening
    BOOL pruneInProgress = (BOOL) InterlockedCompareExchange(
        (volatile LONG *) &vm.misc.standByPruningInProgress, TRUE, FALSE);

    if (pruneInProgress == FALSE) {
        batchVictimsFromStandByList(threadInfo);
        InterlockedExchange((volatile LONG *) &vm.misc.standByPruningInProgress, FALSE);
    }
}

/**
 *@brief A function that gets the current transfer virtual address that a thread needs.
 *@param threadContext The thread info of the caller. It is used to update the per thread index into its transfer virtual address space.
 *@return A pointer to a page of unmapped transfer virtual address space.
*/
PVOID getThreadMapping(PTHREAD_INFO threadContext) {
    // Get the current thread's transfer va space
    PVOID currentTransferVa = vm.va.userThreadTransfer[threadContext->ThreadNumber];
    // Index into it
    PVOID va = (PVOID) ((ULONG64) currentTransferVa + PAGE_SIZE * threadContext->TransferVaIndex);

    threadContext->TransferVaIndex += 1;
    ASSERT(threadContext->TransferVaIndex <= vm.config.size_of_user_thread_transfer_va_space_in_pages);

    return va;
}

/**
 * @brief Unmaps the thread's transfer virtual address space if it has all mapped. Batching it in this way saves a lot of time spent mapping and flushing.
 * @param threadContext The thread info of the caller. It is used to index into the correct transfer virtual address space.
 */
VOID freeThreadMapping(PTHREAD_INFO threadContext) {
    // If we are at the end, reset and unmap everything
    if (threadContext->TransferVaIndex == vm.config.size_of_user_thread_transfer_va_space_in_pages) {
        threadContext->TransferVaIndex = 0;

        if (MapUserPhysicalPages(vm.va.userThreadTransfer[threadContext->ThreadNumber],
                                 vm.config.size_of_user_thread_transfer_va_space_in_pages, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n",
                   vm.va.userThreadTransfer[threadContext->ThreadNumber]);
            return;
        }
    }
}

/**
 *@brief This function is the core of the program. It will culminate in the mapping of a physical page to the passed in virtual address.
 *@param arbitrary_va The virtual address that is being accessed and faulted on.
 *@param threadContext The information about the faulting thread.
 *@return A boolean that determines whether we need to back up and redo the fault.
*/

BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID threadContext) {
    BOOL returnValue;
    pte *currentPTE;
    pte pteContents;
    ULONG64 frameNumber;
    pte newPTE;
    ULONG64 regionStatus;


    currentPTE = va_to_pte((ULONG64) arbitrary_va);


    // Checks to see if the user is accessing out of bounds va space
    if (isVaValid((ULONG64) arbitrary_va) == FALSE) {
        DebugBreak();
        printf("invalid va %p\n", arbitrary_va);
    }


    returnValue = !REDO_FAULT;


    lockPTE(currentPTE);
    pteContents = *currentPTE;

    // If the page fault was already handled by another thread
    if (pteContents.validFormat.valid != TRUE) {
        // If the pte is in flight in the system threads
        if ((currentPTE->transitionFormat.isTransition == TRUE)) {
            // Determine if the rescue told us to back up or not
            frameNumber = rescue_page((ULONG64) arbitrary_va, currentPTE, (PTHREAD_INFO) threadContext);
            if (frameNumber == REDO_FAULT) {
                returnValue = REDO_FAULT;
            }
        } else {
            // If we are on a first fault or a disk read map that page.
            frameNumber = mapPage((ULONG64) arbitrary_va, currentPTE, threadContext);
            if (frameNumber == REDO_FAULT) {
                returnValue = REDO_FAULT;
            }
        }


        // keeps track of pte region so that trimmer can skip blank ones
        if (returnValue != REDO_FAULT) {
            PTE_REGION *region = getPTERegion(currentPTE);

            regionStatus = region->hasActiveEntry;



            InterlockedIncrement64(&vm.pfn.numActivePages);


            if (MapUserPhysicalPages((PVOID) arbitrary_va, 1, &frameNumber) == FALSE) {
                DebugBreak();
                printf("rfull_virtual_memory_test : could not map VA %p to page %llX\n", (PVOID) arbitrary_va,
                       frameNumber);
                return FALSE;
            }

            // if this is the first active pte make it an aging candidate
            if (regionStatus == FALSE) {
                addRegionToTail(&vm.pte.ageList[0], region, threadContext);
            }
            region->hasActiveEntry = TRUE;
            region->numOfAge[0]++;

            newPTE.entireFormat = 0;
            newPTE.validFormat.frameNumber = frameNumber;
            newPTE.validFormat.valid = 1;
            newPTE.validFormat.access = 1;
            newPTE.validFormat.age = 0;
            writePTE(currentPTE, newPTE);
        }
    }


    unlockPTE(currentPTE);


    return returnValue;
}


/**
 * @brief This function will rescue a page table entry that has been unmapped by the trimming thread, but the contents of the virtual page are still on the physical page.
 * If the page corresponding to the virtual address is on the modified list, this function just simply removes it from the middle.
 * If the page is on the standby list, this function removes it from the middle of the list and sets the corresponding disk space free.
 * Lastly, If the page is not on any list, but in the process of being written out, just mark it as no longer being written out.
 * The writer will see this and free the corresponding disk space
 *
 * @param arbitrary_va The virtual address that is faulted on.
 * @param currentPTE  The page table entry corresponding with the virtual address.
 * @param threadInfo The info of the thread that is faulted on.
 * @return Returns true if we need to redo the fault, false otherwise.
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry
 */
ULONG64 rescue_page(ULONG64 arbitrary_va, pte *currentPTE, PTHREAD_INFO threadInfo) {
    pfn *page;
    ULONG64 frameNumber;

    pte entryContents;

    // We must read no fence because the pte could be getting cross edited by another faulter who is using it as a victim
    entryContents.entireFormat = ReadULong64NoFence((DWORD64 const volatile *) currentPTE);


    // The following two checks see if the case where a pte is cross edited in the process of victimization occured

    // If before we got our snapshot, the pte was changed to non rescue
    if (entryContents.transitionFormat.isTransition == 0) {
        return REDO_FAULT;
    }

    frameNumber = entryContents.transitionFormat.frameNumber;
    page = getPFNfromFrameNumber(frameNumber);

    enterPageLock(page, threadInfo);

    // Now that we are in the pagelock we can be sure the pte is not being edited without a lock in our victimization code.
    // Therefore, we can now check the actual pte for the same things as above as opposed to our copy

    if (page->pte != currentPTE) {
        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }

    if (entryContents.entireFormat != currentPTE->entireFormat) {
        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }
    ASSERT(currentPTE->transitionFormat.frameNumber == frameNumber);

    // If there is a write in progress the page is not on any list
    // We do not know when in the right this is occurring, so we set this variable to signal to the writer to not add this
    // page to the standby list
    // Instead, the writer will free its disk space
    if (page->isBeingWritten == TRUE) {
#if DBG
    page->hasBeenRescuedWhileWritten = 1;

#endif
        page->isBeingWritten = FALSE;
    }
    // If its on standby, remove it and set the disk space free
    else if (page->location == STAND_BY_LIST) {
        removeFromMiddleOfPageList(&vm.lists.standby, page, threadInfo);

        set_disk_space_free(page->diskIndex);
#if DBG
        page->diskIndex = 0;
#endif
    } else {
        // It must be on the modified list now if it was determined to be a rescue but no a write in progress or a standby page
        removeFromMiddleOfPageList(&vm.lists.modified, page, threadInfo);
    }


    // we can leave this lock because it is no longer on a list, so no other operations like a list removal but be poking at this page
    // A faulter that is accessing the same region will be stopped by a pagetable lock
    leavePageLock(page, threadInfo);

    InterlockedIncrement64(&vm.misc.numRescues);


    return frameNumber;
}


/**
 * @brief This function handles the first time a virtual address is accessed, and if a virtual address contents are on disk.
 * First this thread will attempt to map from the free list; then it will try to map from standby. If none of those succeed it will wake up the trimmer and wait to redo the fault.
 * If this thread notices that usable pages are getting low, it will wake up the trimmer.
 * @param arbitrary_va The virtual address being accessed.
 * @param currentPTE The page table entry of the virtual address being accessed.
 * @param threadContext The thread info of the faulting thread.
 * @return Returns true if we need to redo the fault, false otherwise.
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry.
 */

ULONG64 mapPage(ULONG64 arbitrary_va, pte *currentPTE, LPVOID threadContext) {
    pfn *page;
    ULONG64 frameNumber;
    PTHREAD_INFO threadInfo;


    threadInfo = (PTHREAD_INFO) threadContext;


    page = NULL;

    // Attempt to get a page from the free list and standby list
    // NULL means the page could not be found


    page = getPageFromLocalList(threadInfo);
    if (page == NULL) {
        page = getPageFromFreeList(threadInfo);

        if (page == NULL) {
            page = getVictimFromStandByList(threadInfo);

            if (page == NULL) {
                // If we are not able to get a page, start the trimmer and sleep

                unlockPTE(currentPTE);


                ResetEvent(vm.events.writingEnd);
                SetEvent(vm.events.trimmingStart);


                vm.misc.pageWaits++;

                ULONG64 start;
                ULONG64 end;

                start = ReadTimeStampCounter();
#if spinEvents
                spinWhileWaiting();
#else
                WaitForSingleObject(vm.events.writingEnd, INFINITE);

#endif

                end = ReadTimeStampCounter();

                InterlockedAdd64((volatile LONG64 *) &vm.misc.totalTimeWaiting, (LONG64) (end - start));

                lockPTE(currentPTE);

                return REDO_FAULT;
            }
        }
    }


    frameNumber = getFrameNumber(page);

    // Zero the page if we are on a first fault, otherwise read from disk
    if (currentPTE->invalidFormat.diskIndex == EMPTY_PTE) {
        if (!zeroOnePage(page, threadInfo)) {
            DebugBreak();
        }
    } else {
        modified_read(currentPTE, frameNumber, threadInfo);
    }

    page->pte = currentPTE;

    InterlockedIncrement64(&vm.pfn.pagesConsumed);

    return frameNumber;
}


/**
 *@brief This function attempts to take a page from the standby list. If there is a page to take, this function
 *will update the page's pagetable entry to reflect that it is no longer on standby, but on disk
 * @param currentPTE The page table entry of the virtual address that is being faulted on
 * @param threadInfo The info of the calling thread
* @return Returns a page from a freelist
* @retval Returns null if the list is empty
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry
 */


/**
 * @brief This function pops a page off the standby list and updates its pte. Important note, it updates the page table entry
 * of the page without holding a page table lock. It can do this because the page table entry's corresponding page lock
 * protects it from being rescued at the same. However, this means that in the rescue or other functions relating to standby pages,
 * the page table entry could be changing.
 * @param threadInfo The thread info of the caller.
 * @return Return a page from the standby list. If the list is empty return NULL
 */
pfn *getVictimFromStandByList(PTHREAD_INFO threadInfo) {
    pfn *page;
    pte local;


    // Exits with page lock held
    page = RemoveFromHeadofPageList(&vm.lists.standby, threadInfo);

    if (page == LIST_IS_EMPTY) {
        return NULL;
    }


    // we can edit the pte without a lock because the page lock protects it
    // if another faulter tries to get this the pagelock will stop it in the rescue
    // if the rescue sees a change in the pte it will redo the fault
    // We have to write no fence here in order to avoid tearing


    local.entireFormat = 0;
    local.transitionFormat.isTransition = 0;
    local.invalidFormat.diskIndex = page->diskIndex;
#if DBG

#if DBG_DISK

        ULONG64 count = 0;

    for (int i = 0; i < vm.config.disk_size_in_pages; i++) {
        if (vm.disk.activeVa[i] == page->pte) {
            count++;
        }
    }
    ASSERT(count == 1);
#endif

    page->hasBeenRescuedWhileWritten = 0;
    page->diskIndex = 0;

#endif

    writePTE(page->pte, local);

    leavePageLock(page, threadInfo);

    // I will now prune if no one else is pruning
    checkIfPrune(threadInfo);

    InterlockedIncrement64(&vm.misc.pagesFromStandBy);


    return page;
}

/**
 * @brief This function reads in the contents of a disk page onto a physical page. It uses an interim transfer virtual address to complete this task.
 * Only after the entire transfer space is used, will this function unmap the pages from the transfer virtual address. By having limited unmap calls,
 * a lot of time spent flushing and unmapping is saved.
 * @param currentPTE The page table entry that contains the disk page we want to read from.
 * @param frameNumber The frame to read into.
 * @param threadContext The caller's thread info. It is used to determine which transfer virtual page to use
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry
 */
VOID
modified_read(pte *currentPTE, ULONG64 frameNumber, PTHREAD_INFO threadContext) {
    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) vm.disk.start + diskIndex * PAGE_SIZE;


    PVOID transferVaLocation = getThreadMapping(threadContext);


    if (MapUserPhysicalPages(transferVaLocation, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaLocation, frameNumber);
        return;
    }

    memcpy(transferVaLocation, (PVOID) diskAddress, PAGE_SIZE);

    freeThreadMapping(threadContext);

    set_disk_space_free(diskIndex);

#if DBG
    pfn *page = getPFNfromFrameNumber(frameNumber);

#if DBG_DISK

    if (currentPTE != NULL) {
        for (int i = 0; i < vm.config.disk_size_in_pages; i++) {
            if (vm.disk.activeVa[i] == currentPTE ) {
                DebugBreak();
            }
        }
    }

#endif
    page->diskIndex = 0;
#endif
}

/**
*  @brief This function zeroes a physical page. It uses an interim transfer virtual address to complete this task.
 * Only after the entire transfer space is used, will this function unmap the pages from the transfer virtual address. By having limited unmap calls,
 * a lot of time spent flushing and unmapping is saved.
 * @param page The page to zero.
 * @param threadContext The caller's thread info. It is used to determine which transfer virtual page to use.

 */

BOOL zeroOnePage(pfn *page, PTHREAD_INFO threadContext) {
    PVOID zeroVA;
    ULONG64 frameNumber;


    zeroVA = getThreadMapping(threadContext);

    frameNumber = getFrameNumber(page);

    if (MapUserPhysicalPages(zeroVA, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", zeroVA, frameNumber);
        return FALSE;
    }
    memset(zeroVA, 0, PAGE_SIZE);


    freeThreadMapping(threadContext);

    return TRUE;
}

/**
 * @brief This function gets a page off of one of the free lists. It returns with the page locked.
 * @param threadContext The thread info of the caller.
 * @return Returns a free page.
 * @post The caller must release the page lock of the returned page.
 */


pfn *getPageFromFreeList(PTHREAD_INFO threadContext) {
    ULONG64 localFreeListIndex;
    PLIST_ENTRY entry;
    pfn *page;
    ULONG64 localTotalFreeListLength;
    boolean pruneInProgress = FALSE;
    boolean gotFreeListLock = FALSE;
    ULONG64 counter;
    ULONG64 i;


    counter = 0;
    i = 0;

    // randomly accesses a freelist first
    localFreeListIndex = GetNextRandom(&threadContext->rng) % vm.config.number_of_free_lists;


    // Iterate through the freelists starting at the index calculated above and try to lock it

    while (TRUE) {
        // In our first run through, just try to enter a freelist lock
        if (counter < vm.config.number_of_free_lists) {
            if (tryEnterPageLock(&vm.lists.free.heads[localFreeListIndex].page, threadContext) == TRUE) {
                gotFreeListLock = TRUE;
            }
            // if all of them were empty at the time we checked
        } else if (counter == 2 * vm.config.number_of_free_lists) {
            return LIST_IS_EMPTY;
            // acquire a freelist lock exclusive
        } else {
            enterPageLock(&vm.lists.free.heads[localFreeListIndex].page, threadContext);
            gotFreeListLock = TRUE;
        }


        if (gotFreeListLock) {
#if DBG
            validateList(&vm.lists.free.heads[localFreeListIndex]);
#endif

            acquire_srw_exclusive(&threadContext->localList.sharedLock, threadContext);
            // Now that we have the head lock, we might as well add pages to our local list
            for (; i < BATCH_SIZE; ++i) {
                // now we have a locked page
                entry = RemoveHeadList(&vm.lists.free.heads[localFreeListIndex]);
                if (entry == LIST_IS_EMPTY) {
                    page = LIST_IS_EMPTY;
                    break;
                } else {
                    localTotalFreeListLength = InterlockedDecrement64((volatile LONG64 *) &vm.lists.free.length);
                    page = container_of(entry, pfn, entry);
                    InsertHeadList(&threadContext->localList, &page->entry);
                }
            }
            release_srw_exclusive(&threadContext->localList.sharedLock);

#if DBG
            validateList(&vm.lists.free.heads[localFreeListIndex]);
#endif

            // now that pages are off the list we no longer need to lock the list
            leavePageLock(&vm.lists.free.heads[localFreeListIndex].page, threadContext);

            if (i == 0) {
                gotFreeListLock = FALSE;
                counter++;
                localFreeListIndex = (localFreeListIndex + 1) % vm.config.number_of_free_lists;
                continue;
            } else {
                // The following code calls the standby pruner.

                // despite warnings, localFreeListLength Must Be Initialized
                ASSERT(localTotalFreeListLength != MAXULONG64);
                // check to see if we have enough pages on the free list
                if (localTotalFreeListLength <= vm.config.stand_by_trim_threshold) {
                    checkIfPrune(threadContext);
                }
                // return the page at the front of the local list
                InterlockedDecrement64(&vm.misc.pagesFromLocalCache);
                InterlockedIncrement64(&vm.misc.pagesFromFree);
                return getPageFromLocalList(threadContext);
            }
        }

        // go around in a circle
        localFreeListIndex = (localFreeListIndex + 1) % vm.config.number_of_free_lists;
        counter++;
    }
}

/**
 * @brief This function returns a free page on the users local list. Since no other thread has access to this lead, there is no need to deal with synchronization
 * @param threadContext The thread info that contains the local list head
 * @return The page at the head of the local list
 */
pfn *getPageFromLocalList(PTHREAD_INFO threadContext) {
    pListHead head;
    pfn *page;
    PLIST_ENTRY entry;


    head = &threadContext->localList;

    // I do this in a lock because the trimmer could be accessing this
    acquire_srw_exclusive(&head->sharedLock, threadContext);

    if (head->length == 0) {
        release_srw_exclusive(&head->sharedLock);
        return LIST_IS_EMPTY;
    }
    entry = RemoveHeadList(head);
    page = container_of(entry, pfn, entry);
    release_srw_exclusive(&head->sharedLock);

    InterlockedIncrement64(&vm.misc.pagesFromLocalCache);

    return page;
}
