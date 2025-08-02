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

// called with page lock held
VOID addPageToFreeList(pfn* page, PTHREAD_INFO threadInfo) {
    ULONG64 localFreeListIndex;
    ULONG64 oldLength;

    // add it to the correct free list
    localFreeListIndex = InterlockedIncrement(&freeListAddIndex);
    localFreeListIndex %= NUMBER_OF_FREE_LISTS;

    InterlockedIncrement(&freeListLength);

    // iterate through the freelists starting at the index calculated above and try to lock it
    while (TRUE) {
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
        // go around in a circle
        localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
    }



}


VOID batchVictimsFromStandByList(PTHREAD_INFO threadInfo) {
    listHead localList;
    pfn* page;
    pfn* nextPage;
    ULONG64 freeListIndex;

    init_list_head(&localList);


    if (removeBatchFromList(&headStandByList, &localList, threadInfo) == LIST_IS_EMPTY) {
        return;
    }
#if DBG
    validateList(&localList);
#endif
    page = container_of(localList.entry.Flink, pfn, entry);
    // for every page on the local list, update its pte without a lock because the pagelock protects it from rescues
    while (&page->entry != &localList.entry) {


        page->pte->transitionFormat.contentsLocation = DISK;
        page->pte->invalidFormat.diskIndex = page->diskIndex;

        nextPage = container_of(page->entry.Flink, pfn, entry);
        addPageToFreeList(page, threadInfo);
        leavePageLock(page, threadInfo);
        page = nextPage;

    }

}


//outputs the next free space in the kernel read va
PVOID getThreadMapping(PTHREAD_INFO threadContext) {
    PVOID currentTransferVa = userThreadTransferVa[threadContext->ThreadNumber];
    PVOID va = (PVOID) ((ULONG64) currentTransferVa + PAGE_SIZE  * threadContext->TransferVaIndex);

    threadContext->TransferVaIndex += 1;
    ASSERT(threadContext->TransferVaIndex <= SIZE_OF_TRANSFER_VA_SPACE_IN_PAGES);

    return va;
}

//unmaps a user kernel va only if all the pages are used up
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
//simple check, should always be called with a lock
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



    //If before we got our snapshot, the pte was changed to non rescue
    if (!isRescue(currentPTE)){

        return REDO_FAULT;
    }

    frameNumber = entryContents.transitionFormat.frameNumber;
    page = getPFNfromFrameNumber(frameNumber);

    // if our supposed page no longer maps to our copy
    if (page->pte->entireFormat != entryContents.entireFormat) {
        return REDO_FAULT;
    }


    enterPageLock(page, threadInfo);

    // Now that we are in the pagelock we can be sure the pte is not being edited without a lock in our victimization code.
    // Therefore, we can now check the actual pte for the same things as above as opposed to out copy

    if (page->pte != currentPTE) {
        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }

    if (!isRescue(currentPTE)) {

        leavePageLock(page, threadInfo);
        return REDO_FAULT;
    }
    ASSERT(currentPTE->transitionFormat.frameNumber == frameNumber);

    //if there is a write in progress the page is not on any list
    // we do not know when in the right this is occuring so we set this variable to signal to the writer to not add this
    // page to standby, but instead free its disk space
    if (page->isBeingWritten == TRUE) {
        page->isBeingWritten = FALSE;
    }
    // if its on standby, remove it and set the disk space free
    else if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        removeFromMiddleOfList(&headStandByList, page, threadInfo);

        set_disk_space_free(page->diskIndex);


    } else {
        // it must be on the modified list now.
        removeFromMiddleOfList(&headModifiedList,page, threadInfo);
    }
    // we can leave this lock because it is no longer on a list and another faulter will be stopped by the page table lock
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


