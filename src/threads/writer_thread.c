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


DWORD diskWriter (LPVOID threadContext) {


    ULONG64 localBatchSize;
    ULONG64 previousBatchSize;

    ULONG64 diskAddressArray[BATCH_SIZE];
    ULONG64 diskIndexArray[BATCH_SIZE];

    ULONG64 frameNumberArray[BATCH_SIZE];
    pfn* pfnArray[BATCH_SIZE];

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

        localBatchSize = getMultipleDiskIndices(diskIndexArray);

        if (localBatchSize == COULD_NOT_FIND_SLOT) {
            SetEvent(writingEndEvent);
            continue;
        }

        previousBatchSize = localBatchSize;

        getPagesFromModifiedList(&localBatchSize, pfnArray, diskIndexArray, frameNumberArray, (PTHREAD_INFO) threadContext);



        if (previousBatchSize != localBatchSize) {
            freeUnusedDiskSlots(diskIndexArray, localBatchSize, previousBatchSize);
        }
        if (localBatchSize == 0) {
            SetEvent(writingEndEvent);
            continue;
        }

        getDiskAddressesFromDiskIndices(diskIndexArray, diskAddressArray, localBatchSize);


        writeToDisk(localBatchSize, frameNumberArray, diskAddressArray);

        addToStandBy(localBatchSize, pfnArray, (PTHREAD_INFO) threadContext);

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
        acquire_srw_exclusive(&headStandByList.sharedLock, (PTHREAD_INFO) threadContext);
        SetEvent(writingEndEvent);
        release_srw_exclusive(&headStandByList.sharedLock);
    }

}

VOID freeUnusedDiskSlots(PULONG64 diskIndexArray, ULONG64 start, ULONG64 end) {
    for (;start < end; start++) {
        set_disk_space_free(diskIndexArray[start]);
    }
}

VOID addToStandBy(ULONG64 localBatchSize, pfn** pfnArray, PTHREAD_INFO info) {

    pfn* page;

    //nptodo make it so the page lock protects this
    for (int i = 0; i < localBatchSize; ++i) {

        page = pfnArray[i];


        enterPageLock(page, info);

        //if it has been rescued, free up the disk space and do not put it on the standby list because the faulter
        // will put it on the active list
        if(page->isBeingWritten == FALSE) {
            set_disk_space_free(page->diskIndex);

            // if a pte/page pair had been freed the user thread does not want to wait for the writer thread
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


VOID writeToDisk(ULONG64 localBatchSize, PULONG64 frameNumberArray, PULONG64 diskAddressArray) {
    if (MapUserPhysicalPages(transferVaWriting, localBatchSize, frameNumberArray) == FALSE) {

        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaWriting, frameNumberArray[0]);
        return ;
    }

    for (int i = 0; i < localBatchSize; ++i) {
        // copy from transfer to disk

        memcpy((PVOID)diskAddressArray[i], (PVOID) ((ULONG64) transferVaWriting + i * PAGE_SIZE) , PAGE_SIZE);

        //     ASSERT(checkVa( (PULONG64)((ULONG64)transferVaWriting + i * PAGE_SIZE), vaArray[i]));
    }

    if (MapUserPhysicalPages(transferVaWriting, localBatchSize, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
        return ;
    }


}


VOID getPagesFromModifiedList (PULONG64 localBatchSizePointer, pfn** pfnArray, PULONG64 diskIndexArray, PULONG64 frameNumberArray, PTHREAD_INFO threadContext) {

    ULONG64 i;
    pfn* page;
    ULONG64 frameNumber;
    BOOL doubleBreak;


    i = 0;
    doubleBreak = FALSE;

    for (; i < *localBatchSizePointer; i++) {


        // this function obtains the pagelock for us
        page = RemoveFromHeadofPageList(&headModifiedList, threadContext);


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
VOID updatePage (pfn* page, ULONG64 diskIndex) {

    page->isBeingWritten = TRUE;
    page->diskIndex = diskIndex;
    page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;

}

VOID getDiskAddressesFromDiskIndices(PULONG64 indices, PULONG64 addresses, ULONG64 size) {
    for (int i = 0; i < size; i++) {
        addresses[i] = indices[i] * PAGE_SIZE + (ULONG64) diskStart;
    }
}


