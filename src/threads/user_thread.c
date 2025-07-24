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


//nptodo remove 2nd transition bit, and check the pfn diskIndex to see what list it is on, then pass into rescue what list it is
//then you will need to reset diskIndex after you map it
BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID lpParam) {



    BOOL returnValue;
    pte* currentPTE;
    pte pteContents;
    PCRITICAL_SECTION currentPageTableLock;
    PTE_REGION* region;

    currentPTE = va_to_pte(arbitrary_va);


    if (isVaValid(arbitrary_va) == FALSE) {
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

            if (rescue_page(arbitrary_va, currentPTE) == REDO_FAULT) {
                returnValue = REDO_FAULT;
            }
            } else {

                if (mapPage(arbitrary_va, currentPTE, lpParam, currentPageTableLock) == REDO_FAULT) {
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
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE) {
    pfn* page;
    ULONG64 frameNumber;
    ULONG64 pageStart;
    pte entryContents = *currentPTE;

    pageStart = arbitrary_va & ~(PAGE_SIZE - 1);

    frameNumber = currentPTE->transitionFormat.frameNumber;
    page = getPFNfromFrameNumber(frameNumber);

    //if there is a write in progress
    if (page->isBeingWritten == TRUE) {
        page->isBeingWritten = FALSE;
    }
    else if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        removeFromMiddleOfList(&headStandByList, page);

        set_disk_space_free(page->diskIndex);

    } else {

        // EnterCriticalSection(lockModifiedList);

        removeFromMiddleOfList(&headModifiedList,page);

        //  LeaveCriticalSection(lockModifiedList);
    }


    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
        return FALSE;
    }

    currentPTE->validFormat.valid = 1;


    //    ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));

    // AcquireSRWLockExclusive(&headActiveList.sharedLock);
    // InsertTailList(&headActiveList, &page->entry);
    // ReleaseSRWLockExclusive(&headActiveList.sharedLock);
    addPageToTail(&headActiveList, page);

    return !REDO_FAULT;

}


