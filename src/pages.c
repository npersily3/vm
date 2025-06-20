//
// Created by nrper on 6/16/2025.
//
#include "pages.h"
#include "util.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>
#include"macros.h"

VOID
modified_read(pte* currentPTE, ULONG64 frameNumber) {
    // Modified reading
    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;


    // MUPP(va, size, physical page)
    if (MapUserPhysicalPages(transferVa, 1, &frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, frameNumber);
        return;
    }

    // memcpy(va,va,size)
    memcpy(transferVa, (PVOID) diskAddress, PAGE_SIZE);

    if (MapUserPhysicalPages(transferVa, 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
        return;
    }

    set_disk_space_free(diskIndex);
}

DWORD
page_trimmer(LPVOID lpParam) {

    WaitForSingleObject(trimmingStartEvent, INFINITE);

    pfn* page;
    ULONG64 va;

    for (int i = 0; i < BATCH_SIZE; ++i) {

        EnterCriticalSection(&lockActiveList);
        page = container_of(RemoveHeadList(&headActiveList), pfn, entry);
        LeaveCriticalSection(&lockActiveList);

        page->pte->transitionFormat.mustBeZero = 0;
        page->pte->transitionFormat.contentsLocation = MODIFIED_LIST;
        va = (ULONG64) pte_to_va(page->pte);

        // unmap everpage, ask how I can string these together
        if (MapUserPhysicalPages(va, 1, NULL) == FALSE) {
            printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
            return 1;
        }

        EnterCriticalSection(&lockModifiedList);
        InsertTailList(&headModifiedList, &page->entry);
        LeaveCriticalSection(&lockModifiedList);

    }
    SetEvent(writingStartEvent);
    return 0;
}

//modified to standBy
DWORD diskWriter(LPVOID lpParam) {
    WaitForSingleObject(writingStartEvent, INFINITE);

    pfn* page;
    ULONG64 frameNumber;
    ULONG64 diskIndex;
    ULONG64 diskAddressArray[BATCH_SIZE];
    ULONG64 frameNumberArray[BATCH_SIZE];
    pfn* pfnArray[BATCH_SIZE];


    for (int i = 0; i < BATCH_SIZE; ++i) {
        EnterCriticalSection(&lockModifiedList);
        page = container_of(RemoveHeadList(&headModifiedList), pfn, entry);
        LeaveCriticalSection(&lockModifiedList);

        pfnArray[i] = page;

        frameNumber = getFrameNumber(page);
        frameNumberArray[i] = frameNumber;

        diskIndex = get_free_disk_index();
        page->diskIndex = diskIndex;
        // modified writing
        ULONG64 diskByteAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;
        diskAddressArray[i] = diskByteAddress;

        page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;
    }

    // map to transfer va
    if (MapUserPhysicalPages(transferVa, BATCH_SIZE, frameNumberArray) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, frameNumber);
        DebugBreak();
        return 1;
    }

    for (int i = 0; i < BATCH_SIZE; ++i) {
        // copy from transfer to disk
        memcpy(&diskAddressArray[i], transferVa + i * PAGE_SIZE , PAGE_SIZE);
    }

    // unmap transfer
    if (MapUserPhysicalPages(transferVa, BATCH_SIZE, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
        DebugBreak();
        return 1;
    }

    for (int i = 0; i < BATCH_SIZE; ++i) {
        EnterCriticalSection(&lockStandByList);

        InsertHeadList(&headStandByList, &pfnArray[i]->entry);

        LeaveCriticalSection(&lockStandByList);
    }

    SetEvent(writingEndEvent);
    return 0;
}

VOID
checkVa(PULONG64 va) {
    va = (PULONG64) ((ULONG64)va & ~(PAGE_SIZE - 1));
    for (int i = 0; i < PAGE_SIZE / 8; ++i) {
        if (!(*va == 0 || *va == (ULONG64) va)) {
            DebugBreak();
        }
        va += 1;
    }
}