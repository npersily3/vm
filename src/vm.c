#include "init.h"
#include "pages.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <intrin.h>
#include "vm.h"
#include "macros.h"

// Timestamp counter for random number generation
static __inline unsigned __int64 GetTimeStampCounter(void) {
    return __rdtsc();
}


VOID
full_virtual_memory_test(VOID) {





    init_virtual_memory();

    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.




    if (vaStart == NULL) {
        printf("full_virtual_memory_test : could not reserve memory %x\n", GetLastError());
        return;
    }


    SetEvent(userStartEvent);

    WaitForSingleObject(userEndEvent, INFINITE);

    // Now that we're done with our memory we can be a good
    // citizen and free it.
    VirtualFree(vaStart, 0, MEM_RELEASE);
    return;
}

DWORD testVM(LPVOID lpParam) {

    WaitForSingleObject(userStartEvent, INFINITE);



    PULONG_PTR arbitrary_va;
    unsigned random_number;
    unsigned i;

    BOOL page_faulted;

    //this line is what put it all together

    arbitrary_va = NULL;
      // Now perform random accesses
    for (i = 0; i < MB(1); i += 1) {
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation. Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        page_faulted = FALSE;


        if (arbitrary_va == NULL) {
            random_number = (unsigned) (GetTimeStampCounter() >> 4);
            random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;//virtual_address_size_in_unsigned_chunks;

            // Write the virtual address into each page. If we need to
            // debug anything, we'll be able to see these in the pages.


            // Ensure the write to the arbitrary virtual address doesn't
            // straddle a PAGE_SIZE boundary just to keep things simple for now.
            random_number &= ~0x7;
            arbitrary_va = vaStart + random_number;
        }
        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page_faulted = TRUE;
        }

        if (page_faulted) {
            if (pageFault(arbitrary_va, lpParam) == REDO_FAULT) {
             i--;
            }

        } else {
            arbitrary_va = NULL;
        }


    }

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);
    SetEvent(userEndEvent);
    return 0;
}



BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID lpParam) {

    //nptodo add a check to see if the user code is in the accessible va range

    pte* currentPTE;
    pte pteContents;
    currentPTE = va_to_pte(arbitrary_va);
    PCRITICAL_SECTION currentPageTableLock;

    currentPageTableLock = getPageTableLock(currentPTE);




    EnterCriticalSection(currentPageTableLock);

    pteContents = *currentPTE;

    if (pteContents.validFormat.valid != TRUE) {
        // if the page can be rescued
        if (currentPTE->transitionFormat.contentsLocation == MODIFIED_LIST ||
            currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {
            if (rescue_page(arbitrary_va, currentPTE) == REDO_FAULT) {
                LeaveCriticalSection(currentPageTableLock);
                return REDO_FAULT;
            }
            } else {
                // if this returns REDO FAULT, map_page has released the pte lock
                if (mapPage(arbitrary_va, currentPTE, lpParam, currentPageTableLock) == REDO_FAULT) {
                    return REDO_FAULT;
                }
            }
    }
    LeaveCriticalSection(currentPageTableLock);


    return !REDO_FAULT;

}

// this function assumes a pagetable lock is being held
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE) {
    pfn* page;
    ULONG64 frameNumber;
    pte entryContents = *currentPTE;


        frameNumber = currentPTE->transitionFormat.frameNumber;
        page = getPFNfromFrameNumber(frameNumber);

    //two different kinds of locks
    if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        EnterCriticalSection(&lockStandByList);
        removeFromMiddleOfList(&headStandByList,&page->entry);
        LeaveCriticalSection(&lockStandByList);

        set_disk_space_free(page->diskIndex);

    } else {


        EnterCriticalSection(&lockModifiedList);
        removeFromMiddleOfList(&headModifiedList,&page->entry);
        LeaveCriticalSection(&lockModifiedList);
    }


    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
        return FALSE;
    }



    currentPTE->validFormat.valid = 1;

    checkVa((PULONG64)arbitrary_va);

    EnterCriticalSection(&lockActiveList);
    InsertTailList(&headActiveList, &page->entry);
    LeaveCriticalSection(&lockActiveList);


    return !REDO_FAULT;

}
BOOL zeroPage (pfn* page) {
    ULONG64 frameNumber;

    frameNumber = getFrameNumber(page);

    if (MapUserPhysicalPages(transferVaWipePage, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaWipePage, frameNumber);
        return FALSE;
    }
    memset(transferVaWipePage, 0, PAGE_SIZE);

    return TRUE;
}


