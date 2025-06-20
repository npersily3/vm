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

    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;

    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;


    // Allocate the physical pages that we will be managing.
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf("full_virtual_memory_test : could not get privilege\n");
        return;
    }

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
    physical_page_handle = CreateSharedMemorySection();

    if (physical_page_handle == NULL) {
        printf("CreateFileMapping2 failed, error %#x\n", GetLastError());
        return;
    }
#else
    physical_page_handle = GetCurrentProcess();
#endif

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;
    physical_page_numbers = (PULONG_PTR)malloc(physical_page_count * sizeof(ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return;
    }

    allocated = AllocateUserPhysicalPages(physical_page_handle,
                                          &physical_page_count,
                                          physical_page_numbers);

    if (allocated == FALSE) {
        printf("full_virtual_memory_test : could not allocate physical pages\n");
        return;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
               physical_page_count,
               NUMBER_OF_PHYSICAL_PAGES);
    }

    init_virtual_memory();

    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;

    // Round down to a PAGE_SIZE boundary
    virtual_address_size &= ~(PAGE_SIZE - 1);

    virtual_address_size_in_unsigned_chunks = virtual_address_size / sizeof(ULONG_PTR);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section created above
    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    vaStart = (PULONG_PTR)VirtualAlloc2(NULL,
                                        NULL,
                                        virtual_address_size,
                                        MEM_RESERVE | MEM_PHYSICAL,
                                        PAGE_READWRITE,
                                        &parameter,
                                        1);
#else
    vaStart = (PULONG_PTR)VirtualAlloc(NULL,
                                       virtual_address_size,
                                       MEM_RESERVE | MEM_PHYSICAL,
                                       PAGE_READWRITE);

    transferVa = VirtualAlloc(NULL,
                              PAGE_SIZE * BATCH_SIZE,
                              MEM_RESERVE | MEM_PHYSICAL,
                              PAGE_READWRITE);
#endif

    if (vaStart == NULL) {
        printf("full_virtual_memory_test : could not reserve memory %x\n", GetLastError());
        return;
    }



    // SetEvent(GlobalStartEvent);
    //
    //
    // WaitForMultipleObjects(NUMBER_OF_THREADS, workDoneThreadHandles, WAIT_FOR_ALL, INFINITE);

    // Now that we're done with our memory we can be a good
    // citizen and free it.
    VirtualFree(vaStart, 0, MEM_RELEASE);
    return;
}

DWORD testVM(LPVOID lpParam) {



    pte* currentPTE;

    PULONG_PTR arbitrary_va;
    unsigned random_number;
    unsigned i;
    BOOL allocated;
    BOOL page_faulted;

    //this line is what put it all together
    WaitForSingleObject(GlobalStartEvent, INFINITE);
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

        random_number = (unsigned) (GetTimeStampCounter() >> 4);
        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;//virtual_address_size_in_unsigned_chunks;

        // Write the virtual address into each page. If we need to
        // debug anything, we'll be able to see these in the pages.
        page_faulted = FALSE;

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

            currentPTE = va_to_pte(arbitrary_va);

            // if the page can be rescued
            if (currentPTE->transitionFormat.contentsLocation == MODIFIED_LIST ||
                currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {
                rescue_page(arbitrary_va, currentPTE);
            } else {
                mapPage(arbitrary_va, currentPTE);
            }

            *arbitrary_va = (ULONG_PTR) arbitrary_va;
            checkVa((PULONG64)arbitrary_va);

        }
    }

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);
    return 0;
}


VOID rescue_page(ULONG64 arbitrary_va, pte* currentPTE) {
    pfn* page;
    ULONG64 frameNumber;

        frameNumber = currentPTE->transitionFormat.frameNumber;
        page = getPFNfromFrameNumber(frameNumber);

    //two different kinds of locks
    if (currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {

        EnterCriticalSection(&lockStandByList);
        removeFromMiddleOfList(page);
        LeaveCriticalSection(&lockStandByList);

        wipePage(page->diskIndex);

    } else {
        EnterCriticalSection(&lockModifiedList);
        removeFromMiddleOfList(page);
        LeaveCriticalSection(&lockModifiedList);
    }

    if (MapUserPhysicalPages(arbitrary_va, 1, &frameNumber) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
        return;
    }

    // validate pte and add it to the activeList
    currentPTE->transitionFormat.contentsLocation = ACTIVE_LIST;
    currentPTE->validFormat.valid = 1;

    EnterCriticalSection(&lockActiveList);
    InsertTailList(&page->entry, &headActiveList);
    LeaveCriticalSection(&lockActiveList);
}


VOID mapPage(ULONG64 arbitrary_va,pte* currentPTE) {

    pfn* page;
    ULONG64 frameNumber;
    // if it is not empty
    if (headFreeList.Flink != &headFreeList) {

        EnterCriticalSection(&lockFreeList);
        page = RemoveHeadList(&headFreeList);
        LeaveCriticalSection(&lockFreeList);


        frameNumber = getFrameNumber(page);
        if (currentPTE->invalidFormat.diskIndex == EMPTY_PTE) {
            if (MapUserPhysicalPages(arbitrary_va, 1, &frameNumber) == FALSE) {
                DebugBreak();
                printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
                return;
            }
        } else {
            modified_read(currentPTE, frameNumber);
        }
    } else {
        if (headStandByList.Flink != &headStandByList) {
            EnterCriticalSection(&lockStandByList);
            page = RemoveHeadList(&headStandByList);
            LeaveCriticalSection(&lockStandByList);

            page->pte->transitionFormat.contentsLocation = DISK;
            page->pte->invalidFormat.diskIndex = page->diskIndex;

            frameNumber = getFrameNumber(page);

            if (MapUserPhysicalPages(arbitrary_va, 1, &frameNumber) == FALSE) {
                DebugBreak();
                printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, frameNumber);
                return;
            }

        } else {
            SetEvent(&trimmingStartEvent);
            // ask what to do from here
            WaitForSingleObject(writingEndEvent, INFINITE);
            // if the page can be rescued
            if (currentPTE->transitionFormat.contentsLocation == MODIFIED_LIST ||
                currentPTE->transitionFormat.contentsLocation == STAND_BY_LIST) {
                rescue_page(arbitrary_va, currentPTE);
                } else {
                    mapPage(arbitrary_va, currentPTE);
                }
        }
    }

    //do I need a lock here
    currentPTE->validFormat.frameNumber = frameNumber;
    currentPTE->validFormat.transition = ACTIVE_LIST;
    currentPTE->validFormat.valid = 1;

    EnterCriticalSection(&lockActiveList);
    InsertTailList( &headActiveList, &page->entry);
    LeaveCriticalSection(&lockActiveList);
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