// this function assumes the page table lock is held before being called and will be released for them when exiting
// all other locks must be entered and left before returning
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    ULONG64 frameNumber;
    PTHREAD_INFO threadInfo;
    BOOL pruneInProgress;

    threadInfo = (PTHREAD_INFO)threadContext;




    if (mapPageFromFreeList(arbitrary_va, threadInfo, &frameNumber) == REDO_FAULT) {
        if (mapPageFromStandByList(arbitrary_va, currentPageTableLock, (PTHREAD_INFO) threadContext, &frameNumber) == REDO_FAULT) {

            // if we were not able to from freelist to modified, start the trimmer and go to sleep


            LeaveCriticalSection(currentPageTableLock);

            //TODO find a way to resolve the case where it resets writing end event and there is a deadlock
            ResetEvent(writingEndEvent);
            SetEvent(trimmingStartEvent);

            pageWaits++;

            ULONG64 start;
            ULONG64 end;

            start = ReadTimeStampCounter();

            WaitForSingleObject(writingEndEvent, INFINITE);

           end = ReadTimeStampCounter();

            InterlockedAdd64(&totalTimeWaiting, end - start);

            EnterCriticalSection(currentPageTableLock);



            return REDO_FAULT;
        }
    }





    // if we seem to be running low on free and standby wake the trimmer
    if (((double) (headStandByList.length + ReadULong64NoFence(&freeListLength)) / ((double) NUMBER_OF_PHYSICAL_PAGES )) < .5) {
        SetEvent(trimmingStartEvent);
    }



    //validate the pte and add it to the list
    currentPTE->validFormat.frameNumber = frameNumber;
    currentPTE->validFormat.valid = 1;

    page = getPFNfromFrameNumber(frameNumber);
    page->pte = currentPTE;


    enterPageLock(page, threadInfo);
    addPageToTail(&headActiveList, page, threadInfo);
    leavePageLock(page, threadContext);

    return !REDO_FAULT;
}

//assumes pte lock is held on entry and will be released
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



    // read if neccesary or zero it
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

    *frameNumber = getFrameNumber(page);


    if (entryContents.invalidFormat.diskIndex == EMPTY_PTE) {
        if (!zeroOnePage(page, threadInfo)) {
            DebugBreak();
        }
        isPageZeroed = TRUE;
    } else {
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

// gets a page off of standby and return it
pfn* getVictimFromStandByList (PCRITICAL_SECTION currentPageTableLock, PTHREAD_INFO threadInfo) {

    pfn* page;
    pte local;


    page = RemoveFromHeadofPageList(&headStandByList, threadInfo);

    if (page == LIST_IS_EMPTY) {
        return NULL;
    }


    // we can edit the pte without a lock because the page lock protects it
    // if another faulter tries to get this the pagelock will stop it in the rescue
    // if the rescue sees a change in the pte it will redo the fault

    local.transitionFormat.contentsLocation = DISK;
    local.invalidFormat.diskIndex = page->diskIndex;

    WriteULong64NoFence(page->pte,  local.entireFormat);

    leavePageLock(page, threadInfo);


    return page;
}

// copies the contents of a disk page into a physical frame number, and frees disk space
// assumes pagetable lock is held on entry and will be release by the caller
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

// zeroes one physical page
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

// gets a page of off the free list, assumes caller will release pagelock
pfn* getPageFromFreeList(PTHREAD_INFO threadContext) {
    ULONG64 localFreeListIndex;
    PLIST_ENTRY entry;
    pfn* page;
    ULONG64 localTotalFreeListLength;
    boolean pruneInProgress = FALSE;
    boolean gotFreeListLock = FALSE;
    ULONG64 counter;


    counter = 0;
    localFreeListIndex = InterlockedIncrement(&freeListRemoveIndex) % NUMBER_OF_FREE_LISTS;


    // iterate through the freelists starting at the index calculated above and try to lock it

    while (TRUE) {
        if (counter < NUMBER_OF_FREE_LISTS) {
            if (TryEnterCriticalSection(&headFreeLists[localFreeListIndex].page.lock) == TRUE) {
                gotFreeListLock = TRUE;
            }
        } else if (counter == 2* NUMBER_OF_FREE_LISTS) {
            return LIST_IS_EMPTY;
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

            LeaveCriticalSection(&headFreeLists[localFreeListIndex].page.lock);

            if (page == LIST_IS_EMPTY) {
                gotFreeListLock = FALSE;
                counter++;
                localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
                continue;
            } else {

                localTotalFreeListLength = InterlockedDecrement(&freeListLength);

               ASSERT(localTotalFreeListLength != MAXULONG64);
                // check to see if we have enough pages on the free list
                if (localTotalFreeListLength <= STAND_BY_TRIM_THRESHOLD) {
                    // if there is not a pruning mission already happening
                    pruneInProgress = InterlockedCompareExchange(&standByPruningInProgress, TRUE, FALSE);

                    if (pruneInProgress == FALSE) {
                        batchVictimsFromStandByList(threadContext);
                    }

                    InterlockedExchange(&standByPruningInProgress, FALSE);
                }

                enterPageLock(page, threadContext);
                return page;
            }
        }

        // go around in a circle
        localFreeListIndex = (localFreeListIndex + 1) % NUMBER_OF_FREE_LISTS;
        counter++;
    }
}