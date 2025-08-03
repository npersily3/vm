//
// Created by nrper on 7/16/2025.
//

#include "../../include/threads/user_thread.h"
#include <stdio.h>
#include "../../include/variables/globals.h"
#include "../../include/variables/structures.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/utils/page_utils.h"
#include "../../include/disk/disk.h"
#include "../../include/utils/thread_utils.h"
#include "initialization/init.h"

#if spinEvents

/**
 * * @brief This function spins while waiting so waits show up as time on a performance trace
 */
VOID spinWhileWaiting(VOID) {

    DWORD status;

    while (TRUE) {
        status = WaitForSingleObject(writingEndEvent, 0);
        if (status != WAIT_TIMEOUT) {
            return;
        }
    }

}
#endif

/**
 * @brief Adds a page to any available freelist.
 *

 *
 * @param page The page you are inserting into the list
 * @param threadInfo The thread info of a caller. It is mainly used for debug functions
 *
* @pre The page  must be locked.
 * @post The caller must release the page lock.
 */

VOID addPageToFreeList(pfn* page, PTHREAD_INFO threadInfo) {
    ULONG64 localFreeListIndex;


    // Increment the global index which is a guess as to what free list most likely is available and has pages
    localFreeListIndex = InterlockedIncrement(&freeListAddIndex);
    localFreeListIndex %= NUMBER_OF_FREE_LISTS;
    InterlockedIncrement64((volatile LONG64 *)&freeListLength);

    // iterate through the freelists starting at the index calculated above and try to lock it
    while (TRUE) {
        // once you lock a free list insert it at the head
        if (TryEnterCriticalSection(&headFreeLists[localFreeListIndex].page.lock) == TRUE) {

#if DBG
            validateList(&headFreeLists[localFreeListIndex]);
#endif


            InsertHeadList(&headFreeLists[localFreeListIndex], &page->entry );

#if DBG
            validateList(&headFreeLists[localFreeListIndex]);
#endif

            leavePageLock(&headFreeLists[localFreeListIndex].page, threadInfo);

            return;

        }
        // increment and wrap
        localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
    }



}

/**
 * @brief Removes a batch of victims from the standby list and puts them on the free list.
 * @param threadInfo The thread info of a caller. It is mainly used for debug functions.
 */
VOID batchVictimsFromStandByList(PTHREAD_INFO threadInfo) {
    listHead localList;
    pfn* page;
    pfn* nextPage;


    init_list_head(&localList);


    // adds pages onto the local list from the standby list but does not update them
    // returns with all the pages locked
    if (removeBatchFromList(&headStandByList, &localList, threadInfo) == LIST_IS_EMPTY) {
        return;
    }

#if DBG
    validateList(&localList);
#endif


    page = container_of(localList.entry.Flink, pfn, entry);
    // For every page on the local list

    while (&page->entry != &localList.entry) {

        // We can do this without a pte lock because the page lock protects it from rescues
        page->pte->transitionFormat.contentsLocation = DISK;
        page->pte->invalidFormat.diskIndex = page->diskIndex;

        // add it to the list and manage locks
        nextPage = container_of(page->entry.Flink, pfn, entry);
        addPageToFreeList(page, threadInfo);
        leavePageLock(page, threadInfo);
        page = nextPage;

    }

}
/**
 *@brief A function that gets the current transfer virtual address that a thread needs.
 *@param threadContext The thread info of the caller. It is used to update the per thread index into its transfer virtual address space.
 *@return A pointer to a page of unmapped transfer virtual address space.
*/

PVOID getThreadMapping(PTHREAD_INFO threadContext) {
    // Get the current thread's transfer va space
    PVOID currentTransferVa = userThreadTransferVa[threadContext->ThreadNumber];
    // Index into it
    PVOID va = (PVOID) ((ULONG64) currentTransferVa + PAGE_SIZE  * threadContext->TransferVaIndex);

    threadContext->TransferVaIndex += 1;
    ASSERT(threadContext->TransferVaIndex <= SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES);

    return va;
}

/**
 * @brief Unmaps the thread's transfer virtual address space if it has all mapped. Batching it in this way saves a lot of time spent mapping and flushing.
 * @param threadContext The thread info of the caller. It is used to index into the correct transfer virtual address space.
 */
