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

    HANDLE* events[2];
    DWORD returnEvent;
    events[0] = writingStartEvent;
    events[1] = systemShutdownEvent;

    while (TRUE) {



        returnEvent = WaitForMultipleObjects(2,events, FALSE, INFINITE);

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

        localBatchSize = BATCH_SIZE;

        // pre acquire all the disk slots
        localBatchSize = getMultipleDiskIndices(diskIndexArray);

        // the disk was empty upon entry
        if (localBatchSize == COULD_NOT_FIND_SLOT) {
            SetEvent(writingEndEvent);
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
            SetEvent(writingEndEvent);
            continue;
        }

        // simply converts indices to addresses
        getDiskAddressesFromDiskIndices(diskIndexArray, diskAddressArray, localBatchSize);

        // actual write to disk
        writeToDisk(localBatchSize, frameNumberArray, diskAddressArray);

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
        acquire_srw_exclusive(&headStandByList.sharedLock, (PTHREAD_INFO) threadContext);
        SetEvent(writingEndEvent);
        release_srw_exclusive(&headStandByList.sharedLock);
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
            addPageToTail(&headStandByList, page, info);

        }

        leavePageLock(page, info);
    }
}

/**
 * @brief This function maps physical pages to an interim va in order to read the contents into the disk at specified indices.
  * @param localBatchSize The number of diskSlots and frames passed
 * @param frameNumberArray An array of frame numbers
 * @param diskAddressArray An array of diskAddresses
 */
// TODO make it so that is no longer unmaps everytime similar to modified read
VOID writeToDisk(ULONG64 localBatchSize, PULONG64 frameNumberArray, PULONG64 diskAddressArray) {
    if (MapUserPhysicalPages(transferVaWriting, localBatchSize, frameNumberArray) == FALSE) {

        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaWriting, frameNumberArray[0]);
        return ;
    }

    for (int i = 0; i < localBatchSize; ++i) {
        memcpy((PVOID)diskAddressArray[i], (PVOID) ((ULONG64) transferVaWriting + i * PAGE_SIZE) , PAGE_SIZE);
    }

    if (MapUserPhysicalPages(transferVaWriting, localBatchSize, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
        return ;
    }


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
        page = RemoveFromHeadofPageList(&headModifiedList, threadContext);


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
        addresses[i] = indices[i] * PAGE_SIZE + (ULONG64) diskStart;
    }
}