// this function assumes the page table sharedLock is held before being called and will be released for them when exiting
// all other locks must be entered and left before returning
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    ULONG64 frameNumber;
    pte entryContents = *currentPTE;
    PTHREAD_INFO threadInfo;
    PCRITICAL_SECTION victimPageTableLock;
    ULONG64 pageStart;

    threadInfo = (PTHREAD_INFO)threadContext;
    pageStart = arbitrary_va & ~(PAGE_SIZE - 1);

    // if it is not empty need locks around these type of checks otherwise blow up
    AcquireSRWLockExclusive(&headFreeList.sharedLock);
    if (headFreeList.length != 0) {
        mapPageFromFreeList(arbitrary_va, threadInfo, &frameNumber);
    } else {
        ReleaseSRWLockExclusive(&headFreeList.sharedLock);

        // standby is not empty

        AcquireSRWLockExclusive(&headStandByList.sharedLock);
        if (headStandByList.length != 0) {
            if (mapPageFromStandByList(arbitrary_va, currentPageTableLock, (PTHREAD_INFO) threadContext, &frameNumber) == REDO_FAULT) {
                return REDO_FAULT;
            }
        } else {


            LeaveCriticalSection(currentPageTableLock);
            // sharedLock cant be released till here because the event could be reset

            ResetEvent(writingEndEvent);
            SetEvent(trimmingStartEvent);


           ReleaseSRWLockExclusive(&headStandByList.sharedLock);

            WaitForSingleObject(writingEndEvent, INFINITE);

            EnterCriticalSection(currentPageTableLock);

            return REDO_FAULT;
        }
    }
    if (((double) (headStandByList.length + headFreeList.length) / ((double) NUMBER_OF_PHYSICAL_PAGES )) < .5) {
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
    addPageToTail(&headActiveList, page);

    return !REDO_FAULT;
}
BOOL mapPageFromFreeList (ULONG64 arbitrary_va, PTHREAD_INFO threadInfo, PULONG64 frameNumber) {

    pte* currentPTE;
    pfn* page;




    currentPTE = va_to_pte(arbitrary_va);

    page = container_of(RemoveHeadList(&headFreeList),pfn,entry);

    ReleaseSRWLockExclusive(&headFreeList.sharedLock);


    *frameNumber = getFrameNumber(page);



    if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
        modified_read(currentPTE, *frameNumber, threadInfo->ThreadNumber);
    } else {
        zeroOnePage(page, threadInfo->ThreadNumber);
    }

    if (MapUserPhysicalPages(arbitrary_va, 1, frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
        return FALSE;
    }
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



    page = getVictimFromStandByList(currentPageTableLock);

    if (page == NULL) {
        return REDO_FAULT;
    }


    if (entryContents.invalidFormat.diskIndex == EMPTY_PTE) {
        if (!zeroOnePage(page, threadInfo->ThreadNumber)) {
            DebugBreak();
        }
        isPageZeroed = TRUE;
        //add flag saying that I zeroed
    }


    // re-enter the faulting va's sharedLock and see if things changed and map the page accordingly
    //I make a copy instead of holding 2 pte locks because there is a rare chance of the contents being changed
    // and a redo fault occuring, while the bottleneck of holding two locks is large //nptodo check in xperf
    EnterCriticalSection(currentPageTableLock);


    if (currentPTE->entireFormat != entryContents.entireFormat) {
        // if (isPageZeroed == FALSE) {
        //     acquireLock(&lockToBeZeroedList);
        //     InsertTailList(&headToBeZeroedList, &page->entry);
        //     releaseLock(&lockToBeZeroedList);
        //
        //     if (headToBeZeroedList.length == BATCH_SIZE ) {
        //         SetEvent(zeroingStartEvent);
        //     }
        //
        // } else {
            AcquireSRWLockExclusive(&headFreeList.sharedLock);
            InsertTailList(&headFreeList, &page->entry);
            ReleaseSRWLockExclusive(&headFreeList.sharedLock);
        //}
        return REDO_FAULT;
    }

    *frameNumber = getFrameNumber(page);

    if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
        modified_read(currentPTE, *frameNumber, threadInfo->ThreadNumber);
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
pfn* getVictimFromStandByList (PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    PCRITICAL_SECTION victimPageTableLock;
    PTE_REGION* victimRegion;

    page = container_of(headStandByList.entry.Flink, pfn, entry);

    victimRegion = getPTERegion(page->pte);


    victimPageTableLock = &victimRegion->lock;


    //be a good samaritan and dont hold 2 pte locks at once, later check to see if the contents have changed
    LeaveCriticalSection(currentPageTableLock);


    // if you can get the standby sharedLock, you can safely hold both locks without deadlocking
    //otherwise redo the fault
    if (TryEnterCriticalSection(victimPageTableLock) == FALSE) {
        ReleaseSRWLockExclusive(&headStandByList.sharedLock);
        EnterCriticalSection(currentPageTableLock);
        return NULL;
    }

    //   removeFromMiddleOfList(&headStandByList, &page->entry);
    page = container_of(RemoveHeadList(&headStandByList), pfn, entry);



    page->pte->transitionFormat.contentsLocation = DISK;
    page->pte->invalidFormat.diskIndex = page->diskIndex;

    LeaveCriticalSection(victimPageTableLock);
    ReleaseSRWLockExclusive(&headStandByList.sharedLock);

    return page;
}

VOID
modified_read(pte* currentPTE, ULONG64 frameNumber, ULONG64 threadNumber) {
    // Modified reading
    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;




    // MUPP(va, size, physical page)
    if (MapUserPhysicalPages(userThreadTransferVa[threadNumber], 1, &frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", userThreadTransferVa[threadNumber], frameNumber);
        return;
    }

    // memcpy(va,va,size)

    memcpy(userThreadTransferVa[threadNumber], (PVOID) diskAddress, PAGE_SIZE);

    if (MapUserPhysicalPages(userThreadTransferVa[threadNumber], 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", userThreadTransferVa[threadNumber]);
        return;
    }

    set_disk_space_free(diskIndex);
}

// returns true if succeeds, wipes a physical page
//have an array of these transfers vas and locks, and try enter, and keep going down until you get one
BOOL zeroOnePage (pfn* page, ULONG64 threadNumber) {

    PVOID transferVa;
    ULONG64 frameNumber;


    transferVa = userThreadTransferVa[threadNumber];

    frameNumber = getFrameNumber(page);

    if (MapUserPhysicalPages(transferVa, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", userThreadTransferVa[threadNumber], frameNumber);
        return FALSE;
    }
    memset(transferVa, 0, PAGE_SIZE);

    if (MapUserPhysicalPages(transferVa, 1, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", userThreadTransferVa[threadNumber], frameNumber);
        return FALSE;
    }

    return TRUE;
}