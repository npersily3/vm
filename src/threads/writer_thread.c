//
// Created by nrper on 7/16/2025.
//


#include "../../include/threads/writer_thread.h"
#include "../../include/variables/globals.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/utils/page_utils.h"
#include "../../include/disk/disk.h"
#include "../../include/utils/thread_utils.h"
#include "threads/user_thread.h"
/**
 *@file writer_thread.c
 *@brief This file contains all the functions associated with writing to page that have been recently trimmed to disk.
 *@author Noah Persily
*/




/**
 *@brief A function that gets the current transfer virtual address that a thread needs.
 *@param threadContext The thread info of the caller. It is used to update the per thread index into its transfer virtual address space.
 *@return A pointer to a page of unmapped transfer virtual address space.
*/

PVOID getWriterThreadMapping(PTHREAD_INFO threadContext) {
    // Get the current thread's transfer va space
    PVOID currentTransferVa = vm.va.writing;
    // Index into it
    PVOID va = (PVOID) ((ULONG64) currentTransferVa + BATCH_SIZE * PAGE_SIZE  * threadContext->TransferVaIndex);

    threadContext->TransferVaIndex += 1;
    ASSERT(threadContext->TransferVaIndex <= vm.config.size_of_user_thread_transfer_va_space_in_pages);

    return va;
}

/**
 * @brief Unmaps the thread's transfer virtual address space if it has all mapped. Batching it in this way saves a lot of time spent mapping and flushing.
 * @param threadContext The thread info of the caller. It is used to index into the correct transfer virtual address space.
 */
