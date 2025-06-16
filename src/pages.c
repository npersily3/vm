//
// Created by nrper on 6/16/2025.
//
#include "pages.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>

VOID
modified_read(ULONG64 arbitrary_va, pte* currentPTE, pfn* freePage) {
    // Modified reading
    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;

    // MUPP(va, size, physical page)
    if (MapUserPhysicalPages(transferVa, 1, &freePage->frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, &freePage->frameNumber);
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

VOID
page_trimmer(VOID) {
    pfn* trimmed = listRemove(REMOVE_ACTIVE_PAGE);
    ULONG64 trimmedVa = (ULONG64) pte_to_va(trimmed->pte);

    // gets which disk page to write to
    ULONG64 diskIndex = get_free_disk_index();

    pfnInbounds(trimmed);

    // update pte
    trimmed->pte->validFormat.valid = 0;
    trimmed->pte->invalidFormat.diskIndex = diskIndex;

    // unmap from trimmed va
    if (MapUserPhysicalPages((PVOID)trimmedVa, 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", (PVOID)trimmedVa);
        DebugBreak();
        return;
    }

    // modified writing
    ULONG64 diskByteAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;

    // map to transfer va
    if (MapUserPhysicalPages(transferVa, 1, &trimmed->frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, trimmed->frameNumber);
        DebugBreak();
        return;
    }

    // copy from transfer to disk
    memcpy((PVOID) diskByteAddress, transferVa, PAGE_SIZE);

    // I need to zero it because when a new va gets this page, it cant have another va's data
    memset(transferVa, 0, PAGE_SIZE);

    // unmap transfer
    if (MapUserPhysicalPages(transferVa, 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
        DebugBreak();
        return;
    }

    listAdd(trimmed, FALSE);
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