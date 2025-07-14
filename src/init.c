//
// Created by nrper on 6/16/2025.
//
#include "init.h"
#include "disk.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include "macros.h"
#include "pages.h"

#pragma comment(lib, "advapi32.lib")




#pragma comment(lib, "onecore.lib")


//
// Global variable definitions
//
listHead headFreeList;
listHead headActiveList;
listHead headModifiedList;
listHead headStandByList;
listHead headToBeZeroedList;

pte *pageTable;
pfn *pfnStart;
pfn *endPFN;
PULONG_PTR vaStart;
PULONG_PTR vaEnd;
PVOID transferVaWriting;
PVOID userThreadTransferVa[NUMBER_OF_USER_THREADS];
PVOID zeroThreadTransferVa;
ULONG_PTR physical_page_count;
PULONG_PTR physical_page_numbers;

// Disk-related globals
PVOID diskStart;
ULONG64 diskEnd;
PULONG64 diskActive;
PULONG64 diskActiveEnd;
PULONG64* diskActiveVa;
ULONG64* number_of_open_slots;

HANDLE workDoneThreadHandles[NUMBER_OF_THREADS];

 PCRITICAL_SECTION lockFreeList;
 PCRITICAL_SECTION lockActiveList;
 PCRITICAL_SECTION lockModifiedList;
 PCRITICAL_SECTION lockStandByList;
 PCRITICAL_SECTION lockDiskActive;
 PCRITICAL_SECTION lockNumberOfSlots;
PCRITICAL_SECTION lockWritingTransferVa;

ULONG64 lockModList;
ULONG64 lockToBeZeroedList;
CRITICAL_SECTION pageTableLocks[NUMBER_OF_PAGE_TABLE_LOCKS];


HANDLE GlobalStartEvent;
HANDLE trimmingStartEvent;
HANDLE writingStartEvent;
HANDLE writingEndEvent;
HANDLE userStartEvent;
HANDLE userEndEvent;
HANDLE zeroingStartEvent;

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

    ULONG_PTR numBytes;
    ULONG_PTR numEntries;

    // Initialize disk structures
    numBytes = DISK_SIZE_IN_BYTES;
    diskStart = init_memory(numBytes);
    diskEnd = (ULONG64) diskStart + numBytes;



#if DISK_SIZE_IN_PAGES % 64 != 0

    numEntries = DISK_SIZE_IN_PAGES / 64 + 1;

#else

    numDiskSlots = DISK_SIZE_IN_PAGES / 64;

#endif



    diskActive = (PULONG64)init_memory(numEntries*8);

    //make the first slot in valid
    diskActive[0] = DISK_ACTIVE;

    //makes it so that the out of bounds portion that exists in our diskMeta data is never accessed
#if DISK_SIZE_IN_PAGES % 64 != 0
    ULONG64 number_of_usable_bits;
    ULONG64 bitMask;
    bitMask = MAXULONG64;

    number_of_usable_bits = (DISK_SIZE_IN_PAGES % 64) - 1;

    bitMask &= (1 << (1 + number_of_usable_bits)) - 1;



    diskActive[numEntries - 1] = ~bitMask;



#endif
    diskActiveEnd = (PULONG64)((ULONG64) diskActive + numEntries);




    diskActiveVa = init_memory(numEntries * sizeof(ULONG64));


    numBytes = sizeof(ULONG64) * NUMBER_OF_DISK_DIVISIONS;
    number_of_open_slots = (ULONG64*)malloc(numBytes);

    if (number_of_open_slots == NULL) {
        printf("Failed to allocate memory for number_of_open_slots\n");
        return;
    }

    for (int i = 0; i < NUMBER_OF_DISK_DIVISIONS; ++i) {
        number_of_open_slots[i] = DISK_DIVISION_SIZE_IN_PAGES;
    }
    number_of_open_slots[NUMBER_OF_DISK_DIVISIONS - 1] += 2;
    number_of_open_slots[0] -= 1;

    // Initialize the page table
    numBytes = PAGE_TABLE_SIZE_IN_BYTES;
    pageTable = (pte*)init_memory(numBytes);


    ULONG64 max = getMaxFrameNumber();
    max += 1;
    pfnStart = VirtualAlloc(NULL,sizeof(pfn)*max,MEM_RESERVE,PAGE_READWRITE);

    if (pfnStart == NULL) {
        printf("Failed to reserve memory for PFN database\n");
        return;
    }
    endPFN = pfnStart + max;



    init_list_head(&headToBeZeroedList);
    init_list_head(&headStandByList);
    init_list_head(&headModifiedList);
    init_list_head(&headFreeList);
    init_list_head(&headActiveList);

    // Add every page to the free list
    for (int i = 0; i < physical_page_count; ++i) {


        pfn *new_pfn = (pfn*)(pfnStart + physical_page_numbers[i]);

        // Calculate the page-aligned range that contains this pfn structure
        PVOID startPage = (PVOID)ROUND_DOWN_TO_PAGE(new_pfn);
        PVOID endPage = (PVOID)ROUND_UP_TO_PAGE((ULONG_PTR)new_pfn + sizeof(pfn));
        SIZE_T commitSize = (ULONG_PTR)endPage - (ULONG_PTR)startPage;

        // Commit the full page(s) that contain this pfn structure
        if (VirtualAlloc(startPage, commitSize, MEM_COMMIT, PAGE_READWRITE) != startPage) {
            printf("Failed to commit at expected address\n");
        }

        // Now initialize the pfn structure
        memset(new_pfn, 0, sizeof(pfn));
        InsertTailList(&headFreeList, &new_pfn->entry);
    }


    createThreads();



}
VOID init_list_head(pListHead head) {
    head->entry.Flink = &head->entry;
    head->entry.Blink = &head->entry;
    head->length = 0;
}



