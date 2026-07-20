#include "../../include/initialization/init.h"

#include "../../include/variables/globals.h"
#include "../../include/variables/structures.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/disk/disk.h"
#include <stdio.h>
#include <wchar.h>
#include "vm.h"
#include "../../include/variables/macros.h"
#include "threads/user_thread.h"
#include "utils/random_utils.h"
#include "utils/statistics_utils.h"

// Timestamp counter for random number generation
static __inline unsigned __int64 GetTimeStampCounter(void) {
    return __rdtsc();
}

void do_work_to_slow_consumption(PTHREAD_INFO threadInfo) {
    int WORK_TIME = 10;
    ULONG64 data = 19941994;
    for (int j = 0; j < WORK_TIME; ++j) {
        YieldProcessor();
        GetNextRandom(&threadInfo->rng);
    }
}

/**
 * @brief Gets the next va based on an algorithm that decides if it should access sequentially or randomly
 * @param thread_info The thread info of the caller. Used to look at the rng seed
 * @return A pointer at the arbitrary va to try next
 */
PULONG_PTR get_arbitrary_va(PTHREAD_INFO thread_info) {
    ULONG64 random_number;

    random_number = GetNextRandom(&thread_info->rng);

    random_number %= vm.config.virtual_address_size_in_unsigned_chunks;


    random_number &= ~0x7;
    return vm.va.start + random_number;
}


#define PAGE_JUMP_PROBABILITY .1

/**
 * @brief Gets the next va based on an algorithm that decides if it should access sequentially or randomly
 * @param thread_info The thread info of the caller. Used to look at the rng seed
 * @param previous_va The previous VA accessed
 * @return A pointer at the arbitrary va to try next
 */
PULONG_PTR get_next_va(PULONG_PTR previous_va, PTHREAD_INFO thread_info) {
    // Generate the next VA
    PULONG_PTR new_va = (previous_va + 1);
    // wrap at the committed window (not vm.va.end) so the sequential walk stays backable too
    if (new_va >= vm.va.start + vm.config.virtual_address_size_in_unsigned_chunks)
        new_va = vm.va.start;

    // If we are still within a given page, return this VA and continue on.
    if ((ULONG64) new_va % PAGE_SIZE != 0) return new_va;

    // Otherwise, we will move on to the next page with a certain probability p
    // And we will jump to a random page with a probability (1-p)

    double p = (double) (GetNextRandom(&thread_info->rng) >> 11) * (1.0 / 9007199254740992.0);
    if (p < PAGE_JUMP_PROBABILITY)
        new_va = get_arbitrary_va(thread_info);

    return new_va;
}


/**
 * @brief This function mimics the piece of hardware that sets access bits in the cpu. It is not guaranteed to work, but it will not trample on others' work
 * @param va The VA to set the access bit of its corresponding PTE
 * @return Whether the write succeeded or not
 */
boolean setAccessBit(ULONG64 va) {
    pte *pteAddress;
    pte newPTE;
    pte oldPTE;
    ULONG64 row;

   for (int i = 0; i < vm.config.number_of_page_table_layers; ++i) {
       row = parseVA_Row_value(va, i);
       if (i == 0) {
           // root pte: entry row0 of the single layer-0 pagetable
           pteAddress = (pte *) vm.pte.start_of_layer[0] + row;
       } else {
           // descend from the layer i-1 parent into layer i
           pteAddress = getNextLayer(pteAddress, i - 1, row);
       }

       while (true) {
           __try {
               oldPTE.entireFormat = ReadULong64NoFence((volatile ULONG64 *) pteAddress);
               if (oldPTE.validFormat.valid == 0) {
                   return REDO_FAULT;
               }
               newPTE.entireFormat = oldPTE.entireFormat;

               newPTE.validFormat.access = 1;

               // retry only if the compare-exchange lost a race; break once it lands
               pte prev = writePTE(pteAddress, newPTE, oldPTE);
               if (prev.entireFormat == oldPTE.entireFormat) {
                   break;
               }
           } __except(EXCEPTION_EXECUTE_HANDLER) {
               printf("?");
               break;
           }

       }
   }

    return 0;
}


/**
 * @brief This is the main orchestrating thread that initializes the simulation, waits for all threads to exit, and prints final statistics
 */
