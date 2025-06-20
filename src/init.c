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

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

//
// Global variable definitions
//
LIST_ENTRY headFreeList;
LIST_ENTRY headActiveList;
LIST_ENTRY headModifiedList;
LIST_ENTRY headStandByList;

pte *pageTable;
pfn *pfnStart;
pfn *endPFN;
PULONG_PTR vaStart;
PVOID transferVa;
ULONG_PTR physical_page_count;
PULONG_PTR physical_page_numbers;

// Disk-related globals
PVOID diskStart;
ULONG64 diskEnd;
boolean* diskActive;
ULONG64* number_of_open_slots;

HANDLE workDoneThreadHandles[NUMBER_OF_THREADS];

 CRITICAL_SECTION lockFreeList;
 CRITICAL_SECTION lockActiveList;
 CRITICAL_SECTION lockModifiedList;
 CRITICAL_SECTION lockStandByList;
 CRITICAL_SECTION lockDiskActive;
 CRITICAL_SECTION lockNumberOfSlots;

HANDLE GlobalStartEvent;
HANDLE trimmingStartEvent;
HANDLE writingStartEvent;
HANDLE writingEndEvent;

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

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
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
#endif

VOID
init_virtual_memory(VOID) {
    ULONG_PTR numBytes;
    ULONG_PTR numDiskSlots;

    // Initialize disk structures
    numBytes = DISK_SIZE_IN_BYTES;
    diskStart = init_memory(numBytes);
    diskEnd = (ULONG64) diskStart + numBytes;

    numDiskSlots = DISK_SIZE_IN_PAGES;
    diskActive = (boolean*)init_memory(numDiskSlots);

    numBytes = sizeof(ULONG64) * NUMBER_OF_DISK_DIVISIONS;
    number_of_open_slots = (ULONG64*)malloc(numBytes);
    for (int i = 0; i < NUMBER_OF_DISK_DIVISIONS; ++i) {
        number_of_open_slots[i] = DISK_DIVISION_SIZE_IN_PAGES;
    }
    number_of_open_slots[NUMBER_OF_DISK_DIVISIONS - 1] += 1;

    // Initialize the page table
    numBytes = VIRTUAL_ADDRESS_SIZE / PAGE_SIZE * sizeof(pte);
    pageTable = (pte*)malloc(numBytes);

    ULONG64 length = numBytes / sizeof(pte);
    for (int i = 0; i < length; i++) {
        pageTable[i].invalidFormat.diskIndex = EMPTY_PTE;
        pageTable[i].invalidFormat.mustBeZero = 0;
    }

    // Initialize the PFN array which will manage the free and active list
   // numBytes = NUMBER_OF_PHYSICAL_PAGES * sizeof(pfn);
    pfnStart = (pfn*)init_memory(numBytes);
    ULONG64 max = getMaxFrameNumber();
    max+=1;
    pfnStart = VirtualAlloc(NULL,sizeof(pfn)*max,MEM_RESERVE,PAGE_READWRITE);
    endPFN = pfnStart + max;



    // Initialize the free and active list as empty
    headFreeList.Flink = &headFreeList;
    headFreeList.Blink = &headFreeList;

    headActiveList.Flink = &headActiveList;
    headActiveList.Blink = &headActiveList;

    headModifiedList.Flink = &headModifiedList;
    headModifiedList.Blink = &headModifiedList;

    headStandByList.Flink = &headStandByList;
    headStandByList.Blink = &headStandByList;


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
  //  createThreads();


}

ULONG64 getMaxFrameNumber(VOID) {
    ULONG64 maxFrameNumber = 0;

    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; ++i) {
        maxFrameNumber = max(maxFrameNumber, physical_page_numbers[i]);
    }
    return maxFrameNumber;
}



VOID createThreads(VOID) {
    LPVOID ThreadParameter;
    PTHREAD_INFO ThreadContext;
    HANDLE Handle;
    BOOL ReturnValue;
    LPTHREAD_START_ROUTINE ThreadFunction;
    THREAD_INFO ThreadInfo[NUMBER_OF_THREADS] = {0};

    THREAD_INFO UserThreadInfo[NUMBER_OF_USER_THREADS] = {0};
    THREAD_INFO TrimmerThreadInfo[NUMBER_OF_TRIMMING_THREADS] = {0};
    THREAD_INFO WriterThreadInfo[NUMBER_OF_WRITING_THREADS] = {0};

    ULONG maxThread = 0;

    ThreadFunction = testVM;

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

        Handle = createUserThread(ThreadContext);

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

        Handle = createTrimmingThread(ThreadContext);

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

        Handle = createWritingThread(ThreadContext);

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


    writingStartEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    trimmingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    writingEndEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_ON, NULL);
}
VOID initCriticalSections(VOID) {
    InitializeCriticalSection(&lockFreeList);
    InitializeCriticalSection(&lockActiveList);
    InitializeCriticalSection(&lockModifiedList);
    InitializeCriticalSection(&lockStandByList);
    InitializeCriticalSection(&lockDiskActive);
    InitializeCriticalSection(&lockNumberOfSlots);
}

HANDLE createTrimmingThread(PTHREAD_INFO ThreadContext) {
    return CreateThread(DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               page_trimmer,
                               ThreadContext,
                               DEFAULT_CREATION_FLAGS,
                               &ThreadContext->ThreadId);
}
HANDLE createWritingThread(PTHREAD_INFO ThreadContext) {
    return CreateThread(DEFAULT_SECURITY,
                           DEFAULT_STACK_SIZE,
                           diskWriter,
                           ThreadContext,
                           DEFAULT_CREATION_FLAGS,
                           &ThreadContext->ThreadId);
}

HANDLE createUserThread(PTHREAD_INFO ThreadContext) {
    return CreateThread(DEFAULT_SECURITY,
                           DEFAULT_STACK_SIZE,
                           testVM,
                           ThreadContext,
                           DEFAULT_CREATION_FLAGS,
                           &ThreadContext->ThreadId);
}
