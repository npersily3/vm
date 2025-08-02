//
// Created by nrper on 7/30/2025.
//

#include "../../include/threads/user_free.h"

#include <stdio.h>

#include "disk/disk.h"
#include "threads/user_thread.h"
#include "utils/page_utils.h"
#include "utils/pte_utils.h"
#include "utils/thread_utils.h"
#include "variables/globals.h"


BOOL freeVa(PULONG64 arbitrary_va, PTHREAD_INFO threadInfo) {

    pte* currentPTE;
    PTE_REGION* region;
    PCRITICAL_SECTION currentPageTableLock;

    boolean wasOnPage;
    pfn* page;
    ULONG64 freeListIndex;

    if (isVaValid((ULONG64) arbitrary_va) == FALSE) {
        DebugBreak();

    }

    currentPTE = va_to_pte((ULONG64) arbitrary_va);


    region = getPTERegion(currentPTE);
    currentPageTableLock = &region->lock;

    EnterCriticalSection(currentPageTableLock);


    if (currentPTE->validFormat.valid == 1) {
        unmapActivePage(currentPTE, threadInfo, page);
        wasOnPage = TRUE;
    } else if (isRescue(currentPTE)) {
        // sets wasOnPage
      if (unmapRescuePage(currentPTE, threadInfo, page, &wasOnPage) == REDO_FREE) {
          return REDO_FREE;
      }
    } else {
        unmapDiskFormatPTE(currentPTE, threadInfo);
        wasOnPage = FALSE;
    }

    if (wasOnPage) {
        zeroOnePage(page, threadInfo);

        freeListIndex = InterlockedIncrement(&freeListAddIndex);
        freeListIndex %= NUMBER_OF_FREE_LISTS;
        addPageToTail(&headFreeLists[freeListIndex], page, threadInfo);
        leavePageLock(page, threadInfo);
        InterlockedIncrement(&freeListLength);
    }


    //TODO change it to a new pte format that signals that it has been freed
    currentPTE->entireFormat = EMPTY_PTE;


    return !REDO_FREE;
}


VOID unmapActivePage(pte* currentPTE, PTHREAD_INFO threadInfo, pfn* page) {
    PVOID va;

    ULONG64 frameNumber;


    va = pte_to_va(currentPTE);

    frameNumber = currentPTE->validFormat.frameNumber;

    page = getPFNfromFrameNumber(frameNumber);

    enterPageLock(page, threadInfo);
    removeFromMiddleOfList(&headActiveList, page, threadInfo);

    if (MapUserPhysicalPages(va, 1, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", va, frameNumber);
        return;
    }


    return;
}

BOOL unmapRescuePage(pte* currentPTE, PTHREAD_INFO threadInfo, pfn* page, boolean* addToFreeList) {


    ULONG64 frameNumber;

    pte entryContents;
    entryContents.entireFormat = ReadULong64NoFence((ULONG64)currentPTE);


    if ((entryContents.transitionFormat.contentsLocation != MODIFIED_LIST)  &&
        (entryContents.transitionFormat.contentsLocation != STAND_BY_LIST )) {

        return REDO_FREE;
    }

    frameNumber = entryContents.transitionFormat.frameNumber;
    page = getPFNfromFrameNumber(frameNumber);

    if (page->pte->entireFormat != entryContents.entireFormat) {
        return REDO_FREE;
    }


    enterPageLock(page, threadInfo);

    if (page->pte != currentPTE) {
        leavePageLock(page, threadInfo);
        return REDO_FREE;
    }

    if ((currentPTE->transitionFormat.contentsLocation != MODIFIED_LIST)  &&
        (currentPTE->transitionFormat.contentsLocation != STAND_BY_LIST )) {

        leavePageLock(page, threadInfo);
        return REDO_FREE;
    }
    ASSERT(currentPTE->transitionFormat.frameNumber == frameNumber);
    //if there is a write in progress
    if (page->isBeingWritten == TRUE) {
        page->isBeingWritten = FALSE;
        page->isBeingFreed = TRUE;
        leavePageLock(page, threadInfo);
        *addToFreeList = FALSE;
    }
    else if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        removeFromMiddleOfList(&headStandByList, page, threadInfo);
        set_disk_space_free(page->diskIndex);
        *addToFreeList = TRUE;

    } else {
        removeFromMiddleOfList(&headModifiedList,page, threadInfo);
        *addToFreeList = TRUE;
    }



    return !REDO_FREE;
}

BOOL unmapDiskFormatPTE(pte* currentPTE, PTHREAD_INFO threadInfo) {
    set_disk_space_free(currentPTE->invalidFormat.diskIndex);
}