VOID
full_virtual_memory_test(VOID) {
    ULONG64 start, end;

    init_virtual_memory();


    start = GetTickCount64();

    printf("initialization done \n");

    SetEvent(vm.events.userStart);

    int i = 0;


    // wait for users to end
    for (; i < vm.config.number_of_user_threads; ++i) {
        WaitForSingleObject(vm.events.userThreadHandles[i], INFINITE);
    }

    // once users are over, shut down system threads
    SetEvent(vm.events.systemShutdown);
    i = 0;
    for (; i < vm.config.number_of_system_threads; ++i) {
        WaitForSingleObject(vm.events.systemThreadHandles[i], INFINITE);
    }

    ResetEvent(vm.events.systemShutdown);

    end = GetTickCount64();
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //TODO free everything else
    VirtualFree(vm.va.start, 0, MEM_RELEASE);

    printf("Elapsed time: %llu ms\n", end - start);

    printf("StandBy length %llu  \n", vm.lists.standby.length);
    printf("Modified length %llu \n", vm.lists.modified.length);
    printf("Free length %llu \n", vm.lists.free.length);
    printf("Active length %llu \n", vm.pfn.numActivePages);

    printf("pagewaits %llu \n", vm.misc.pageWaits);
    printf("total time waiting %llu ticks\n", (vm.misc.totalTimeWaiting));


    printf("num physical: %llu \n", vm.config.number_of_physical_pages * PAGE_SIZE / GB(1));
    printf("num virtual: %llu G\n", vm.config.virtual_address_size / GB(1));
    printf("num userthreads: %llu \n", vm.config.number_of_user_threads);
    printf("num freelists: %llu \n", vm.config.number_of_free_lists);


    printf("num active pages: %llu \n", vm.pfn.numActivePages);

    double totalHardFaults = (double) vm.misc.pagesFromFree + vm.misc.pagesFromLocalCache + vm.misc.pagesFromStandBy;
    double totalFaults = vm.misc.numRescues + totalHardFaults;

    printf("num faults %.0f \n", totalFaults);
    printf("num hard faults %.0f \n", totalHardFaults);

    printf("rescue percentage: %.2f%% \n", ((vm.misc.numRescues / totalFaults) * 100));
    printf("hard fault percentage: %.2f%% \n", ((totalHardFaults / totalFaults) * 100));
    printf("Percentages of hard faults:\n");

    printf("num freeList: %.2f%% \n", vm.misc.pagesFromFree / totalHardFaults * 100);
    printf("num localCache: %.2f%% \n", vm.misc.pagesFromLocalCache / totalHardFaults * 100);
    printf("num standBy: %.2f%% \n", vm.misc.pagesFromStandBy / totalHardFaults * 100);

    return;
}

/**
 * @brief The function which represents one user thread accessing the shared VA space.
 * @param lpParam The thread info passed in
 * @return
 */
DWORD testVM(LPVOID lpParam) {
    WaitForSingleObject(vm.events.userStart, INFINITE);


    // DebugBreak();

    PULONG_PTR arbitrary_va;
    unsigned i;
    PTHREAD_INFO thread_info;

    BOOL page_faulted;
    BOOL redo_fault;
    BOOL redo_try_same_address;
    ULONG64 counter;

    i = 1;
    thread_info = (PTHREAD_INFO) lpParam;
    arbitrary_va = NULL;
    redo_try_same_address = FALSE;

    WCHAR threadName[16];
    swprintf(threadName, ARRAYSIZE(threadName), L"User_%lu", thread_info->ThreadNumber);
    SetThreadDescription(GetCurrentThread(), threadName);

    InitializeThreadRNG(&thread_info->rng);
    arbitrary_va = get_arbitrary_va(thread_info);
    // Now perform random accesses

    // Depending on if we are in debug/performance test mode, spin forever
#if DBG || spinEvents
    while (TRUE) {

#else

    //MB(1)/NUMBER_OF_USER_THREADS
    for (; i < MB(500); i++) {
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
            // Write the virtual address into each page. If we need to
            // debug anything, we'll be able to see these in the pages.
            // Ensure the write to the arbitrary virtual address doesn't
            // straddle a PAGE_SIZE boundary just to keep things simple for now.

            arbitrary_va = get_next_va(arbitrary_va, thread_info);
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
                redo_fault = pageFaultEntryPoint(arbitrary_va, lpParam);
            }
            redo_try_same_address = TRUE;
            i--;
        } else {
            setAccessBit((ULONG64) arbitrary_va);
            redo_try_same_address = FALSE;


#if 1 || DBG
            if (i % MB(1) == 0) {
                printf(".");
            }
#endif
        }
    }

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    return 0;
}


int main(int argc, char **argv) {
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
    memset(&vm, 0, sizeof(vm));

    if (argc > 1) {
        if (argc != 6) {
            printf("should be of format -> vm_debug [numUserThreads] [layers] [paSizeInGigs]  [numFreeLists] [diskInGigs]");
            exit(1);
        }

        ULONG64 userThreads = (ULONG64) atoi(argv[1]);
        ULONG64 num_layers = (ULONG64) atoi(argv[2]);
        ULONG64 paSizeInGigs = (ULONG64) atoi(argv[3]);
        ULONG64 numFreeLists = (ULONG64) atoi(argv[4]);
        ULONG64 diskSizeInGigs = (ULONG64) atoi(argv[5]);

        init_config_params(userThreads, num_layers, paSizeInGigs, numFreeLists, diskSizeInGigs);
    } else {
        // default: 8 user threads, 2 pte layers (1 GB VA), 1 GB physical, 16 free lists, 1 GB disk.
        // L=2 keeps VA (1 GB) fully backable by physical+disk, so uniform-random access stays
        // within commit. TODO: support n layers by default (L=3 = 512 GB VA) once the test uses a
        // locality-biased access pattern instead of uniform-random, so we don't outrun commit.
#if DBG
        init_config_params(8, 3, 1, 16, 1);

#else
        init_config_params(8, 2, 1, 16, 1);
#endif
    }
    printf("%llu ", sizeof(pfn));
#if DBG
    printf("%llu ", sizeof(debugPFN));
#endif


    full_virtual_memory_test();
}
