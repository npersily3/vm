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


VOID debugUserTransferVA (PVOID va, ULONG64 number_pages) {

    for (int i = 0; i < number_pages; i++) {
        PULONG p = (PULONG)((ULONG64) va + i * PAGE_SIZE);

        *p = 1;
    }
}

VOID batchVictimsFromStandByList(PTHREAD_INFO threadInfo) {
    listHead localList;
    pfn* page;
    ULONG64 freeListIndex;

    init_list_head(&localList);


    removeBatchFromList(&headStandByList, &localList, threadInfo);

    while (localList.entry.Flink != &localList.entry) {
        page = container_of(localList.entry.Flink, pfn, entry);

        page->pte->transitionFormat.contentsLocation = DISK;
        page->pte->invalidFormat.diskIndex = page->diskIndex;

        freeListIndex = InterlockedIncrement(&freeListAddIndex);
        freeListIndex %= NUMBER_OF_FREE_LISTS;
        addPageToTail(&headFreeLists[freeListIndex], page, threadInfo);
        InterlockedIncrement(&freeListLength);

        leavePageLock(page, threadInfo);
    }

}


PVOID getThreadMapping(PTHREAD_INFO threadContext) {
    PVOID currentTransferVa = userThreadTransferVa[threadContext->ThreadNumber];
    PVOID va = (PVOID) ((ULONG64) currentTransferVa + PAGE_SIZE  * threadContext->TransferVaIndex);
#if DBG
    debugUserTransferVA(currentTransferVa, threadContext->TransferVaIndex);

#endif


#if DBG
    debugUserTransferVA(currentTransferVa, threadContext->TransferVaIndex + 1);

#endif

    threadContext->TransferVaIndex += 1;
    ASSERT(threadContext->TransferVaIndex <= SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES);

    return va;
}
VOID freeThreadMapping(PTHREAD_INFO threadContext) {

    if (threadContext->TransferVaIndex == SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES) {
        threadContext->TransferVaIndex = 0;

        if (MapUserPhysicalPages(userThreadTransferVa[threadContext->ThreadNumber], SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n", userThreadTransferVa[threadContext->ThreadNumber]);
            return;
        }
    }
}

//nptodo remove 2nd transition bit, and check the pfn diskIndex to see what list it is on, then pass into rescue what list it is
//then you will need to reset diskIndex after you map it
BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID lpParam) {



    BOOL returnValue;
    pte* currentPTE;
    pte pteContents;
    PCRITICAL_SECTION currentPageTableLock;
    PTE_REGION* region;

    currentPTE = va_to_pte((ULONG64) arbitrary_va);


    if (isVaValid((ULONG64) arbitrary_va) == FALSE) {
        DebugBreak();
        printf("invalid va %p\n", arbitrary_va);
    }

    returnValue = !REDO_FAULT;
    region = getPTERegion(currentPTE);
    currentPageTableLock = &region->lock;

    EnterCriticalSection(currentPageTableLock);
    pteContents = *currentPTE;

    //if the page fault was already handled by another thread
    if (pteContents.validFormat.valid != TRUE) {

        if (isRescue(currentPTE)) {

            if (rescue_page((ULONG64) arbitrary_va, currentPTE, (PTHREAD_INFO) lpParam) == REDO_FAULT) {
                returnValue = REDO_FAULT;
            }
            } else {

                if (mapPage((ULONG64) arbitrary_va, currentPTE, lpParam, currentPageTableLock) == REDO_FAULT) {
                    returnValue = REDO_FAULT;
                }
            }
    }

    if (returnValue == !REDO_FAULT) {
        *arbitrary_va = (ULONG_PTR) arbitrary_va;
    }

    LeaveCriticalSection(currentPageTableLock);


    return returnValue;

}
BOOL isRescue(pte* currentPTE) {
    return (currentPTE->transitionFormat.contentsLocation == MODIFIED_LIST) ||
            (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST);
}


