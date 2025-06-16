//
// Created by nrper on 6/16/2025.
//
#include "init.h"
#include "disk.h"
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

//
// Global variable definitions
//
LIST_ENTRY headFreeList;
LIST_ENTRY headActiveList;
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
    numBytes = NUMBER_OF_PHYSICAL_PAGES * sizeof(pfn);
    pfnStart = (pfn*)init_memory(numBytes);
    endPFN = pfnStart + NUMBER_OF_PHYSICAL_PAGES;

    // Initialize the free and active list as empty
    headFreeList.Flink = &headFreeList;
    headFreeList.Blink = &headFreeList;
    headActiveList.Flink = &headActiveList;
    headActiveList.Blink = &headActiveList;

    // Add every page to the free list
    for (int i = 0; i < physical_page_count; ++i) {
        pfn *new_pfn = pfnStart + i;
        new_pfn->frameNumber = physical_page_numbers[i];
        listAdd(new_pfn, FALSE);
    }
}