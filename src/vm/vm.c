#include "../../include/initialization/init.h"

#include "../../include/variables/globals.h"
#include "../../include/variables/structures.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/disk/disk.h"
#include <stdio.h>
#include "vm.h"
#include "../../include/variables/macros.h"
#include "threads/user_thread.h"
#include "utils/random_utils.h"
#include "utils/statistics_utils.h"

// Timestamp counter for random number generation
static __inline unsigned __int64 GetTimeStampCounter(void) {
    return __rdtsc();
}
boolean setAccessBit(ULONG64 va) {
    pte* pteAddress;
    pte newPTE;
    pte oldPTE;
    pte returnValue;

    pteAddress = pte_to_va(va);
    oldPTE.entireFormat = ReadULong64NoFence(pteAddress);
    newPTE.entireFormat = oldPTE.entireFormat;

    newPTE.validFormat.access = 1;

   returnValue.entireFormat = InterlockedCompareExchange64(pteAddress, newPTE.entireFormat, oldPTE.entireFormat);

    if (returnValue.validFormat.valid == 0) {
        return REDO_FAULT;
    }
    return !REDO_FAULT;

}

ULONG64 noah;

VOID
full_virtual_memory_test(VOID) {



    noah = 1;

    ULONG64 start, end;


    CRITICAL_SECTION cs;
    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
  

    init_virtual_memory();


    start = GetTickCount64();

    printf("initialization done \n");
    SetEvent(vm.events.userStart);

    int i;
    i = 0;



     for (; i < vm.config.number_of_user_threads; ++i) {
         WaitForSingleObject(vm.events.userThreadHandles[i], INFINITE);
     }
    SetEvent(vm.events.systemShutdown);

    i = 0;
    for (; i < vm.config.number_of_system_threads; ++i) {
        WaitForSingleObject(vm.events.systemThreadHandles[i], INFINITE);
    }
    ResetEvent(vm.events.systemShutdown);

    end = GetTickCount64();
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    VirtualFree(vm.va.start, 0, MEM_RELEASE);

    printf("Elapsed time: %llu ms\n", end - start);

    printf("StandBy length %llu  \n", vm.lists.standby.length);
    printf("Modified length %llu \n", vm.lists.modified.length);
    printf("Free length %llu \n", vm.lists.free.length);
    printf("Active length %llu \n", vm.pfn.numActivePages);

    printf("pagewaits %llu \n",   vm.misc.pageWaits);
    printf("total time waiting %llu ticks\n",   (vm.misc.totalTimeWaiting));


    printf("num physical: %llu \n", vm.config.number_of_physical_pages* PAGE_SIZE/GB(1));
    printf("num virtual: %llu G\n", vm.config.virtual_address_size/GB(1));
    printf("num userthreads: %llu \n", vm.config.number_of_user_threads);
    printf("num freelists: %llu \n", vm.config.number_of_free_lists);


    printf("num active pages: %llu \n", vm.pfn.numActivePages);

    double totalHardFaults = (double) vm.misc.pagesFromFree + vm.misc.pagesFromLocalCache + vm.misc.pagesFromStandBy;
    double totalFaults = vm.misc.numRescues + totalHardFaults;

    printf("num faults %.0f \n", totalFaults);
    printf("num hard faults %.0f \n", totalHardFaults);

    printf("rescue percentage: %.2f%% \n", ((vm.misc.numRescues/totalFaults)*100));
    printf("hard fault percentage: %.2f%% \n", ((totalHardFaults/totalFaults)*100));
    printf("Percentages of hard faults:\n");

    printf("num freeList: %.2f%% \n", vm.misc.pagesFromFree/totalHardFaults*100);
    printf("num localCache: %.2f%% \n", vm.misc.pagesFromLocalCache/totalHardFaults*100);
    printf("num standBy: %.2f%% \n", vm.misc.pagesFromStandBy/totalHardFaults*100);

    return;
}


DWORD testVM(LPVOID lpParam) {

    WaitForSingleObject(vm.events.userStart, INFINITE);


   // DebugBreak();

    PULONG_PTR arbitrary_va;
    ULONG64 random_number;
    unsigned i;
    PTHREAD_INFO thread_info;

    BOOL page_faulted;
    BOOL redo_fault;
    BOOL redo_try_same_address;
    ULONG64 counter;

    i = 1;
    thread_info = (PTHREAD_INFO)lpParam;
    arbitrary_va = NULL;
    redo_try_same_address = FALSE;

    InitializeThreadRNG(&thread_info->rng);
      // Now perform random accesses

#if DBG || spinEvents
    while (TRUE) {
        
#else

//MB(1)/NUMBER_OF_USER_THREADS
 for (; i < MB(5); i++) {
//while (TRUE) {
        #endif


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



            if (redo_try_same_address == FALSE) {
                random_number = GetNextRandom(&thread_info->rng);
#if 0
                random_number += KB(256)*(thread_info->ThreadNumber + 2);
                random_number <<= 18;
                random_number |= (rand() & (KB(256) -1));
#else
            //    random_number += KB(256)*(thread_info->ThreadNumber + 2);
#endif

                random_number %= vm.config.virtual_address_size_in_unsigned_chunks;

                // Write the virtual address into each page. If we need to
                // debug anything, we'll be able to see these in the pages.


                // Ensure the write to the arbitrary virtual address doesn't
                // straddle a PAGE_SIZE boundary just to keep things simple for now.
                random_number &= ~0x7;
                arbitrary_va = vm.va.start + random_number;
               // printf("arbitrary_va %p\n", arbitrary_va);

            }
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

            }
            redo_try_same_address = TRUE;
            i--;

        } else {

            setAccessBit(arbitrary_va);
            redo_try_same_address = FALSE;



#if 0
    if (i % KB(512) == 0) {
        printf(".");
    }
#endif
        }
    i++;
}

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    return 0;
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
    memset (&vm, 0, sizeof(vm));
    //calls get physical pages, because his parameters might change

    if (argc > 1) {
        if (argc != 5) {
            printf("should be of format -> vm_debug [numUserThreads] [vaSizeInGigs] [paSizeInGigs]  [numFreeLists]");
            exit(1);
        }

        ULONG64 userThreads = (ULONG64) atoi(argv[1]);
        ULONG64 vaSizeInGigs = (ULONG64) atoi(argv[2]);
        ULONG64 paSizeInGigs = (ULONG64) atoi(argv[3]);
        ULONG64 numFreeLists = (ULONG64) atoi(argv[4]);


        init_config_params(userThreads, vaSizeInGigs, paSizeInGigs, numFreeLists);
    } else {
        init_base_config();
    }
    printf("%llu ",sizeof(pfn));
#if DBG
    printf("%llu ",sizeof(debugPFN));
#endif




    full_virtual_memory_test();

}


