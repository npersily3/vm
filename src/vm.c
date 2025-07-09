#include "init.h"
#include "pages.h"
#include "disk.h"
#include <stdio.h>
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


   // DebugBreak();

    PULONG_PTR arbitrary_va;
    unsigned random_number;
    unsigned i;

    BOOL page_faulted;
    BOOL redo_fault;
    ULONG64 counter;

    arbitrary_va = NULL;
      // Now perform random accesses
    while (true) {//for (int i = 0; i < MB(1); i++) {
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



            random_number = (unsigned) (GetTimeStampCounter() >> 4);
            random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;//virtual_address_size_in_unsigned_chunks;

            // Write the virtual address into each page. If we need to
            // debug anything, we'll be able to see these in the pages.


            // Ensure the write to the arbitrary virtual address doesn't
            // straddle a PAGE_SIZE boundary just to keep things simple for now.
            random_number &= ~0x7;
            arbitrary_va = vaStart + random_number;

        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page_faulted = TRUE;
        }

        if (page_faulted) {

            //continuously fault on the same va
            redo_fault = REDO_FAULT;
            counter = 0;
            while (redo_fault == REDO_FAULT) {
                redo_fault = pageFault(arbitrary_va, lpParam);
                counter += 1;
                if (counter == MB(1)) {
                    DebugBreak();
                    printf("Fault overflow, at va %u", arbitrary_va);
                }
            }
           // *arbitrary_va = (ULONG_PTR) arbitrary_va;
        }
    }

    // printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);
    // SetEvent(userEndEvent);
    // return 0;
}


//nptodo remove 2nd transition bit, and check the pfn diskIndex to see what list it is on, then pass into rescue what list it is
//then you will need to reset diskIndex after you map it
BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID lpParam) {

   if (isVaValid(arbitrary_va) == FALSE) {
       DebugBreak();
       printf("invalid va %p\n", arbitrary_va);
   }

    BOOL returnValue = !REDO_FAULT;

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
                returnValue = REDO_FAULT;
            }
        } else {
                // if this returns REDO FAULT, map_page has released the pte lock
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

// this function assumes the page table lock is held before being called and will be released for them when exiting
// all other locks must be entered and left before returning

BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE) {
    pfn* page;
    ULONG64 frameNumber;
    ULONG64 pageStart;
    pte entryContents = *currentPTE;

    pageStart = arbitrary_va & ~(PAGE_SIZE - 1);

        frameNumber = currentPTE->transitionFormat.frameNumber;
        page = getPFNfromFrameNumber(frameNumber);



    if (page->isBeingWritten == TRUE) {
        page->isBeingWritten = FALSE;

    }
    else if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        EnterCriticalSection(lockStandByList);
        removeFromMiddleOfList(&headStandByList,&page->entry);
        LeaveCriticalSection(lockStandByList);

        set_disk_space_free(page->diskIndex);

    } else {

       // EnterCriticalSection(lockModifiedList);
        acquireLock(&lockModList);
        removeFromMiddleOfList(&headModifiedList,&page->entry);
        releaseLock(&lockModList);
        //  LeaveCriticalSection(lockModifiedList);
    }


    if (MapUserPhysicalPages((PVOID)arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
        return FALSE;
    }



    currentPTE->validFormat.valid = 1;


//    ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));

    EnterCriticalSection(lockActiveList);
    InsertTailList(&headActiveList, &page->entry);
    LeaveCriticalSection(lockActiveList);


    return !REDO_FAULT;

}