VOID freeWriterThreadMapping(PTHREAD_INFO threadContext) {

    // If we are at the end, reset and unmap everything
    if (threadContext->TransferVaIndex == NUM_WRITING_BATCHES) {
        threadContext->TransferVaIndex = 0;

        if (MapUserPhysicalPages(vm.va.writing, BATCH_SIZE * NUM_WRITING_BATCHES, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n", vm.va.userThreadTransfer[threadContext->ThreadNumber]);
            return;
        }
    }
}


/**
 * @brief This function takes pages off the modified list and tries to write them to disk. It batch writes pages to disk to be more effiecient.
 * @param info A pointer to a thread info struct. Passed in during the function CreateThread
 * @retval 0 If the program succeeds
 */
DWORD diskWriter (LPVOID info) {


    ULONG64 localBatchSize;
    ULONG64 previousBatchSize;

    ULONG64 diskAddressArray[BATCH_SIZE];
    ULONG64 diskIndexArray[BATCH_SIZE];

    ULONG64 frameNumberArray[BATCH_SIZE];
    pfn* pfnArray[BATCH_SIZE];

    PTHREAD_INFO threadContext = (PTHREAD_INFO)info;

    HANDLE events[2];
    DWORD returnEvent;
    events[0] = vm.events.writingStart;
    events[1] = vm.events.systemShutdown;

    while (TRUE) {



        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        vm.misc.numWrites ++;

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

        localBatchSize = BATCH_SIZE;

        // pre acquire all the disk slots
        localBatchSize = getMultipleDiskIndices(diskIndexArray);

        // the disk was empty upon entry
        if (localBatchSize == COULD_NOT_FIND_SLOT) {
            SetEvent(vm.events.writingEnd);
            continue;
        }

        // save how many disk indices we got
        previousBatchSize = localBatchSize;

        // fill our local page and frame number arrays
        getPagesFromModifiedList(&localBatchSize, pfnArray, diskIndexArray, frameNumberArray,  threadContext);



        // if the amount of pages we got off the modified list is less than the amount of disk slots we acquired free the disk slots
        if (previousBatchSize != localBatchSize) {
            freeUnusedDiskSlots(diskIndexArray, localBatchSize, previousBatchSize);
        }

        if (localBatchSize == 0) {
            SetEvent(vm.events.writingEnd);
            continue;
        }

        // simply converts indices to addresses
        getDiskAddressesFromDiskIndices(diskIndexArray, diskAddressArray, localBatchSize);

        // actual write to disk
        writeToDisk(localBatchSize, frameNumberArray, diskAddressArray, threadContext);

        addToStandBy(localBatchSize, pfnArray, threadContext);

// Usually it is fine to have stale data stored in these stack variables,
// but for debugging it is useful to reset the variables
#if DBG
for (int i = 0; i < BATCH_SIZE; ++i)
{
     diskAddressArray[i] = 0;
     diskIndexArray[i] = 0;
     frameNumberArray[i] = 0;
     pfnArray[i] = 0;

}

#endif


        // this needs to be in a sharedLock to avoid
        // the race condition where it is reset right after it is
        // TODO think about this
        acquire_srw_exclusive(&vm.lists.standby.sharedLock, (PTHREAD_INFO) threadContext);
        SetEvent(vm.events.writingEnd);
        release_srw_exclusive(&vm.lists.standby.sharedLock);
    }

}

/**
 * @brief This function frees a specified range of disk indices in a given array
 * @param diskIndexArray An array of disk indices
 * @param start Where to start freeing in the array of disk indices.
 * @param end Where to finish freeing in the array of disk indices.
 */
VOID freeUnusedDiskSlots(PULONG64 diskIndexArray, ULONG64 start, ULONG64 end) {
    for (;start < end; start++) {
        set_disk_space_free(diskIndexArray[start]);
    }
}

/**
 *
 *@brief Adds pages from an array onto the standby list.
 * @param localBatchSize The number of pages to add
 * @param pfnArray An array of page pointers
 * @param info The info about the caller
 */
VOID addToStandBy(ULONG64 localBatchSize, pfn** pfnArray, PTHREAD_INFO info) {

    pfn* page;


    for (int i = 0; i < localBatchSize; ++i) {

        page = pfnArray[i];


        enterPageLock(page, info);

        //if it has been rescued, free up the disk space and do not put it on the standby list because the faulter
        // will put it on the active list
        if(page->isBeingWritten == FALSE) {
            set_disk_space_free(page->diskIndex);
#if DBG
            page->diskIndex = 0;
#endif
            // if a pte/page pair had been freed, the user thread does not want to wait for the writer thread
            // to finish in order to release locks, so the faulter signals this status and tells us to put this page on
            // the freelist
            if (page->isBeingFreed == TRUE) {
                page->isBeingFreed = FALSE;
                addPageToFreeList(page, info);
            }
        } else {

            page->isBeingWritten = FALSE;

            // this expects me to come in with the lock and does not release it for me
            addPageToTail(&vm.lists.standby, page, info);

        }

        leavePageLock(page, info);
    }
}

/**
 * @brief This function maps physical pages to an interim va in order to read the contents into the disk at specified indices.
  * @param localBatchSize The number of diskSlots and frames passed
 * @param frameNumberArray An array of frame numbers
 * @param diskAddressArray An array of diskAddresses
 * @param ThreadContext The info of the caller
 */

VOID writeToDisk(ULONG64 localBatchSize, PULONG64 frameNumberArray, PULONG64 diskAddressArray, PTHREAD_INFO ThreadContext) {

    PVOID va;

    va = getWriterThreadMapping(ThreadContext);

    if (MapUserPhysicalPages(vm.va.writing, localBatchSize, frameNumberArray) == FALSE) {

        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", vm.va.writing, frameNumberArray[0]);
        return ;
    }

    for (int i = 0; i < localBatchSize; ++i) {
        memcpy((PVOID)diskAddressArray[i], (PVOID) ((ULONG64) vm.va.writing + i * PAGE_SIZE) , PAGE_SIZE);
    }

    freeWriterThreadMapping(ThreadContext);


}

/**
 * @brief This function pops pages off the modified list and updates their status as standby, in progress write, and gives them a disk index
 * @param localBatchSizePointer A pointer to our local batch size. If there are less pages on the modified list than disk indices,
 * the function will update the contents of the pointer to reflect that change.
 * @param pfnArray A location for this function to store the pages it pops of the modified list.
 * @param diskIndexArray An array of disk indices to update the pfns.
 * @param frameNumberArray An array of frame numbers to be filled in.
 * @param threadContext The thread info of the caller.
 */
VOID getPagesFromModifiedList (PULONG64 localBatchSizePointer, pfn** pfnArray, PULONG64 diskIndexArray, PULONG64 frameNumberArray, PTHREAD_INFO threadContext) {

    ULONG64 i;
    pfn* page;
    ULONG64 frameNumber;
    BOOL doubleBreak;


    i = 0;
    doubleBreak = FALSE;

    for (; i < *localBatchSizePointer; i++) {


        // this function obtains the page lock for us
        page = RemoveFromHeadofPageList(&vm.lists.modified, threadContext);


        // if we reach the end update the contents of the size pointer and break
        if (page == LIST_IS_EMPTY) {
            if (i == 0) {
                doubleBreak = TRUE;
            }
            *localBatchSizePointer = i;
            break;
        }
        frameNumber = getFrameNumber(page);

        pfnArray[i] = page;
        frameNumberArray[i] = frameNumber;

        updatePage(page, diskIndexArray[i]);

        leavePageLock(page, threadContext);
    }
    if (doubleBreak == TRUE) {
        return;
    }

    return;
}


// called with pagelock held
// no pte lock held, but that is ok, since the only time it could accessed while marked modified is in a rescue
// and I use pagelocks in the rescue to serialize threads
/**
 * @brief This function updates the page to hold a disk index and change its status to write in progress and standby
 * @param page The page to update
 * @param diskIndex The disk index to store in the page
* @pre The page  must be locked.
 * @post The caller must release the page lock.
 */
// TODO make it so the pte only has one transition bit and pfn has a standby or modifed bit
VOID updatePage (pfn* page, ULONG64 diskIndex) {

    page->isBeingWritten = TRUE;
    page->diskIndex = diskIndex;
    page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;

}

/**
 * @brief This function converts indices to addresses by multiplying by page size and adding the disk base
 * @param indices Array of disk indices that is used to get the disk addresses
 * @param addresses Array of addresses that will be filled in by this function
 * @param size The size of the arrays
 */
VOID getDiskAddressesFromDiskIndices(PULONG64 indices, PULONG64 addresses, ULONG64 size) {
    for (int i = 0; i < size; i++) {
        addresses[i] = indices[i] * PAGE_SIZE + (ULONG64) vm.disk.start;
    }
}