// this function deals with the case where a pages content is still on a physical page as opposed to a completely new
//page or a disk read
// this function determines where the page is and rescues it after it has been trimmed, written, or in between
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE, PTHREAD_INFO threadInfo) {
    pfn* page;
    ULONG64 frameNumber;

    pte entryContents;
    entryContents.entireFormat = ReadULong64NoFence((ULONG64)currentPTE);



    if ((entryContents.transitionFormat.contentsLocation != MODIFIED_LIST)  &&
        (entryContents.transitionFormat.contentsLocation != STAND_BY_LIST )) {

        return REDO_FAULT;
    }

    frameNumber = entryContents.transitionFormat.frameNumber;
    page = getPFNfromFrameNumber(frameNumber);

    if (page->pte->entireFormat != entryContents.entireFormat) {
        return REDO_FAULT;
    }


    enterPageLock(page, threadInfo);

    if (page->pte != currentPTE) {
        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }

    if ((currentPTE->transitionFormat.contentsLocation != MODIFIED_LIST)  &&
        (currentPTE->transitionFormat.contentsLocation != STAND_BY_LIST )) {

        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }
    ASSERT(currentPTE->transitionFormat.frameNumber == frameNumber);
    //if there is a write in progress
    if (page->isBeingWritten == TRUE) {
        page->isBeingWritten = FALSE;
    }
    else if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        removeFromMiddleOfList(&headStandByList, page, threadInfo);

        set_disk_space_free(page->diskIndex);

    } else {

        // EnterCriticalSection(lockModifiedList);

        removeFromMiddleOfList(&headModifiedList,page, threadInfo);

        //  LeaveCriticalSection(lockModifiedList);
    }
    leavePageLock(page, threadInfo);

    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", (PVOID)arbitrary_va, frameNumber);
        return FALSE;
    }

    currentPTE->validFormat.valid = 1;
    currentPTE->validFormat.transition = UNASSIGNED;


    //    ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));

    // AcquireSRWLockExclusive(&headActiveList.sharedLock);
    // InsertTailList(&headActiveList, &page->entry);
    // ReleaseSRWLockExclusive(&headActiveList.sharedLock);
    enterPageLock(page, threadInfo);
    addPageToTail(&headActiveList, page, threadInfo);
    leavePageLock(page, threadInfo);

    return !REDO_FAULT;

}


