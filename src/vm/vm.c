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


VOID
full_virtual_memory_test(VOID) {




    ULONG64 start, end;


    CRITICAL_SECTION cs;
    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
  

    init_virtual_memory();


    start = GetTickCount64();

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
    printf("Active length %llu \n", vm.lists.active.length);

    printf("pagewaits %llu \n",   vm.misc.pageWaits);
    printf("total time waiting %llu ticks\n",   (vm.misc.totalTimeWaiting));


    printf("num physical: %llu \n", vm.config.number_of_physical_pages* PAGE_SIZE/GB(1));
    printf("num virtual: %llu G\n", vm.config.virtual_address_size/GB(1));
    printf("num userthreads: %llu \n", vm.config.number_of_user_threads);
    printf("num freelists: %llu \n", vm.config.number_of_free_lists);



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

    i = 0;
    thread_info = (PTHREAD_INFO)lpParam;
    arbitrary_va = NULL;
    redo_try_same_address = FALSE;

    InitializeThreadRNG(&thread_info->rng);
      // Now perform random accesses

#if DBG || spinEvents
    while (TRUE) {
        
#else

//MB(1)/NUMBER_OF_USER_THREADS
 for (; i < MB(1)/8; i++) {
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
           // *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } else {
           //recordAccess(arbitrary_va);
            redo_try_same_address = FALSE;
        }

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




    full_virtual_memory_test();


}