VOID freeThreadMapping(PTHREAD_INFO threadContext) {

    // If we are at the end, reset and unmap everything
    if (threadContext->TransferVaIndex == SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES) {
        threadContext->TransferVaIndex = 0;

        if (MapUserPhysicalPages(userThreadTransferVa[threadContext->ThreadNumber], SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n", userThreadTransferVa[threadContext->ThreadNumber]);
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
//TODO move mapping and adding to the active list to this function as opposed to being in two place
BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID threadContext) {



    BOOL returnValue;
    pte* currentPTE;
    pte pteContents;
    PCRITICAL_SECTION currentPageTableLock;
    PTE_REGION* region;

    currentPTE = va_to_pte((ULONG64) arbitrary_va);


    // Checks to see if the user is accessing out of bounds va space
    if (isVaValid((ULONG64) arbitrary_va) == FALSE) {
        DebugBreak();
        printf("invalid va %p\n", arbitrary_va);
    }

    returnValue = !REDO_FAULT;
    region = getPTERegion(currentPTE);
    currentPageTableLock = &region->lock;

    EnterCriticalSection(currentPageTableLock);
    pteContents = *currentPTE;

    // If the page fault was already handled by another thread
    if (pteContents.validFormat.valid != TRUE) {

        // If the pte is in flight
        if (isRescue(currentPTE)) {
            // Determine if the rescue told us to back up or not
            if (rescue_page((ULONG64) arbitrary_va, currentPTE, (PTHREAD_INFO) threadContext) == REDO_FAULT) {
                returnValue = REDO_FAULT;
            }
        } else {
            // If we are on a first fault or a disk read map that page.
            if (mapPage((ULONG64) arbitrary_va, currentPTE, threadContext, currentPageTableLock) == REDO_FAULT) {
                returnValue = REDO_FAULT;
            }
        }
    }


    LeaveCriticalSection(currentPageTableLock);


    return returnValue;

}

/**
 * @brief
 *
 * @param currentPTE The page table entry of the virtual address that was faulted on.
 * @return It returns true if the currentPTE is signaled as transition.
 *
 * @pre The page table entry must be locked.
 * @post The caller must release the lock of the page table entry.
 */
BOOL isRescue(pte* currentPTE) {
    return (currentPTE->transitionFormat.contentsLocation == MODIFIED_LIST) ||
            (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST);
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
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE, PTHREAD_INFO threadInfo) {
    pfn* page;
    ULONG64 frameNumber;

    pte entryContents;

    // We must read no fence because the pte could be getting cross edited by another faulter who is using it as a victim
    entryContents.entireFormat = ReadULong64NoFence((DWORD64 const volatile *)currentPTE);


    // The following two checks see if the case where a pte is cross edited in the process of victimization occured

    // If before we got our snapshot, the pte was changed to non rescue
    if (!isRescue(currentPTE)){

        return REDO_FAULT;
    }

    frameNumber = entryContents.transitionFormat.frameNumber;
    page = getPFNfromFrameNumber(frameNumber);

    // if our page that we believe our contents is on no longer maps to our copy
    if (page->pte->entireFormat != entryContents.entireFormat) {
        return REDO_FAULT;
    }



    enterPageLock(page, threadInfo);

    // Now that we are in the pagelock we can be sure the pte is not being edited without a lock in our victimization code.
    // Therefore, we can now check the actual pte for the same things as above as opposed to our copy

    if (page->pte != currentPTE) {
        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }

    if (!isRescue(currentPTE)) {

        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }
    ASSERT(currentPTE->transitionFormat.frameNumber == frameNumber);

    // If there is a write in progress the page is not on any list
    // We do not know when in the right this is occurring, so we set this variable to signal to the writer to not add this
    // page to the standby list
    // Instead, the writer will free its disk space
    if (page->isBeingWritten == TRUE) {
        page->isBeingWritten = FALSE;
    }
    // If its on standby, remove it and set the disk space free
    else if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        removeFromMiddleOfList(&headStandByList, page, threadInfo);

        set_disk_space_free(page->diskIndex);


    } else {
        // It must be on the modified list now if it was determined to be a rescue but no a write in progress or a standby page
        removeFromMiddleOfList(&headModifiedList,page, threadInfo);
    }
    // we can leave this lock because it is no longer on a list, so no other operations like a list removal but be poking at this page
    // A faulter that is accessing the same region will be stopped by a pagetable lock
    leavePageLock(page, threadInfo);

    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", (PVOID)arbitrary_va, frameNumber);
        return FALSE;
    }

    currentPTE->validFormat.valid = 1;
    currentPTE->validFormat.transition = UNASSIGNED;


    enterPageLock(page, threadInfo);
    addPageToTail(&headActiveList, page, threadInfo);
    leavePageLock(page, threadInfo);

    return !REDO_FAULT;

}


/**
 * @brief This function handles the first time a virtual address is accessed, and if a virtual address contents are on disk.
 * First this thread will attempt to map from the free list; then it will try to map from standby. If none of those succeed it will wake up the trimmer and wait to redo the fault.
 * If this thread notices that usable pages are getting low, it will wake up the trimmer.
 * @param arbitrary_va The virtual address being accessed.
 * @param currentPTE The page table entry of the virtual address being accessed.
 * @param threadContext The thread info of the faulting thread.
 * @param currentPageTableLock The lock corresponding to the page table entry
 * @return Returns true if we need to redo the fault, false otherwise.
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry.
 */
//TODO change it so that only the pte is passed in, and thread Context is of type PTHREAD_INFO, also pull out the decision to zero or modified read from the inner functions to this function
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    ULONG64 frameNumber;
    PTHREAD_INFO threadInfo;


    threadInfo = (PTHREAD_INFO)threadContext;


    frameNumber = NULL;
    page = NULL;

    // Attempt to get a page from the free list and standby list
    // NULL means the page could not be found
     mapPageFromFreeList(currentPTE, threadInfo, &page);

    ASSERT(false);
     if (page == NULL) {
         mapPageFromStandByList(currentPTE,  threadInfo, page);

         if (page == NULL) {

            // If we are not able to get a page, start the trimmer and sleep

            LeaveCriticalSection(currentPageTableLock);

            //TODO find a way to resolve the case where it resets writing end event and there is a deadlock

            ResetEvent(writingEndEvent);
            SetEvent(trimmingStartEvent);


            pageWaits++;

            ULONG64 start;
            ULONG64 end;

            start = ReadTimeStampCounter();
#if spinEvents
            spinWhileWaiting();
#else
            WaitForSingleObject(writingEndEvent, INFINITE);

#endif

           end = ReadTimeStampCounter();

            InterlockedAdd64((volatile LONG64 *) &totalTimeWaiting, (LONG64)(end - start));

            EnterCriticalSection(currentPageTableLock);

            return REDO_FAULT;
        }
    }





    // if we seem to be running low on free and standby pages wake the trimmer
    if (((double) (headStandByList.length + ReadULong64NoFence(&freeListLength)) / ((double) NUMBER_OF_PHYSICAL_PAGES )) < .5) {
        SetEvent(trimmingStartEvent);
    }



    //validate the pte and add it to the list
    currentPTE->validFormat.frameNumber = frameNumber;
    currentPTE->validFormat.valid = 1;

    page = getPFNfromFrameNumber(frameNumber);
    page->pte = currentPTE;

    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %llu to page %llX\n", arbitrary_va, frameNumber);
        return FALSE;
    }

    enterPageLock(page, threadInfo);
    addPageToTail(&headActiveList, page, threadInfo);
    leavePageLock(page, threadContext);

    return !REDO_FAULT;
}

