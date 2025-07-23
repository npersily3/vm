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


DWORD diskWriter (LPVOID threadContext) {


    ULONG64 localBatchSize;


    ULONG64 diskAddressArray[BATCH_SIZE];
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

        if (getAllPagesAndDiskIndices(&localBatchSize, pfnArray, diskAddressArray, frameNumberArray) == FALSE) {
            SetEvent(writingEndEvent);
            continue;
        }

        writeToDisk(localBatchSize, frameNumberArray, diskAddressArray);

        addToStandBy(localBatchSize, pfnArray);

        // this needs to be in a lock to avoid
        // the race condition where it is reset right after it is
        EnterCriticalSection(lockStandByList);
        SetEvent(writingEndEvent);
        LeaveCriticalSection(lockStandByList);
    }

}
VOID addToStandBy(ULONG64 localBatchSize, pfn** pfnArray) {

    pfn* page;
    PCRITICAL_SECTION writingPageTableLock;
    PTE_REGION* region;

    for (int i = 0; i < localBatchSize; ++i) {

        page = pfnArray[i];


        region = getPTERegion(page->pte);
        writingPageTableLock = &region->lock;
        EnterCriticalSection(writingPageTableLock);

        //if it has been rescued, free up the disk space and do not put it on the standby list
        if(page->isBeingWritten == FALSE) {
            set_disk_space_free(page->diskIndex);
        } else {
            //check if disk page is empty
            //checkIfPageIsZero(diskAddressArray[i]);

            page->isBeingWritten = FALSE;
            EnterCriticalSection(lockStandByList);
            InsertTailList(&headStandByList, &page->entry);
            LeaveCriticalSection(lockStandByList);

        }
        // need to hold pte lock you could optimize to reduce enters and leave by grouping the locks, you need, but that is for later
        //make sure pte then list lock
        LeaveCriticalSection(writingPageTableLock);
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


BOOL getAllPagesAndDiskIndices (PULONG64 localBatchSizePointer, pfn** pfnArray, PULONG64 diskAddressArray, PULONG64 frameNumberArray) {

    ULONG64 i;
    ULONG64 counter;
    pfn* page;
    ULONG64 localBatchSize;
    PCRITICAL_SECTION writingPageTableLock;
    PTE_REGION* region;

    ULONG64 frameNumber;
    BOOL doubleBreak;


    localBatchSize = *localBatchSizePointer;

    counter = 0;
    i = 0;
    doubleBreak = FALSE;

    for (; i < localBatchSize; i++) {

        // if page is valid, the mod list lock is still
        page = getPageFromModifiedList();

        if (page == NULL) {
            if (i == 0) {
                doubleBreak = TRUE;
            }
            localBatchSize = i;
            break;
        }

        region = getPTERegion(page->pte);
        writingPageTableLock = &region->lock;

        // if the pte is being worked on, release your locks and back up
        if (TryEnterCriticalSection(writingPageTableLock) == FALSE) {

            ASSERT(counter < MB(1))
            i--;
            counter++;
            ///LeaveCriticalSection(lockModifiedList);
            releaseLock(&lockModList);
            continue;
        }
        counter = 0;

        // now that you have access to both the pte and modified list lock you can finally completely remove it from
        // the list and then release the modified list lock

        removeFromMiddleOfList(&headModifiedList, &page->entry);

        //LeaveCriticalSection(lockModifiedList);
        releaseLock(&lockModList);

        pfnArray[i] = page;

        frameNumber = getFrameNumber(page);
        frameNumberArray[i] = frameNumber;

        if (getDiskSlotAndUpdatePage(page, diskAddressArray, i) == FALSE) {
            localBatchSize = i;

            acquireLock(&lockModList);
            InsertTailList(&headModifiedList, &page->entry);
            releaseLock(&lockModList);

            if (i == 0) {
                doubleBreak = TRUE;
            }
            LeaveCriticalSection(writingPageTableLock);

            break;
        }
        LeaveCriticalSection(writingPageTableLock);
    }
    if (doubleBreak == TRUE) {
        return FALSE;
    }


    *localBatchSizePointer = localBatchSize;
    return TRUE;
}


BOOL getDiskSlotAndUpdatePage (pfn* page, PULONG64 diskAddressArray, ULONG64 index) {
    ULONG64 diskIndex;
    ULONG64 diskByteAddress;


    diskIndex = get_free_disk_index();


    if (diskIndex == COULD_NOT_FIND_SLOT) {
        return FALSE;
    }
    //
    // if (diskActiveVa[diskIndex] == NULL || ((ULONG64)diskActiveVa[diskIndex] & 0x1)) {
    //     diskActiveVa[diskIndex] = va;
    // } else {
    //     DebugBreak();
    // }


    // modified writing
    diskByteAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;
    diskAddressArray[index] = diskByteAddress;



    page->isBeingWritten = TRUE;
    page->diskIndex = diskIndex;
    page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;

    return TRUE;
}


pfn* getPageFromModifiedList(VOID) {

    pfn* page;
    PLIST_ENTRY newPageEntry;
    //  EnterCriticalSection(lockModifiedList);
    acquireLock(&lockModList);
    newPageEntry = headModifiedList.entry.Flink;
    // similar to mapPage, tryEnter page table, if I can then procceed, otherwise i-- and continue, release locks before hand
    ASSERT(newPageEntry != &headActiveList.entry);

    if (newPageEntry == &headModifiedList.entry) {

        releaseLock(&lockModList);
        return NULL;
    }

    page = container_of(newPageEntry, pfn, entry);

    return page;
}