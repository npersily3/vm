//
// Created by nrper on 6/16/2025.
//


#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/initialization/init.h"
#include "../../include/variables/macros.h"
#include "../../include/initialization/init_threads.h"

#include <stdio.h>
#include <stdlib.h>


#pragma comment(lib, "advapi32.lib")




#pragma comment(lib, "onecore.lib")
//TODO batch commit the pages. First sort the array. Then, see if the next pfn will reside in the same page.
//TODO Switch to Virtual alloc (Reserve | Commit) for the pagefile



state vm;

VOID init_config_params(ULONG64 number_of_user_threads, ULONG64 vaSizeInGigs, ULONG64 physicalInGigs, ULONG64 numFreeLists) {
    vm.config.virtual_address_size = GB(vaSizeInGigs);
    vm.config.number_of_physical_pages = GB(physicalInGigs)/PAGE_SIZE;

    vm.config.virtual_address_size_in_unsigned_chunks = vm.config.virtual_address_size / sizeof(ULONG64);

    getPhysicalPages();

    vm.config.number_of_disk_divisions = 1;
    vm.config.disk_size_in_bytes = (vm.config.virtual_address_size - (PAGE_SIZE * vm.config.number_of_physical_pages) + (2 * PAGE_SIZE));
    vm.config.disk_size_in_pages = vm.config.disk_size_in_bytes / PAGE_SIZE;
    vm.config.disk_division_size_in_pages = vm.config.disk_size_in_pages / vm.config.number_of_disk_divisions;

    vm.config.number_of_user_threads = number_of_user_threads;
    vm.config.number_of_trimming_threads = 1;
    vm.config.number_of_writing_threads = 1;
    vm.config.number_of_threads = vm.config.number_of_user_threads + vm.config.number_of_trimming_threads + vm.config.number_of_writing_threads;
    vm.config.number_of_system_threads = vm.config.number_of_threads - vm.config.number_of_user_threads;

    vm.config.size_of_transfer_va_space_in_pages = 128;
    vm.config.stand_by_trim_threshold = vm.config.number_of_physical_pages / 2;
    vm.config.number_of_pages_to_trim_from_stand_by = vm.config.number_of_physical_pages / 8;


    vm.config.number_of_ptes = vm.config.virtual_address_size / PAGE_SIZE;
    vm.config.page_table_size_in_bytes = vm.config.number_of_ptes * sizeof(pte);

    vm.config.number_of_ptes_per_region = 64;
    vm.config.number_of_pte_regions = vm.config.number_of_ptes / vm.config.number_of_ptes_per_region;

    vm.config.number_of_free_lists = numFreeLists;
    vm.config.time_until_recall_pages = 2500000;
}