// returns true if succeeds, wipes a physical page
//have an array of these transfers vas and locks, and try enter, and keep going down until you get one
BOOL zeroPage (pfn* page, ULONG64 threadNumber) {

    PVOID transferVa;
    ULONG64 frameNumber;

    if (threadNumber == NUMBER_OF_THREADS) {
        transferVa = zeroThreadTransferVa;
    } else {
        transferVa = userThreadTransferVa[threadNumber];
    }
    frameNumber = getFrameNumber(page);

    if (MapUserPhysicalPages(transferVa, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", userThreadTransferVa[threadNumber], frameNumber);
        return FALSE;
    }
    memset(userThreadTransferVa[threadNumber], 0, PAGE_SIZE);

    if (MapUserPhysicalPages(transferVa, 1, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", userThreadTransferVa[threadNumber], frameNumber);
        return FALSE;
    }

    return TRUE;
}


// this function assumes the page table lock is held before being called and will be released for them when exiting
// all other locks must be entered and left before returning
BOOL mapPage(ULONG64 arbitrary_va,pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock) {

    pfn* page;
    ULONG64 frameNumber;
    pte entryContents = *currentPTE;
    PTHREAD_INFO threadInfo;
    PCRITICAL_SECTION victimPageTableLock;
    ULONG64 pageStart;
    BOOL isPageZeroed;

    isPageZeroed = FALSE;
    threadInfo = (PTHREAD_INFO)threadContext;
    pageStart = arbitrary_va & ~(PAGE_SIZE - 1);

    // if it is not empty need locks around these type of checks otherwise blow up
    EnterCriticalSection(lockFreeList);
    if (headFreeList.length != 0) {


        page = container_of(RemoveHeadList(&headFreeList),pfn,entry);

        LeaveCriticalSection(lockFreeList);


        frameNumber = getFrameNumber(page);


        if (currentPTE->invalidFormat.diskIndex != EMPTY_PTE) {
            modified_read(currentPTE, frameNumber, threadInfo->ThreadNumber);
        }

        if (MapUserPhysicalPages(arbitrary_va, 1, &frameNumber) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
            return FALSE;
        }
     //   ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));

    } else {
        LeaveCriticalSection(lockFreeList);

        // standby is not empty
        //nptodo make it so that this condition is based off the standBy density
        EnterCriticalSection(lockStandByList);
        if (headStandByList.length != 0) {


            // while holding the standByLock peek into standby
            page = container_of(headStandByList.entry.Flink, pfn, entry);
            victimPageTableLock = getPageTableLock(page->pte);


            //be a good samaritan and dont hold 2 pte locks at once, later check to see if the contents have changed
            LeaveCriticalSection(currentPageTableLock);


            // if you can get the standby lock, you can safely hold both locks without deadlocking
            //otherwise redo the fault
            if (TryEnterCriticalSection(victimPageTableLock) == FALSE) {
                LeaveCriticalSection(lockStandByList);
                EnterCriticalSection(currentPageTableLock);
                return REDO_FAULT;
            }

            //   removeFromMiddleOfList(&headStandByList, &page->entry);
            page = container_of(RemoveHeadList(&headStandByList), pfn, entry);



            page->pte->transitionFormat.contentsLocation = DISK;
            page->pte->invalidFormat.diskIndex = page->diskIndex;


            //now that I have recieved a page and edited its pte, a rescue can no longer happen,
            //and I no longer need the pte or any page list, I can release both locks
            LeaveCriticalSection(victimPageTableLock);
            LeaveCriticalSection(lockStandByList);


            if (entryContents.invalidFormat.diskIndex == EMPTY_PTE) {
                if (!zeroPage(page, threadInfo->ThreadNumber)) {
                    DebugBreak();
                }
                isPageZeroed = TRUE;
                //add flag saying that I zeroed
            }
        //nptodo think about adding a zeroing thread that zeros a page and then adds it the free list


            // re-enter the faulting va's lock and see if things changed and map the page accordingly
            //I make a copy instead of holding 2 pte locks because there is a rare chance of the contents being changed
            // and a redo fault occuring, while the bottleneck of holding two locks is large //nptodo check in xperf
            EnterCriticalSection(currentPageTableLock);


            if (currentPTE->entireFormat != entryContents.entireFormat) {
                if (isPageZeroed == FALSE) {
                    acquireLock(&lockToBeZeroedList);
                    InsertTailList(&headToBeZeroedList, &page->entry);
                    releaseLock(&lockToBeZeroedList);

                }
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


         //   ASSERT(checkVa((PULONG64) pageStart, (PULONG64)arbitrary_va));


        } else {




            LeaveCriticalSection(currentPageTableLock);
            // lock cant be released till here because the event could be reset

            ResetEvent(writingEndEvent);
            SetEvent(trimmingStartEvent);

            //nptodo ask landy about thread scheduling
            LeaveCriticalSection(lockStandByList);

            WaitForSingleObject(writingEndEvent, INFINITE);

            EnterCriticalSection(currentPageTableLock);
            //nptodo right contract in a comment
            return REDO_FAULT;
        }
    }



    currentPTE->validFormat.frameNumber = frameNumber;
    currentPTE->validFormat.valid = 1;
    page->pte = currentPTE;

    *(PULONG64)arbitrary_va = (ULONG_PTR) arbitrary_va;



    EnterCriticalSection(lockActiveList);
    InsertTailList( &headActiveList, &page->entry);
    LeaveCriticalSection(lockActiveList);


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