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

VOID removeFromMiddleOfList(pListHead head,LIST_ENTRY* entry) {
    LIST_ENTRY* prevEntry = entry->Blink;
    LIST_ENTRY* nextEntry = entry->Flink;
    prevEntry->Flink = nextEntry;
    nextEntry->Blink = prevEntry;
    head->length--;
}
//method that returns the index of the closet lock, takes advantage of truncation
PCRITICAL_SECTION getPageTableLock(pte* pte) {

    ULONG64 pageTableIndex = (pte - pageTable);
    ULONG64 index = pageTableIndex / SIZE_OF_PAGE_TABLE_DIVISION;

    return pageTableLocks + index;
}
BOOL isVaValid(ULONG64 va) {

    return (va >= (ULONG64) vaStart) && (va <= (ULONG64) vaEnd);
}

void acquireLock(PULONG64 lock) {
    ULONG64 oldValue;

    oldValue = *lock;

    while (TRUE) {

        if (oldValue == LOCK_FREE) {
            oldValue =  InterlockedCompareExchange(lock, LOCK_HELD, LOCK_FREE);
            if (oldValue == LOCK_FREE) {
                break;
            }
        }



    }
}
void releaseLock(PULONG64 lock) {
    InterlockedExchange(lock, LOCK_FREE);
}
BOOL tryAcquireLock(PULONG64 lock) {

    ULONG64 oldValue;

    oldValue =  InterlockedCompareExchange(lock, LOCK_HELD, LOCK_FREE);

    if (oldValue == 0) {
        return FALSE;
    }
    return TRUE;
}


VOID checkIfPageIsZero(PULONG64 startLocation) {
    ULONG64 startAddress = startLocation;
    ULONG64 endAddress = startLocation + PAGE_SIZE;

    while (startAddress < endAddress) {
        if (*(PULONG64)startAddress != 0) {
            return;
        }
    }
    DebugBreak();
}
