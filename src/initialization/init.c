//
// Created by nrper on 6/16/2025.
//


#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/initialization/init.h"

#include <math.h>

#include "../../include/variables/macros.h"
#include "../../include/initialization/init_threads.h"

#include <stdio.h>
#include <stdlib.h>


#pragma comment(lib, "advapi32.lib")


#pragma comment(lib, "onecore.lib")
//TODO batch commit the pages. First sort the array. Then, see if the next pfn will reside in the same page.
//TODO Switch to Virtual alloc (Reserve | Commit) for the pagefile


state vm;


// makes life easy
ULONG64 power(ULONG64 base, ULONG64 power) {

    if (power == 0) return 1;

    ULONG64 start = base;

    for (ULONG64 i = 1; i < power; i++) {
        start *= base;
    }
    return start;
}

VOID init_config_params(ULONG64 number_of_user_threads, ULONG64 num_layers, ULONG64 physicalInGigs,
                        ULONG64 numFreeLists, ULONG64 diskInGigs) {


    vm.config.number_of_page_table_layers = num_layers;
    vm.config.pte_entries_per_pagetable = PAGE_SIZE / sizeof(pte);


    // this is only the reservable space.
    vm.config.number_of_leaf_ptes  = power(vm.config.pte_entries_per_pagetable, num_layers);
    vm.config.virtual_address_size = vm.config.number_of_leaf_ptes * PAGE_SIZE;

    // this is in pagetables: layer k holds 512^k pagetables, k = 0 .. num_layers-1
    vm.config.cumulative_number_of_page_tables = 0;
    for (ULONG i = 0; i < num_layers; i++) {
        vm.config.cumulative_number_of_page_tables += power(vm.config.pte_entries_per_pagetable, i);
    }

    vm.config.number_of_leaf_pagetables = vm.config.number_of_leaf_ptes / vm.config.pte_entries_per_pagetable;

    // we might not get all our pages, but that is okay. We just have to make sure nothing is conditioned upon stale values
    vm.config.number_of_physical_pages = GB(physicalInGigs) / PAGE_SIZE;
    getPhysicalPages();

    // disk stuff
    vm.config.number_of_disk_divisions = 1;
    vm.config.disk_size_in_bytes = GB(diskInGigs);
    vm.config.disk_size_in_pages = vm.config.disk_size_in_bytes / PAGE_SIZE;
    vm.config.disk_division_size_in_pages = vm.config.disk_size_in_pages / vm.config.number_of_disk_divisions;

    // amount actually pageable (commitable): physical + disk, minus the zero slot and transfer
    // page, minus every possible page table frame. Page tables are physical-backed (never on
    // disk) and are not commitable data; cumulative_number_of_page_tables already counts the
    // pinned root, so we reserve for the whole tree here.
    if (vm.config.cumulative_number_of_page_tables + 2 >=
        vm.config.number_of_physical_pages + vm.config.disk_size_in_pages) {
        printf("Not enough physical+disk to back the page tables; reduce layers or add memory/disk\n");
        DebugBreak();
    }
    vm.config.max_commit_size_in_pages = vm.config.number_of_physical_pages + vm.config.disk_size_in_pages
                                         - 2 - vm.config.cumulative_number_of_page_tables;

    // Confine the access pattern to the committed (backable) space, so a large VA (e.g. 3 layers
    // = 512 GB) never overcommits. Clamp to the real VA (number_of_leaf_ptes = VA in pages) so a
    // fully-backable config (commit >= VA, e.g. the L=2 default) doesn't index past vm.va.end.
    ULONG64 accessible_pages = min(vm.config.max_commit_size_in_pages, vm.config.number_of_leaf_ptes);
    vm.config.virtual_address_size_in_unsigned_chunks = accessible_pages * (PAGE_SIZE / sizeof(ULONG64));

    //threads
    vm.config.number_of_user_threads = number_of_user_threads;
    vm.config.number_of_trimming_threads = 1;
    vm.config.number_of_writing_threads = 1;
    vm.config.number_of_aging_threads = 1;
    vm.config.number_of_scheduler_threads = 1;

    vm.config.number_of_threads = vm.config.number_of_user_threads +
                                  vm.config.number_of_trimming_threads +
                                  vm.config.number_of_writing_threads +
                                  vm.config.number_of_aging_threads +
                                  vm.config.number_of_scheduler_threads;
    vm.config.number_of_system_threads = vm.config.number_of_threads - vm.config.number_of_user_threads;

    //misc
    vm.config.size_of_user_thread_transfer_va_space_in_pages = 128;
    vm.config.stand_by_trim_threshold = vm.config.number_of_physical_pages * 7 / 8;
    vm.config.number_of_pages_to_trim_from_stand_by = vm.config.number_of_physical_pages / 8;
    vm.config.number_of_free_lists = numFreeLists;
    vm.config.time_until_recall_pages = 2500000;

    memset(&vm.config.parameter, 0, sizeof(vm.config.parameter));

    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section created above
    vm.config.parameter.Type = MemExtendedParameterUserPhysicalHandle;
    vm.config.parameter.Handle = vm.events.physical_page_handle;
}

