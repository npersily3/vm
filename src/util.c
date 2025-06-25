#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



pte*
va_to_pte(PVOID va) {
    ULONG64 index = ((ULONG_PTR)va - (ULONG_PTR) vaStart)/PAGE_SIZE;
    pte* pte = pageTable + index;
    return pte;
}

PVOID
pte_to_va(pte* pte) {
    ULONG64 index = (pte - pageTable);
    return (PVOID)((index * PAGE_SIZE) + (ULONG_PTR) vaStart);
}

PVOID
init_memory(ULONG64 numBytes) {
    PVOID new;
    new = malloc(numBytes);
    memset(new, 0, numBytes);
    return new;
}

VOID
pfnInbounds(pfn* trimmed) {
    if (trimmed < pfnStart || trimmed >= endPFN) {
        DebugBreak();
    }
}
ULONG64 getFrameNumber(pfn* pfn) {
    return (ULONG64)(pfn - pfnStart);
}

pfn* getPFNfromFrameNumber(ULONG64 frameNumber) {
    return pfnStart + frameNumber;
}

VOID removeFromMiddleOfList(LIST_ENTRY* entry) {
    LIST_ENTRY* nextEntry = entry->Flink;
    LIST_ENTRY* prevEntry = nextEntry->Blink;

    nextEntry->Blink = prevEntry;
    prevEntry->Flink = nextEntry;
}