// this function assumes the page table sharedLock is held before being called and will be released for them when exiting
// all other locks must be entered and left before returning
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    ULONG64 frameNumber;

    PTHREAD_INFO threadInfo;


    threadInfo = (PTHREAD_INFO)threadContext;



    if (mapPageFromFreeList(arbitrary_va, threadInfo, &frameNumber) == REDO_FAULT) {
        if (mapPageFromStandByList(arbitrary_va, currentPageTableLock, (PTHREAD_INFO) threadContext, &frameNumber) == REDO_FAULT) {

            LeaveCriticalSection(currentPageTableLock);
            // sharedLock cant be released till here because the event could be reset

            ResetEvent(writingEndEvent);
            SetEvent(trimmingStartEvent);



            WaitForSingleObject(writingEndEvent, INFINITE);

            EnterCriticalSection(currentPageTableLock);



            return REDO_FAULT;
        }
    }

    if (ReadULong64NoFence(&freeListLength) == STAND_BY_TRIM_THRESHOLD) {
        batchVictimsFromStandByList(threadInfo);
    }


    if (((double) (headStandByList.length + freeListLength) / ((double) NUMBER_OF_PHYSICAL_PAGES )) < .5) {
        SetEvent(trimmingStartEvent);
    }



    currentPTE->validFormat.frameNumber = frameNumber;
    currentPTE->validFormat.valid = 1;

    page = getPFNfromFrameNumber(frameNumber);
    page->pte = currentPTE;

    *(PULONG64)arbitrary_va = (ULONG_PTR) arbitrary_va;



    // AcquireSRWLockExclusive(&headActiveList.sharedLock);
    // InsertTailList( &headActiveList, &page->entry);
    // ReleaseSRWLockExclusive(&headActiveList.sharedLock);
    enterPageLock(page, threadInfo);
    addPageToTail(&headActiveList, page, threadInfo);
    leavePageLock(page, threadContext);

    return !REDO_FAULT;
}
BOOL mapPageFromFreeList (ULONG64 arbitrary_va, PTHREAD_INFO threadInfo, PULONG64 frameNumber) {

    pte* currentPTE;
    pfn* page;
    ULONG64 localFreeListIndex;



    currentPTE = va_to_pte(arbitrary_va);




    page = getPageFromFreeList(threadInfo);
    if (page == LIST_IS_EMPTY) {

        return REDO_FAULT;
    }


    leavePageLock(page, threadInfo);


    *frameNumber = getFrameNumber(page);



    if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
        modified_read(currentPTE, *frameNumber, threadInfo);
    } else {
        zeroOnePage(page, threadInfo);
    }

    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
        return FALSE;
    }
    return !REDO_FAULT;
    //   ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));
}
BOOL mapPageFromStandByList (ULONG64 arbitrary_va, PCRITICAL_SECTION currentPageTableLock, PTHREAD_INFO threadInfo, PULONG64 frameNumber) {
    pte* currentPTE;
    pfn* page;
    pte entryContents;

    BOOL isPageZeroed;



    isPageZeroed = FALSE;
    currentPTE = va_to_pte(arbitrary_va);
    entryContents = *currentPTE;



    page = getVictimFromStandByList(currentPageTableLock, threadInfo);

    if (page == NULL) {
        return REDO_FAULT;
    }



    if (entryContents.invalidFormat.diskIndex == EMPTY_PTE) {
        if (!zeroOnePage(page, threadInfo)) {
            DebugBreak();
        }
        isPageZeroed = TRUE;
        //add flag saying that I zeroed
    }



    *frameNumber = getFrameNumber(page);

    if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
        modified_read(currentPTE, *frameNumber, threadInfo);
    }


       if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, *frameNumber);
        return FALSE;
    }


 //   ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));

    return !REDO_FAULT;

}

// gets a page off of standby and returns it
pfn* getVictimFromStandByList (PCRITICAL_SECTION currentPageTableLock, PTHREAD_INFO threadInfo) {

    pfn* page;


    page = RemoveFromHeadofPageList(&headStandByList, threadInfo);

    if (page == LIST_IS_EMPTY) {
        return NULL;
    }



    page->pte->transitionFormat.contentsLocation = DISK;
    page->pte->invalidFormat.diskIndex = page->diskIndex;

    leavePageLock(page, threadInfo);



    return page;
}

VOID
modified_read(pte* currentPTE, ULONG64 frameNumber, PTHREAD_INFO threadContext) {


    // Modified reading
    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;



    PVOID transferVaLocation = getThreadMapping(threadContext);

    // MUPP(va, size, physical page)
    if (MapUserPhysicalPages(transferVaLocation, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaLocation, frameNumber);
        return;
    }



    memcpy(transferVaLocation, (PVOID) diskAddress, PAGE_SIZE);

   freeThreadMapping(threadContext);

    set_disk_space_free(diskIndex);
}

// returns true if succeeds, wipes a physical page
//have an array of these transfers vas and locks, and try enter, and keep going down until you get one
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

pfn* getPageFromFreeList(PTHREAD_INFO threadContext) {
    ULONG64 localFreeListIndex;
    pfn* page;


    localFreeListIndex = InterlockedIncrement(&freeListRemoveIndex) % NUMBER_OF_FREE_LISTS;




    while (TRUE) {
        if (TryEnterCriticalSection(&headFreeLists[localFreeListIndex].page.lock) == TRUE) {
            page = RemoveFromHeadWithListLockHeld(&headFreeLists[localFreeListIndex], threadContext);
            leavePageLock(&headFreeLists[localFreeListIndex].page, threadContext);
            if (page != LIST_IS_EMPTY) {
                InterlockedDecrement(&freeListLength);
                return page;
            }
        }
        localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
    }
}