// caller holds pte lock
BOOL mapPage(ULONG64 arbitrary_va,pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    ULONG64 frameNumber;
    pte entryContents = *currentPTE;
    PTHREAD_INFO threadInfo;
    PCRITICAL_SECTION victimPageTableLock;

    threadInfo = (PTHREAD_INFO)threadContext;

    // if it is not empty need locks around these type of checks otherwise blow up
    EnterCriticalSection(&lockFreeList);
    if (headFreeList.length != 0) {


        page = RemoveHeadList(&headFreeList);

        LeaveCriticalSection(&lockFreeList);


        frameNumber = getFrameNumber(page);


        if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
            modified_read(currentPTE, frameNumber, threadInfo->ThreadNumber);
        }

        if (MapUserPhysicalPages(arbitrary_va, 1, &frameNumber) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
            return FALSE;
        }
        checkVa((PULONG64)arbitrary_va);

    } else {
        LeaveCriticalSection(&lockFreeList);

        // standby is not empty
        EnterCriticalSection(&lockStandByList);
        if (headStandByList.length != 0) {

            // nptodo when I split my pte locks, I might need to access different pte locks in this
            //section, just fyi
            page = container_of(RemoveHeadList(&headStandByList), pfn, entry);
            LeaveCriticalSection(&lockStandByList);

            if (!zeroPage(page)) {
                DebugBreak();
            }
            // Can never hold 2 pagetable locks at once
            //so I release one to edit the victims, then I check if another thread edited the initial contents of the pte
            victimPageTableLock = getPageTableLock(page->pte);

            LeaveCriticalSection(currentPageTableLock);

            EnterCriticalSection(victimPageTableLock);
            page->pte->transitionFormat.contentsLocation = DISK;
            page->pte->invalidFormat.diskIndex = page->diskIndex;
            LeaveCriticalSection(victimPageTableLock);

            EnterCriticalSection(currentPageTableLock);


            if (currentPTE->entireFormat != entryContents.entireFormat) {
                return REDO_FAULT;
            }

            frameNumber = getFrameNumber(page);

            if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
                modified_read(currentPTE, frameNumber, threadInfo->ThreadNumber);
            }


            if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
                DebugBreak();
                printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
                return FALSE;
            }
            checkVa((PULONG64)arbitrary_va);

        } else {

            ResetEvent(writingEndEvent);

            LeaveCriticalSection(currentPageTableLock);

            // if there no victims, I need to start the pagetrimmer. While I'm waiting, I should release the lock
            // to avoid deadlocks


            SetEvent(trimmingStartEvent);

            WaitForSingleObject(writingEndEvent, INFINITE);

            return REDO_FAULT;
        }
    }



    currentPTE->validFormat.frameNumber = frameNumber;
    currentPTE->validFormat.valid = 1;
    page->pte = currentPTE;


    EnterCriticalSection(&lockActiveList);
    InsertTailList( &headActiveList, &page->entry);
    LeaveCriticalSection(&lockActiveList);


    return !REDO_FAULT;
}



VOID
main(int argc, char **argv) {
    // Test our very complicated usermode virtual implementation.
    //
    // We will control the virtual and physical address space management
    // ourselves with the only two exceptions being that we will :
    //
    // 1. Ask the operating system for the physical pages we'll use to
    //    form our pool.
    //
    // 2. Ask the operating system to connect one of our virtual addresses
    //    to one of our physical pages (from our pool).
    //
    // We would do both of those operations ourselves but the operating
    // system (for security reasons) does not allow us to.
    //
    // But we will do all the heavy lifting of maintaining translation
    // tables, PFN data structures, management of physical pages,
    // virtual memory operations like handling page faults, materializing
    // mappings, freeing them, trimming them, writing them out to backing
    // store, bringing them back from backing store, protecting them, etc.
    //
    // This is where we can be as creative as we like, the sky's the limit !


    full_virtual_memory_test();


}