/**
 * @brief This function tries to get a physical page of a free list. It will also detect if the number of free pages
 * is running low, and take some from the standby list.
 * @param currentPTE The page table entry of the virtual address that is being faulted on
 * @param threadInfo The info about the calling thread.
 * @param page A place to store the page we are about to map.
* @return Returns true if we need to redo the fault, false otherwise.
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry.
 */

VOID mapPageFromFreeList (pte* currentPTE, PTHREAD_INFO threadInfo, pfn* page) {



    ULONG64 frameNumber;

    // This function acquires the page lock
    page = getPageFromFreeList(threadInfo);


    if (page == LIST_IS_EMPTY) {
        return;
    }


    leavePageLock(page, threadInfo);

    frameNumber = getFrameNumber(page);



    // Zero the page if we are on a first fault, otherwise read from disk
    if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
        modified_read(currentPTE, frameNumber, threadInfo);
    } else {
        zeroOnePage(page, threadInfo);
    }


    return;

}

/**
 *@brief This function attempts to take a page from the standby list. If there is a page to take, this function
 *will update the page's pagetable entry to reflect that it is no longer on standby, but on disk
 * @param currentPTE The page table entry of the virtual address that is being faulted on
 * @param threadInfo The info of the calling thread
 * @param page A place to store the page that is about to be mapped to the virtual address
 * @return Returns true if we need to redo the fault, false otherwise.
 * @pre The page table entry must be locked on entry to this function.
 * @post The caller must unlock the page table entry
 */

VOID mapPageFromStandByList (pte*  currentPTE, PTHREAD_INFO threadInfo, pfn* page) {


    ULONG64 frameNumber;
    pte entryContents;

    entryContents = *currentPTE;



    // All locks pertaining to this page are dealt with in this function. We do not need to lock or unlock it
    page = getVictimFromStandByList(threadInfo);

    if (page == NULL) {
        return;
    }

    frameNumber = getFrameNumber(page);



    // Zero the page if we are on a first fault, otherwise read from disk
    if (entryContents.invalidFormat.diskIndex == EMPTY_PTE) {
        if (!zeroOnePage(page, threadInfo)) {
            DebugBreak();
        }
    } else {
        modified_read(currentPTE, frameNumber, threadInfo);
    }

    return;
}

/**
 * @brief This function pops a page off the standby list and updates its pte. Important note, it updates the page table entry
 * of the page without holding a page table lock. It can do this because the page table entry's corresponding page lock
 * protects it from being rescued at the same. However, this means that in the rescue or other functions relating to standby pages,
 * the page table entry could be changing.
 * @param threadInfo The thread info of the caller.
 * @return Return a page from the standby list. If the list is empty return NULL
 */