BOOL initVA (VOID) {
    BOOL allocated;

    BOOL privilege;

    HANDLE physical_page_handle;



    // Allocate the physical pages that we will be managing.
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf("full_virtual_memory_test : could not get privilege\n");
        return FALSE;
    }


    physical_page_handle = CreateSharedMemorySection();

    if (physical_page_handle == NULL) {
        printf("CreateFileMapping2 failed, error %#x\n", GetLastError());
        return FALSE;
    }

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;
    physical_page_numbers = (PULONG_PTR)malloc(physical_page_count * sizeof(ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return FALSE;
    }

    allocated = AllocateUserPhysicalPages(physical_page_handle,
                                          &physical_page_count,
                                          physical_page_numbers);

    if (allocated == FALSE) {
        printf("full_virtual_memory_test : could not allocate physical pages\n");
        return FALSE;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
               physical_page_count,
               NUMBER_OF_PHYSICAL_PAGES);
    }

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section created above
    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    vaStart = (PULONG_PTR)VirtualAlloc2(NULL,
                                        NULL,
                                        VIRTUAL_ADDRESS_SIZE,
                                        MEM_RESERVE | MEM_PHYSICAL,
                                        PAGE_READWRITE,
                                        &parameter,
                                        1);
    if (vaStart == NULL) {
        printf("Failed to allocate virtual address space\n");
        return FALSE;
    }

    vaEnd = (PULONG64) ((ULONG64) vaStart + VIRTUAL_ADDRESS_SIZE);

    transferVaWriting = (PULONG_PTR)VirtualAlloc2(NULL,
                                        NULL,
                                        PAGE_SIZE*BATCH_SIZE,
                                        MEM_RESERVE | MEM_PHYSICAL,
                                        PAGE_READWRITE,
                                        &parameter,
                                        1);
    if (transferVaWriting == NULL) {
        printf("Failed to allocate transfer VA for writing\n");
        return FALSE;
    }

    zeroThreadTransferVa = VirtualAlloc2(NULL,
                                        NULL,
                                        PAGE_SIZE,
                                        MEM_RESERVE | MEM_PHYSICAL,
                                        PAGE_READWRITE,
                                        &parameter,
                                        1);
    if (zeroThreadTransferVa == NULL) {
        printf("Failed to allocate zero thread transfer VA\n");
        return FALSE;
    }

    for (int i = 0; i < NUMBER_OF_USER_THREADS; ++i) {
        userThreadTransferVa[i] = (PULONG_PTR)VirtualAlloc2(NULL,
                                            NULL,
                                            PAGE_SIZE,
                                            MEM_RESERVE | MEM_PHYSICAL,
                                            PAGE_READWRITE,
                                            &parameter,
                                            1);
        if (userThreadTransferVa[i] == NULL) {
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

    for (i = 0; i < NUMBER_OF_PHYSICAL_PAGES; ++i) {
        maxFrameNumber = max(maxFrameNumber, physical_page_numbers[i]);
    }
    return maxFrameNumber;
}



VOID createThreads(VOID) {


    PTHREAD_INFO ThreadContext;
    HANDLE Handle;
    BOOL ReturnValue;

    THREAD_INFO ThreadInfo[NUMBER_OF_THREADS] = {0};

    THREAD_INFO UserThreadInfo[NUMBER_OF_USER_THREADS] = {0};
    THREAD_INFO TrimmerThreadInfo[NUMBER_OF_TRIMMING_THREADS] = {0};
    THREAD_INFO WriterThreadInfo[NUMBER_OF_WRITING_THREADS] = {0};
    THREAD_INFO ZeroIngThreadInfo[NUMBER_OF_ZEROING_THREADS] = {0};

    ULONG maxThread = 0;



    createEvents();

    initCriticalSections();

    for (int i = 0; i < NUMBER_OF_USER_THREADS; ++i) {
        ThreadContext = &UserThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->WorkDoneHandle = CreateEvent (NULL,
                                                     AUTO_RESET,
                                                     FALSE,
                                                     NULL);
        if (ThreadContext->WorkDoneHandle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create work event %x\n", ReturnValue);
            return;
        }

        Handle = createNewThread(testVM, ThreadContext);

        if (Handle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create thread %x\n", ReturnValue);
            return;
        }

        ThreadContext->ThreadHandle = Handle;
        workDoneThreadHandles[maxThread] = ThreadContext->WorkDoneHandle;

        maxThread++;
    }

    for (int i = 0; i < NUMBER_OF_TRIMMING_THREADS; ++i) {
        ThreadContext = &TrimmerThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->WorkDoneHandle = CreateEvent (NULL,
                                                     AUTO_RESET,
                                                     FALSE,
                                                     NULL);
        if (ThreadContext->WorkDoneHandle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create work event %x\n", ReturnValue);
            return;
        }

        Handle = createNewThread(page_trimmer, ThreadContext);

        if (Handle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create thread %x\n", ReturnValue);
            return;
        }

        ThreadContext->ThreadHandle = Handle;
        workDoneThreadHandles[maxThread] = ThreadContext->WorkDoneHandle;

        maxThread++;
    }
    for (int i = 0; i < NUMBER_OF_WRITING_THREADS; ++i) {
        ThreadContext = &WriterThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->WorkDoneHandle = CreateEvent (NULL,
                                                     AUTO_RESET,
                                                     FALSE,
                                                     NULL);
        if (ThreadContext->WorkDoneHandle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create work event %x\n", ReturnValue);
            return;
        }

        Handle = createNewThread(diskWriter,ThreadContext);

        if (Handle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create thread %x\n", ReturnValue);
            return;
        }

        ThreadContext->ThreadHandle = Handle;
        workDoneThreadHandles[maxThread] = ThreadContext->WorkDoneHandle;

        maxThread++;
    }

    for (int i = 0; i < NUMBER_OF_WRITING_THREADS; ++i) {
        ThreadContext = &WriterThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->WorkDoneHandle = CreateEvent (NULL,
                                                     AUTO_RESET,
                                                     FALSE,
                                                     NULL);
        if (ThreadContext->WorkDoneHandle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create work event %x\n", ReturnValue);
            return;
        }

        Handle = createNewThread(zeroingThread,ThreadContext);

        if (Handle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create thread %x\n", ReturnValue);
            return;
        }

        ThreadContext->ThreadHandle = Handle;
        workDoneThreadHandles[maxThread] = ThreadContext->WorkDoneHandle;

        maxThread++;
    }

}
VOID createEvents(VOID) {

    userStartEvent = CreateEvent (NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    writingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    trimmingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    writingEndEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    userEndEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    zeroingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
}
VOID initCriticalSections(VOID) {
    INITIALIZE_LOCK(lockFreeList);
    INITIALIZE_LOCK(lockActiveList);
    INITIALIZE_LOCK(lockModifiedList);
    INITIALIZE_LOCK(lockStandByList);
    INITIALIZE_LOCK(lockDiskActive);
    INITIALIZE_LOCK(lockNumberOfSlots);

    lockModList = LOCK_FREE;
    lockToBeZeroedList = LOCK_FREE;

    initializePageTableLocks();

}
VOID initializePageTableLocks(VOID) {
    for (int i = 0; i < NUMBER_OF_PAGE_TABLE_LOCKS; ++i) {
        INITIALIZE_LOCK_DIRECT(pageTableLocks[i]);
    }
}



HANDLE createNewThread(LPTHREAD_START_ROUTINE ThreadFunction, PTHREAD_INFO ThreadContext) {
    return CreateThread(DEFAULT_SECURITY,
                           DEFAULT_STACK_SIZE,
                           ThreadFunction,
                           ThreadContext,
                           DEFAULT_CREATION_FLAGS,
                           &ThreadContext->ThreadId);
}