BOOL
GetPrivilege(VOID) {
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege[1];
    } Info;

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    // Open the token
    hProcess = GetCurrentProcess();
    Result = OpenProcessToken(hProcess, TOKEN_ADJUST_PRIVILEGES, &Token);

    if (Result == FALSE) {
        printf("Cannot open process token.\n");
        return FALSE;
    }

    // Enable the privilege
    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get the LUID
    Result = LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf("Cannot get privilege\n");
        return FALSE;
    }

    // Adjust the privilege
    Result = AdjustTokenPrivileges(Token, FALSE, (PTOKEN_PRIVILEGES) &Info, 0, NULL, NULL);

    // Check the result
    if (Result == FALSE) {
        printf("Cannot adjust token privileges %u\n", GetLastError());
        return FALSE;
    }

    if (GetLastError() != ERROR_SUCCESS) {
        printf("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle(Token);
    return TRUE;
}


HANDLE
CreateSharedMemorySection(VOID) {
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = {0};

    // Create an AWE section. Later we deposit pages into it and/or return them.
    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2(INVALID_HANDLE_VALUE,
                                 NULL,
                                 SECTION_MAP_READ | SECTION_MAP_WRITE,
                                 PAGE_READWRITE,
                                 SEC_RESERVE,
                                 0,
                                 NULL,
                                 &parameter,
                                 1);

    return section;
}


VOID
init_virtual_memory(VOID) {
    initVA();

    init_pfns();

    init_pageTable();

    init_disk();

    initThreads();
}

VOID initAgeList(VOID) {
    for (int i = 0; i < NUMBER_OF_AGES; ++i) {
        init_list_head(&vm.pte.ageList[i]);
    }
}

VOID init_pte_regions(VOID) {
    initAgeList();

    // one region per pagetable across every layer: branch and leaf regions are uniform,
    // so the ager/trimmer treat them identically. Indexed by whole-table pagetable offset
    // (see getPTERegion: region - vm.pte.table).
    vm.pte.regions_base = (PTE_REGION *) init_memory(sizeof(PTE_REGION) * vm.config.cumulative_number_of_page_tables);
    vm.pte.globalNumOfAge[0].count = 0;

    PTE_REGION *currentRegion = vm.pte.regions_base;

}

VOID init_pageTable(VOID) {

    InitializeCriticalSection(&vm.pte.rootLock);

    ULONG64 numBytes;
    // Initialize the page table
    numBytes = vm.config.cumulative_number_of_page_tables * sizeof(PAGETABLE);
    vm.pte.table = (PPAGETABLE) VirtualAlloc2(NULL,
                                         NULL,
                                         numBytes,
                                         MEM_RESERVE | MEM_PHYSICAL,
                                         PAGE_READWRITE,
                                         &vm.config.parameter,
                                         1);

    if (vm.pte.table == NULL) {
        printf("Cannot allocate page table\n");
        DebugBreak();
    }

    // store the pointers to the start of each layer for easy pointer math.
    // layer k holds 512^k pagetables, so layer i begins after the sum of all
    // lower layers: start_of_layer[i] = table + (512^0 + ... + 512^(i-1)).
    vm.pte.start_of_layer = malloc(vm.config.number_of_page_table_layers * sizeof(PPAGETABLE));
    ULONG64 pagetableOffset = 0;
    for (int i = 0; i < vm.config.number_of_page_table_layers; ++i) {
        vm.pte.start_of_layer[i] = vm.pte.table + pagetableOffset;
        pagetableOffset += power(vm.config.pte_entries_per_pagetable, i);
    }

    // Pin the root page directory to the reserved frame 0 (withheld in init_free_list) so the
    // root pte read at the top of every fault is always backed and never itself faults.
    ULONG_PTR rootFrame = vm.pfn.physical_page_numbers[0];
    if (MapUserPhysicalPages(vm.pte.table, 1, &rootFrame) == FALSE) {
        printf("Failed to map root page directory\n");
        DebugBreak();
    }
    // start its ptes invalid
    memset(vm.pte.table, 0, sizeof(PAGETABLE));

    init_pte_regions();
#if DBG

    vm.pte.debugBuffer = init_memory(sizeof(debugPTE) * DEBUG_PTE_CIRCULAR_BUFFER_SIZE);

#endif
}

VOID init_disk(VOID) {
    ULONG64 numBytes;
    // Initialize disk structures
    numBytes = vm.config.disk_size_in_bytes;
    vm.disk.start = init_memory(numBytes);


    init_disk_active();
    init_num_open_slots();
}

VOID init_disk_active(VOID) {
    ULONG64 numEntries;


    // depending on our disk size check if our bits will go in evenly
    if (vm.config.disk_size_in_pages % 64) {
        numEntries = vm.config.disk_size_in_pages / 64 + 1;
    } else {
        numEntries = vm.config.disk_size_in_pages / 64;
    }


    vm.disk.active = (PULONG64) init_memory(numEntries * sizeof(ULONG64));

    //make the first slot in valid
    vm.disk.active[0] = DISK_ACTIVE;

    //makes it so that the out of bounds portion that exists in our diskMeta data is never accessed

    if (vm.config.disk_division_size_in_pages % 64 != 0) {
        ULONG64 number_of_usable_bits;
        ULONG64 bitMask;
        bitMask = MAXULONG64;

        number_of_usable_bits = (vm.config.disk_division_size_in_pages % 64) - 1;

        bitMask &= (1 << (1 + number_of_usable_bits)) - 1;


        vm.disk.active[numEntries - 1] = ~bitMask;
    }


    vm.disk.activeEnd = (PULONG64) ((ULONG64) vm.disk.active + numEntries);


    vm.disk.activeVa = init_memory(vm.config.disk_size_in_pages * sizeof(pte *));
}


VOID init_num_open_slots(VOID) {
    ULONG64 numBytes;

    numBytes = sizeof(ULONG64) * vm.config.number_of_disk_divisions;
    vm.disk.number_of_open_slots = (ULONG64 *) malloc(numBytes);

    if (vm.disk.number_of_open_slots == NULL) {
        printf("Failed to allocate memory for number_of_open_slots\n");
        return;
    }

    // if you switch back to more regions, change this back
    for (int i = 0; i < vm.config.number_of_disk_divisions; ++i) {
        vm.disk.number_of_open_slots[i] = vm.config.disk_division_size_in_pages - 1;
    }
    // number_of_open_slots[NUMBER_OF_DISK_DIVISIONS - 1] += 2;
    // number_of_open_slots[0] -= 1;
}


VOID init_pfns(VOID) {
    ULONG64 max = getMaxFrameNumber();
    max += 1;
    vm.pfn.start = VirtualAlloc(NULL, sizeof(pfn) * max,MEM_RESERVE,PAGE_READWRITE);

    if (vm.pfn.start == NULL) {
        printf("Failed to reserve memory for PFN database\n");
        return;
    }
    vm.pfn.end = vm.pfn.start + max;

    init_lists();

    init_free_list();

#if DBG
    vm.pfn.debugBuffer = init_memory(sizeof(debugPFN) * DEBUG_PFN_CIRCULAR_BUFFER_SIZE);
#endif
}

VOID init_free_list(VOID) {
    vm.lists.free.heads = (pListHead) init_memory(vm.config.number_of_free_lists * sizeof(listHead));
    for (int i = 0; i < vm.config.number_of_free_lists; ++i) {
        init_list_head(&vm.lists.free.heads[i]);
    }

    // frame 0 (physical_page_numbers[0]) is reserved for the root page directory so it is always
    // resident and the first pte read in every fault never itself faults. It is pinned in
    // init_pageTable; the free list gets every other frame.
    vm.lists.free.length = vm.pfn.physical_page_count - 1;

    // Add every page except the reserved root frame to the free list
    for (int i = 1; i < vm.pfn.physical_page_count; ++i) {
        pfn *new_pfn = (pfn *) (vm.pfn.start + vm.pfn.physical_page_numbers[i]);

        // Calculate the page-aligned range that contains this pfn structure
        PVOID startPage = (PVOID) ROUND_DOWN_TO_PAGE(new_pfn);
        PVOID endPage = (PVOID) ROUND_UP_TO_PAGE((ULONG_PTR)new_pfn + sizeof(pfn));
        SIZE_T commitSize = (ULONG_PTR) endPage - (ULONG_PTR) startPage;

        // Commit the full page(s) that contain this pfn structure
        if (VirtualAlloc(startPage, commitSize, MEM_COMMIT, PAGE_READWRITE) != startPage) {
            printf("Failed to commit at expected address\n");
            exit(1);
        }

        // Now initialize the pfn structure
        memset(new_pfn, 0, sizeof(pfn));

        InitializeCriticalSection(&new_pfn->lock);
        InsertTailList(&(vm.lists.free.heads[i % vm.config.number_of_free_lists]), &new_pfn->entry);
    }
}

VOID init_lists(VOID) {
    //init_list_head(&headToBeZeroedList);
    init_list_head(&vm.lists.standby);
    init_list_head(&vm.lists.modified);


    vm.misc.standByPruningInProgress = false;
}

VOID init_list_head(pListHead head) {
    head->entry.Flink = &head->entry;
    head->entry.Blink = &head->entry;
    head->length = 0;

    InitializeSRWLock(&head->sharedLock.sharedLock);

    InitializeCriticalSection(&head->page.lock);


#if DBG
    head->sharedLock.numHeldShared = 0;
    head->sharedLock.threadId = -1;
    InitializeListHead(&head->sharedLock.sharedHolders);
    InitializeCriticalSection(&head->sharedLock.debugLock);
#endif
}

BOOL getPhysicalPages(VOID) {
    BOOL allocated;
    BOOL privilege;

    // Allocate the physical pages that we will be managing.
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf("full_virtual_memory_test : could not get privilege\n");
        return FALSE;
    }


    vm.events.physical_page_handle = CreateSharedMemorySection();

    if (vm.events.physical_page_handle == NULL) {
        printf("CreateFileMapping2 failed, error %#x\n", GetLastError());
        return FALSE;
    }

    vm.pfn.physical_page_count = vm.config.number_of_physical_pages;
    vm.pfn.physical_page_numbers = (PULONG_PTR) malloc(vm.pfn.physical_page_count * sizeof(ULONG_PTR));

    if (vm.pfn.physical_page_numbers == NULL) {
        printf("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return FALSE;
    }

    allocated = AllocateUserPhysicalPages(vm.events.physical_page_handle,
                                          &vm.pfn.physical_page_count,
                                          vm.pfn.physical_page_numbers);

    if (allocated == FALSE) {
        printf("full_virtual_memory_test : could not allocate physical pages\n");
        return FALSE;
    }

    if (vm.pfn.physical_page_count != vm.config.number_of_physical_pages) {
        printf("full_virtual_memory_test : allocated only %llu pages out of %llu pages requested\n",
               vm.pfn.physical_page_count,
               vm.config.number_of_physical_pages);

        vm.config.number_of_physical_pages = vm.pfn.physical_page_count;
    }

    return TRUE;
}


BOOL initVA() {


    vm.va.start = (PULONG_PTR) VirtualAlloc2(NULL,
                                             NULL,
                                             vm.config.virtual_address_size,
                                             MEM_RESERVE | MEM_PHYSICAL,
                                             PAGE_READWRITE,
                                             &vm.config.parameter,
                                             1);
    if (vm.va.start == NULL) {
        printf("Failed to allocate virtual address space: size %I64x \n ", vm.config.virtual_address_size);
        DebugBreak();
        exit(1);
    }

    vm.va.end = (PULONG64) ((ULONG64) vm.va.start + vm.config.virtual_address_size);

    vm.va.writing = (PULONG_PTR) VirtualAlloc2(NULL,
                                               NULL,
                                               PAGE_SIZE * BATCH_SIZE * NUM_WRITING_BATCHES,
                                               MEM_RESERVE | MEM_PHYSICAL,
                                               PAGE_READWRITE,
                                               &vm.config.parameter,
                                               1);
    if (vm.va.writing == NULL) {
        printf("Failed to allocate transfer VA for writing\n");
        return FALSE;
    }


    vm.va.userThreadTransfer = init_memory(sizeof(PVOID) * vm.config.number_of_user_threads);
    for (int i = 0; i < vm.config.number_of_user_threads; ++i) {
        vm.va.userThreadTransfer[i] = (PULONG_PTR) VirtualAlloc2(NULL,
                                                                 NULL,
                                                                 vm.config.
                                                                 size_of_user_thread_transfer_va_space_in_pages *
                                                                 PAGE_SIZE,
                                                                 MEM_RESERVE | MEM_PHYSICAL,
                                                                 PAGE_READWRITE,
                                                                 &vm.config.parameter,
                                                                 1);
        if (vm.va.userThreadTransfer[i] == NULL) {
            printf("Failed to allocate user thread transfer VA %d\n", i);
            return FALSE;
        }
    }


    return TRUE;
}


ULONG64 getMaxFrameNumber(VOID) {
    ULONG64 maxFrameNumber;
    maxFrameNumber = 0;

    ULONG64 i;

    for (i = 0; i < vm.config.number_of_physical_pages; ++i) {
        maxFrameNumber = max(maxFrameNumber, vm.pfn.physical_page_numbers[i]);
    }
    return maxFrameNumber;
}

PVOID init_memory(ULONG64 numBytes) {
    PVOID new;
    new = malloc(numBytes);

    if (new == NULL) {
        printf("malloc failed\n");
        DebugBreak();
        exit(1);
    }

    memset(new, 0, numBytes);
    return new;
}

VOID delete_list_head(pListHead head) {
    // the SRWLOCK needs no teardown
    DeleteCriticalSection(&head->page.lock);

#if DBG
    DeleteCriticalSection(&head->sharedLock.debugLock);
#endif
}

/**
 * @brief Tears down everything init_virtual_memory built, in reverse order.
 *
 * Only safe once every thread has exited - nothing here checks for live users, and deleting a
 * critical section someone still holds is undefined. Call it after printStats, which still reads
 * vm.threadInfo and the list lengths.
 */
VOID cleanup_virtual_memory(VOID) {
    // threads: the handles, their contexts, and the per-user local list locks
    for (ULONG64 i = 0; i < vm.config.number_of_user_threads; ++i) {
        CloseHandle(vm.events.userThreadHandles[i]);
        delete_list_head(&vm.threadInfo.user[i].localList);
    }
    for (ULONG64 i = 0; i < vm.config.number_of_system_threads; ++i) {
        CloseHandle(vm.events.systemThreadHandles[i]);
    }
    free(vm.events.userThreadHandles);
    free(vm.events.systemThreadHandles);
    free(vm.threadInfo.user);
    free(vm.threadInfo.trimmer);
    free(vm.threadInfo.writer);
    free(vm.threadInfo.aging);
    free(vm.threadInfo.scheduler);

    CloseHandle(vm.events.userStart);
    CloseHandle(vm.events.writingStart);
    CloseHandle(vm.events.trimmingStart);
    CloseHandle(vm.events.writingEnd);
    CloseHandle(vm.events.agerStart);
    CloseHandle(vm.events.systemShutdown);

    // page lists. vm.lists.zeroed is never initialized (see init_lists), so it is not torn down.
    delete_list_head(&vm.lists.standby);
    delete_list_head(&vm.lists.modified);
    for (ULONG64 i = 0; i < vm.config.number_of_free_lists; ++i) {
        delete_list_head(&vm.lists.free.heads[i]);
    }
    free(vm.lists.free.heads);

    // pte regions and the age lists they hang off
    for (int i = 0; i < NUMBER_OF_AGES; ++i) {
        delete_list_head(&vm.pte.ageList[i]);
    }

    free(vm.pte.regions_base);
    free(vm.pte.start_of_layer);
    DeleteCriticalSection(&vm.pte.rootLock);

    // disk
    free(vm.disk.number_of_open_slots);
    free(vm.disk.activeVa);
    free(vm.disk.active);
    free(vm.disk.start);

    // the pfn locks live inside the pfn database, so they must go before it is released. Frame 0 is
    // the pinned root page directory - init_free_list never gave it a lock.
    for (ULONG64 i = 1; i < vm.pfn.physical_page_count; ++i) {
        DeleteCriticalSection(&(vm.pfn.start + vm.pfn.physical_page_numbers[i])->lock);
    }

    // Releasing the MEM_PHYSICAL regions unmaps their frames, which FreeUserPhysicalPages requires.
    for (ULONG64 i = 0; i < vm.config.number_of_user_threads; ++i) {
        VirtualFree(vm.va.userThreadTransfer[i], 0, MEM_RELEASE);
    }
    free(vm.va.userThreadTransfer);
    VirtualFree(vm.va.writing, 0, MEM_RELEASE);
    VirtualFree(vm.va.start, 0, MEM_RELEASE);
    VirtualFree(vm.pte.table, 0, MEM_RELEASE);
    VirtualFree(vm.pfn.start, 0, MEM_RELEASE);

    FreeUserPhysicalPages(vm.events.physical_page_handle,
                          &vm.pfn.physical_page_count,
                          vm.pfn.physical_page_numbers);
    free(vm.pfn.physical_page_numbers);
    CloseHandle(vm.events.physical_page_handle);

#if DBG
    free(vm.pte.debugBuffer);
    free(vm.pfn.debugBuffer);
#endif
}