VOID init_base_config(VOID) {
#if DBG
    vm.config.virtual_address_size = 256 * PAGE_SIZE;
    vm.config.number_of_physical_pages = 128;
#else

    vm.config.virtual_address_size = GB(4);
    vm.config.number_of_physical_pages = GB(2)/PAGE_SIZE;
#endif
    vm.config.virtual_address_size_in_unsigned_chunks = vm.config.virtual_address_size / sizeof(ULONG64);

    getPhysicalPages();

    vm.config.number_of_disk_divisions = 1;
    vm.config.disk_size_in_bytes = (vm.config.virtual_address_size - (PAGE_SIZE * vm.config.number_of_physical_pages) + (2 * PAGE_SIZE));
    vm.config.disk_size_in_pages = vm.config.disk_size_in_bytes / PAGE_SIZE;
    vm.config.disk_division_size_in_pages = vm.config.disk_size_in_pages / vm.config.number_of_disk_divisions;

    vm.config.number_of_user_threads = 8;
    vm.config.number_of_trimming_threads = 1;
    vm.config.number_of_writing_threads = 1;
    vm.config.number_of_threads = vm.config.number_of_user_threads + vm.config.number_of_trimming_threads + vm.config.number_of_writing_threads;
    vm.config.number_of_system_threads = vm.config.number_of_threads - vm.config.number_of_user_threads;

    vm.config.size_of_transfer_va_space_in_pages = 128;
    vm.config.stand_by_trim_threshold = vm.config.number_of_physical_pages / 2;
    vm.config.number_of_pages_to_trim_from_stand_by = vm.config.number_of_physical_pages / 8;


    vm.config.number_of_ptes = vm.config.virtual_address_size / PAGE_SIZE;
    vm.config.page_table_size_in_bytes = vm.config.number_of_ptes * sizeof(pte);




    vm.config.number_of_ptes_per_region = 64;
    vm.config.number_of_pte_regions = vm.config.number_of_ptes / vm.config.number_of_ptes_per_region;
    vm.config.time_until_recall_pages = 2500000;
    vm.config.number_of_free_lists = 8;
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
    MEM_EXTENDED_PARAMETER parameter = { 0 };

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
VOID init_pte_regions(VOID) {

    //nptodo add the case where NUMPTES is not divisible by 64


    vm.pte.RegionsBase = (PTE_REGION*) init_memory(sizeof(PTE_REGION) * vm.config.number_of_pte_regions);




}

VOID init_pageTable(VOID) {
    ULONG64 numBytes;
    // Initialize the page table
    numBytes = vm.config.page_table_size_in_bytes ;
    vm.pte.table = (pte*)init_memory(numBytes);

    init_pte_regions();

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
if  (vm.config.disk_size_in_pages  % 64) {
    numEntries = vm.config.disk_size_in_pages  / 64 + 1;
} else {
    numEntries = vm.config.disk_size_in_pages / 64;
}




    vm.disk.active = (PULONG64)init_memory(numEntries * sizeof(ULONG64));

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


    vm.disk.activeEnd = (PULONG64)((ULONG64) vm.disk.active + numEntries);




    vm.disk.activeVa = init_memory(numEntries * sizeof(ULONG64));

}


VOID init_num_open_slots(VOID) {
    ULONG64 numBytes;

    numBytes = sizeof(ULONG64) * vm.config.number_of_disk_divisions;
    vm.disk.number_of_open_slots = (ULONG64*)malloc(numBytes);

    if (vm.disk.number_of_open_slots == NULL) {
        printf("Failed to allocate memory for number_of_open_slots\n");
        return;
    }

    // if you switch back to more regions, change this back
    for (int i = 0; i < vm.config.number_of_disk_divisions ; ++i) {
        vm.disk.number_of_open_slots[i] = vm.config.disk_division_size_in_pages - 1;
    }
    // number_of_open_slots[NUMBER_OF_DISK_DIVISIONS - 1] += 2;
    // number_of_open_slots[0] -= 1;
}



VOID init_pfns(VOID) {

    ULONG64 max = getMaxFrameNumber();
    max += 1;
    vm.pfn.start = VirtualAlloc(NULL,sizeof(pfn)*max,MEM_RESERVE,PAGE_READWRITE);

    if ( vm.pfn.start == NULL) {
        printf("Failed to reserve memory for PFN database\n");
        return;
    }
     vm.pfn.end =  vm.pfn.start + max;

    init_lists();

    init_free_list();


}
VOID init_free_list(VOID) {

    vm.lists.free.heads = (pListHead) init_memory(vm.config.number_of_free_lists * sizeof(listHead));
    for (int i = 0; i < vm.config.number_of_free_lists ; ++i) {

        init_list_head(&vm.lists.free.heads[i]);
    }
    vm.lists.free.AddIndex = 0;
    vm.lists.free.RemoveIndex = 0;
    vm.lists.free.length = vm.config.number_of_physical_pages ;

    // Add every page to the free list
    for (int i = 0; i < vm.pfn.physical_page_count; ++i) {


        pfn *new_pfn = (pfn*)(vm.pfn.start + vm.pfn.physical_page_numbers[i]);

        // Calculate the page-aligned range that contains this pfn structure
         PVOID startPage = (PVOID)ROUND_DOWN_TO_PAGE(new_pfn);
         PVOID endPage = (PVOID)ROUND_UP_TO_PAGE((ULONG_PTR)new_pfn + sizeof(pfn));
        SIZE_T commitSize = (ULONG_PTR)endPage - (ULONG_PTR)startPage;

        // Commit the full page(s) that contain this pfn structure
        if (VirtualAlloc(startPage, commitSize, MEM_COMMIT, PAGE_READWRITE) != startPage) {
            printf("Failed to commit at expected address\n");
            exit(1);
        }

        // Now initialize the pfn structure
        memset(new_pfn, 0, sizeof(pfn));

        InitializeCriticalSection(&new_pfn->lock);
        InsertTailList(&(vm.lists.free.heads[vm.lists.free.AddIndex]), &new_pfn->entry);


        vm.lists.free.AddIndex +=1;
        vm.lists.free.AddIndex %= vm.config.number_of_free_lists ;
    }
}
VOID init_lists(VOID) {

    //init_list_head(&headToBeZeroedList);
    init_list_head(&vm.lists.standby);
    init_list_head(&vm.lists.modified);
    init_list_head(&vm.lists.active);

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
BOOL getPhysicalPages (VOID) {

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

    vm.pfn.physical_page_count = vm.config.number_of_physical_pages ;
    vm.pfn.physical_page_numbers = (PULONG_PTR)malloc(vm.pfn.physical_page_count * sizeof(ULONG_PTR));

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

    if (vm.pfn.physical_page_count != vm.config.number_of_physical_pages ) {
        printf("full_virtual_memory_test : allocated only %llu pages out of %llu pages requested\n",
               vm.pfn.physical_page_count,
               vm.config.number_of_physical_pages );

        vm.config.number_of_physical_pages = vm.pfn.physical_page_count;

    }

    return TRUE;
}



BOOL initVA () {

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section created above
    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = vm.events.physical_page_handle;

    vm.va.start = (PULONG_PTR)VirtualAlloc2(NULL,
                                        NULL,
                                        vm.config.virtual_address_size ,
                                        MEM_RESERVE | MEM_PHYSICAL,
                                        PAGE_READWRITE,
                                        &parameter,
                                        1);
    if (vm.va.start == NULL) {
        printf("Failed to allocate virtual address space: size %I64x \n ", vm.config.virtual_address_size);
        DebugBreak();
        exit(1);
    }

    vm.va.end = (PULONG64) ((ULONG64) vm.va.start + vm.config.virtual_address_size);

    vm.va.writing = (PULONG_PTR)VirtualAlloc2(NULL,
                                        NULL,
                                        PAGE_SIZE*BATCH_SIZE,
                                        MEM_RESERVE | MEM_PHYSICAL,
                                        PAGE_READWRITE,
                                        &parameter,
                                        1);
    if (vm.va.writing == NULL) {
        printf("Failed to allocate transfer VA for writing\n");
        return FALSE;
    }


    vm.va.userThreadTransfer = init_memory(sizeof(PVOID) * vm.config.number_of_user_threads);
    for (int i = 0; i < vm.config.number_of_user_threads; ++i) {
        vm.va.userThreadTransfer[i] = (PULONG_PTR)VirtualAlloc2(NULL,
                                            NULL,
                                            vm.config.size_of_transfer_va_space_in_pages * PAGE_SIZE,
                                            MEM_RESERVE | MEM_PHYSICAL,
                                            PAGE_READWRITE,
                                            &parameter,
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

    for (i = 0; i < vm.config.number_of_physical_pages ; ++i) {
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