pfn* getVictimFromStandByList (PTHREAD_INFO threadInfo) {

    pfn* page;
    pte local;


    // Exits with page lock held
    page = RemoveFromHeadofPageList(&headStandByList, threadInfo);

    if (page == LIST_IS_EMPTY) {
        return NULL;
    }


    // we can edit the pte without a lock because the page lock protects it
    // if another faulter tries to get this the pagelock will stop it in the rescue
    // if the rescue sees a change in the pte it will redo the fault
    // We have to write no fence here in order to avoid tearing

    local.transitionFormat.contentsLocation = DISK;
    local.invalidFormat.diskIndex = page->diskIndex;

    WriteULong64NoFence(page->pte,  local.entireFormat);

    leavePageLock(page, threadInfo);


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
modified_read(pte* currentPTE, ULONG64 frameNumber, PTHREAD_INFO threadContext) {

    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;


    PVOID transferVaLocation = getThreadMapping(threadContext);


    if (MapUserPhysicalPages(transferVaLocation, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaLocation, frameNumber);
        return;
    }

    memcpy(transferVaLocation, (PVOID) diskAddress, PAGE_SIZE);

    freeThreadMapping(threadContext);

    set_disk_space_free(diskIndex);
}

/**
*  @brief This function zeroes a physical page. It uses an interim transfer virtual address to complete this task.
 * Only after the entire transfer space is used, will this function unmap the pages from the transfer virtual address. By having limited unmap calls,
 * a lot of time spent flushing and unmapping is saved.
 * @param page The page to zero.
 * @param threadContext The caller's thread info. It is used to determine which transfer virtual page to use.

 */
// TODO make it only take in a frame number and make it void
BOOL zeroOnePage (pfn* page, PTHREAD_INFO threadContext) {

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
pfn* getPageFromFreeList(PTHREAD_INFO threadContext) {
    ULONG64 localFreeListIndex;
    PLIST_ENTRY entry;
    pfn* page;
    ULONG64 localTotalFreeListLength;
    boolean pruneInProgress = FALSE;
    boolean gotFreeListLock = FALSE;
    ULONG64 counter;



    counter = 0;

    // Increment global data that approximates what free list should be the most full/least contended
    localFreeListIndex = InterlockedIncrement(&freeListRemoveIndex) % NUMBER_OF_FREE_LISTS;


    // Iterate through the freelists starting at the index calculated above and try to lock it

    while (TRUE) {
        // In our first run through, just try to enter a freelist lock
        if (counter < NUMBER_OF_FREE_LISTS) {
            if (TryEnterCriticalSection(&headFreeLists[localFreeListIndex].page.lock) == TRUE) {
                gotFreeListLock = TRUE;
            }
            // if all of them were empty at the time we checked
        } else if (counter == 2* NUMBER_OF_FREE_LISTS) {
            return LIST_IS_EMPTY;
            // acquire a freelist lock exclusive
        } else {
            EnterCriticalSection(&headFreeLists[localFreeListIndex].page.lock);
            gotFreeListLock = TRUE;
        }


        if (gotFreeListLock) {
#if DBG
        validateList(&headFreeLists[localFreeListIndex]);
#endif
            // now we have a locked page
            entry = RemoveHeadList(&headFreeLists[localFreeListIndex]);

            if (entry == LIST_IS_EMPTY) {
                page = LIST_IS_EMPTY;
            } else {
                page = container_of(entry, pfn, entry);
            }
#if DBG
            validateList(&headFreeLists[localFreeListIndex]);
#endif

            // now that the page is off the list we no longer need to lock the list
            LeaveCriticalSection(&headFreeLists[localFreeListIndex].page.lock);

            if (page == LIST_IS_EMPTY) {
                gotFreeListLock = FALSE;
                counter++;
                localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
                continue;
            } else {

                // The following code calls the standby pruner.
                localTotalFreeListLength = InterlockedDecrement64((volatile LONG64 *) &freeListLength);

               ASSERT(localTotalFreeListLength != MAXULONG64);
                // check to see if we have enough pages on the free list
                if (localTotalFreeListLength <= STAND_BY_TRIM_THRESHOLD) {
                    // if there is not a pruning mission already happening
                    pruneInProgress = InterlockedCompareExchange((volatile LONG *)&standByPruningInProgress, TRUE, FALSE);

                    if (pruneInProgress == FALSE) {
                        batchVictimsFromStandByList(threadContext);
                    }

                    InterlockedExchange(&standByPruningInProgress, FALSE);
                }
                // return a locked page
                enterPageLock(page, threadContext);
                return page;
            }
        }

        // go around in a circle
        localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
        counter++;
    }
}