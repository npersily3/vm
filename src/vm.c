#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

#define PAGE_SIZE                   4096

// 40 bit because highest PA is 52 bits so 52/ pagesize -> 2^12 = 2^40
#define frame_number_size       40

#define MB(x)                       ((x) * 1024 * 1024)


//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//



#define VIRTUAL_ADDRESS_SIZE        MB(16)

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)



// easier syntax for list adds and removes
#define REMOVE_FREE_PAGE FALSE
#define REMOVE_ACTIVE_PAGE TRUE


#define NUMBER_OF_DISK_DIVISIONS 8


// for 8 this is 0x1f8
#define DISK_DIVISION_SIZE ((VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES)/PAGE_SIZE)/NUMBER_OF_DISK_DIVISIONS;

BOOL
GetPrivilege(
    VOID) {
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege[1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess();

    Result = OpenProcessToken(hProcess,
                              TOKEN_ADJUST_PRIVILEGES,
                              &Token);

    if (Result == FALSE) {
        printf("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege.
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue(NULL,
                                  SE_LOCK_MEMORY_NAME,
                                  &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges(Token,
                                   FALSE,
                                   (PTOKEN_PRIVILEGES) &Info,
                                   0,
                                   NULL,
                                   NULL);

    //
    // Check the result.
    //

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

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

HANDLE
CreateSharedMemorySection (
    VOID
    )
{
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2 (INVALID_HANDLE_VALUE,
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

#endif


ULONG64 frameNumberSize;
typedef struct {
    ULONG64 valid: 1;
    ULONG64 frameNumber: frame_number_size;
} validPte;

//right now the diskIndex lenngth is the same as frame_number because it is nice to be parrallel
typedef struct {
    // otherwise would be other format
    ULONG64 mustBeZero: 1;
    ULONG64 diskIndex: frame_number_size;

} invalidPte;

typedef struct {
    // overlays
    union {
        validPte validFormat;
        invalidPte invalidFormat;
        ULONG64 entireFormat;
    };
} pte;


typedef struct {

    LIST_ENTRY entry;
    pte *pte;
    ULONG64 frameNumber;

} pfn;


LIST_ENTRY headFreeList;
LIST_ENTRY headActiveList;

pte *pagetable;
pfn *pfnStart;
PULONG_PTR vaStart;
PVOID diskStart;




VOID
listAdd(pfn* pfn, boolean active) {
    LIST_ENTRY* head;
    if (active) {
        head = &headActiveList;
    } else {

        head = &headFreeList;
    }

        pfn->entry.Flink = head;
        pfn->entry.Blink = head->Blink;
        head->Blink->Flink = &pfn->entry;
        head->Blink = &pfn->entry;
}

//make better
pfn* listRemove(boolean active) {
    LIST_ENTRY* head;
    if (active) {
        head = &headActiveList;
    } else {

        head = &headFreeList;
    }
    if (head->Flink->Flink == head) {
        pfn* freePage = head->Flink;
        head->Flink = head;
        head->Blink = head;
        return freePage;
    }

    pfn *freePage = (pfn *) head->Flink;
    head->Flink = freePage->entry.Flink;
    head->Flink->Blink = head;
    return freePage;

}


pte* va_to_pte(PVOID va) {
    // if pagetable was a pvoid must multiply by size
    ULONG64 index = ((ULONG_PTR)va - (ULONG_PTR) vaStart)/PAGE_SIZE;
    pte* pte = pagetable + index;
    return pte;
}

PVOID pte_to_va(pte* pte) {

    ULONG64 index = (pte - pagetable);
    return (PVOID)((index * PAGE_SIZE) + (ULONG_PTR) vaStart);

}


PVOID init(ULONG64 numBytes) {
    PVOID new;
    new = malloc(numBytes);
    memset(new, 0, numBytes);
    return new;
}


PVOID transferVa;
ULONG_PTR physical_page_count;
PULONG_PTR physical_page_numbers;


boolean* diskActive;
ULONG64 * number_of_open_slots;


pfn* endPFN;



ULONG64 most_free_disk_portion() {

    ULONG64 max = 0;
    ULONG64 index = 0;

    for (int i = 0; i < NUMBER_OF_DISK_DIVISIONS; ++i) {
        if (max<= number_of_open_slots[i]) {
            max = number_of_open_slots[i];
            index = i;
        }
    }
    return index;
}


// the size for the portions, I will be searching of diskSize
#define DISK_DIVISION_SIZE ((VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES)/PAGE_SIZE)/NUMBER_OF_DISK_DIVISIONS;
ULONG64 get_free_disk_index() {

    // get the subsection of the diskSlots array that you will be searching through
    ULONG64 freePortion = most_free_disk_portion();
    ULONG64 start = freePortion * DISK_DIVISION_SIZE + (ULONG64) diskActive;
    ULONG64 end = start + DISK_DIVISION_SIZE;


    for (start; start <= end; ++start) {
        if (!diskActive[start]) {
            number_of_open_slots[freePortion] -= 1;
            // This math translate a disk slot into the start of the corresponding page in the disk
            return (start - (ULONG64) diskActive);
        }
    }


    printf("couldn't find free page");
    return -1;
}

ULONG64 diskEnd;

VOID init_virtual_memory() {

    ULONG_PTR numBytes;
    ULONG_PTR numDiskSlots;

    // init disk stuff
    numBytes = VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES;
    diskStart = init(numBytes);
    diskEnd = diskStart + numBytes;

    numDiskSlots = numBytes / PAGE_SIZE;
    diskActive = init(numDiskSlots);

    numBytes = sizeof(ULONG64) * NUMBER_OF_DISK_DIVISIONS;
    number_of_open_slots = malloc(numBytes);
    for (int i = 0; i < NUMBER_OF_DISK_DIVISIONS; ++i) {
        number_of_open_slots[i] = DISK_DIVISION_SIZE;
    }




    // init the pagetable
    numBytes = VIRTUAL_ADDRESS_SIZE / PAGE_SIZE * sizeof(pte);
    pagetable = init(numBytes);
    // init the pfn array which will manage the free and active list
    numBytes = NUMBER_OF_PHYSICAL_PAGES * sizeof(pfn);
    pfnStart = init(numBytes);

    endPFN = pfnStart+numBytes;

    // initialize the free and active list as empty
    headFreeList.Flink = &headFreeList;
    headFreeList.Blink = &headFreeList;

    headActiveList.Flink = &headActiveList;
    headActiveList.Blink = &headActiveList;


    // add every page to the free list
    for (int i = 0; i < physical_page_count; ++i) {
        pfn *new_pfn = pfnStart + i;
        new_pfn->frameNumber = physical_page_numbers[i];
        listAdd(new_pfn, false);
    }


}

VOID modified_read(ULONG64 arbitrary_va, pte* currentPTE, pfn* freePage) {

    // modified reading

    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;

    // MUPP(va, size, physical page)

    if (MapUserPhysicalPages(transferVa, 1, &freePage->frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, &freePage->frameNumber);
        return;
    }

    //memcpy(va,va,size)
    memcpy(transferVa, (PVOID) diskAddress,PAGE_SIZE);
    //next time make a mark free on disk array of booleans

    if (MapUserPhysicalPages(transferVa, 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
        return;
    }

    diskActive[diskIndex] = FALSE;




}

VOID pfnInbounds(pfn* trimmed) {
    pfn* flink;
    pfn* blink;
    if (trimmed < pfnStart || trimmed >= endPFN) {
        DebugBreak();
    }
    // flink = trimmed->entry.Flink;
    // if (flink == (pfn*) &headFreeList || flink == (pfn*) &headActiveList) {
    //
    // } else if ((pfn*) flink < pfns || (pfn*) flink >= endPFN) {
    //     DebugBreak();
    // }
    //
    // blink = trimmed->entry.Blink;
    //
    // if (blink == (pfn*) &headFreeList || blink == (pfn*) &headActiveList) {
    //
    // } else if ((pfn*) blink < pfns || (pfn*) blink >= endPFN) {
    //     DebugBreak();
    // }

}

#if 0

ULONG64 pageBatch[5];
pfn* pfnBatch[5];
// get va
for (int i = 0; i < 5; ++i) {
    pfnBatch[i] = listRemove(true);
    pageBatch[i] = pfnBatch[i]->fn;
}
#endif


VOID page_trimmer(VOID) {

        pfn* trimmed = listRemove(REMOVE_ACTIVE_PAGE);
        ULONG64 trimmedVa = (ULONG64) pte_to_va(trimmed->pte);

        //kingbob make this not one to one and utilize the total space (disk-size = va size - physical size)
    //example search,  regionize, bound search
        ULONG64 diskIndex = get_free_disk_index() + (ULONG64) diskStart;

       pfnInbounds(trimmed);

        // update pte
        trimmed->pte->validFormat.valid = 0;
        trimmed->pte->invalidFormat.diskIndex =  diskIndex;

        // unmap from trimmed va
        if (MapUserPhysicalPages(trimmedVa, 1, NULL) == FALSE) {
            printf("full_virtual_memory_test : could not unmap VA %p\n", trimmedVa);
            return;
        }
        // modified writing
        ULONG64 diskByteAddress = (ULONG64) diskStart +  diskIndex * PAGE_SIZE;
        // map to transfer va
        if (MapUserPhysicalPages(transferVa, 1, &trimmed->frameNumber) == FALSE) {
            printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, trimmed->frameNumber);
            return;
        }

        // copy from transfer to disk
        memcpy((PVOID) diskByteAddress, transferVa, PAGE_SIZE);

        // I need to zero it because when a new va gets this page, it cant have another va's data
        memset(transferVa, 0, PAGE_SIZE);

        // unmap transfer
        if (MapUserPhysicalPages(transferVa, 1, NULL) == FALSE) {
            printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
            return;
        }
        diskActive[diskIndex] = TRUE;
        listAdd(trimmed, false);
}

VOID checkVa(PULONG64 va) {

    va = (PULONG64) ((ULONG64)va &  ~(PAGE_SIZE - 1));
    for (int i = 0; i < PAGE_SIZE / 8; ++i) {
        if (!(*va == 0 || *va == (ULONG64) va)) {
            DebugBreak();
        }
        va += 1;
    }
}
// implement this to save space instead of having a field
ULONG64 pfn_to_frame_number (pfn* pfn) {
    ULONG64 frameNumber;
    frameNumber = pfn - pfnStart;
    return frameNumber;

}



VOID
full_virtual_memory_test(
    VOID) {
    unsigned i;

    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    BOOL obtained_pages;

    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;

    pte* currentPTE;
    pfn* freePage;

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf("full_virtual_memory_test : could not get privilege\n");
        return;
    }

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    physical_page_handle = CreateSharedMemorySection ();

    if (physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError ());
        return;
    }

#else

    physical_page_handle = GetCurrentProcess();

#endif

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    physical_page_numbers = malloc(physical_page_count * sizeof(ULONG_PTR));

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

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //

    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;

    //
    // Round down to a PAGE_SIZE boundary.
    //

    virtual_address_size &= ~(PAGE_SIZE - 1);
    //11 = 1011 => 0100


    virtual_address_size_in_unsigned_chunks =
            virtual_address_size / sizeof(ULONG_PTR);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    p = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

#else

    vaStart = VirtualAlloc(NULL,
                     virtual_address_size,
                     MEM_RESERVE | MEM_PHYSICAL,
                     PAGE_READWRITE);

    transferVa = VirtualAlloc(NULL,
                     PAGE_SIZE,
                     MEM_RESERVE | MEM_PHYSICAL,
                     PAGE_READWRITE);

#if 0



#endif

#endif

    if (vaStart == NULL) {
        printf("full_virtual_memory_test : could not reserve memory %x\n",
               GetLastError());

        return;
    }

    //
    // Now perform random accesses.
    //

    for (i = 0; i < MB(1); i += 1) {
        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = (unsigned) (ReadTimeStampCounter() >> 4);

        random_number %= virtual_address_size_in_unsigned_chunks;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //


        //todo make false
        page_faulted = FALSE;

        //
        // Ensure the write to the arbitrary virtual address doesn't
        // straddle a PAGE_SIZE boundary just to keep things simple for
        // now.
        //

        random_number &= ~0x7;

        arbitrary_va = vaStart + random_number;

        __try {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            page_faulted = TRUE;
        }

        if (page_faulted) {
            // if free list is empty trim
            if (headFreeList.Flink == &headFreeList) {
                page_trimmer();
            }

            freePage = listRemove(REMOVE_FREE_PAGE);
            pfnInbounds(freePage);



            currentPTE = va_to_pte(arbitrary_va);

            // if di is not zero read in from the disk address
            if (currentPTE->invalidFormat.diskIndex != 0) {
                modified_read(arbitrary_va, currentPTE, freePage);
            }

            if (MapUserPhysicalPages(arbitrary_va, 1, &freePage->frameNumber) == FALSE) {
                DebugBreak();
                printf("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, freePage->frameNumber);
                return;
            }
            // update pte and activate the page
            freePage->pte = currentPTE;
            freePage->pte->validFormat.frameNumber = freePage->frameNumber;
            freePage->pte->validFormat.valid = 1;

            listAdd(freePage, true);


            *arbitrary_va = (ULONG_PTR) arbitrary_va;


        }
        checkVa(arbitrary_va);
    }

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree(vaStart, 0, MEM_RELEASE);

    return;
}


VOID
main(
    int argc,
    char **argv
) {
    //
    // Test a simple malloc implementation - we call the operating
    // system to pay the up front cost to reserve and commit everything.
    //
    // Page faults will occur but the operating system will silently
    // handle them under the covers invisibly to us.
    //

    //    malloc_test ();

    //
    // Test a slightly more complicated implementation - where we reserve
    // a big virtual address range up front, and only commit virtual
    // addresses as they get accessed.  This saves us from paying
    // commit costs for any portions we don't actually access.  But
    // the downside is what if we cannot commit it at the time of the
    // fault !
    //

    // commit_at_fault_time_test ();

    //
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
    //

    full_virtual_memory_test();

    return;
}
