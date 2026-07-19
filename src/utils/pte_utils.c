//
// Created by nrper on 7/17/2025.
//

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/utils/pte_utils.h"

#include "utils/thread_utils.h"
// Simple conversion and validation functions


/**
 * @brief slices the va to get what ever nine bits
 * @param va
 * @param layer
 * @return
 */

// this should work

//Decide what it does
#define LAST_NINE_BITS_MASK ((1 << 10) - 1)

//TODO deal with pointer types
pte *getNextLayer(pte *va, int current_layer, int row) {
    ULONG64 index = va - (pte *) vm.pte.start_of_layer[current_layer];
    pte *base = (pte *) vm.pte.start_of_layer[current_layer + 1];
    return base + ((index << 9) + row);
}

/**
 *
 * @param va The va to index into
 * @param layer what page table layer it represents
 * @return The nine bit row value
 */
//TODO some checks
ULONG64 parseVA_Row_value(ULONG64 va, int layer) {
    ULONG64 temp = va - (ULONG64) vm.va.start;
    temp = temp >> 12;
    return (temp >> ((vm.config.number_of_page_table_layers - 1 - layer) * 9)) & (LAST_NINE_BITS_MASK);
}


// assumes that all parent ptes are faulted in
PVOID
pte_to_va(pte *currentPTE) {
    ULONG64 final = 0;
    ULONG64 index;
    pte *parentPTEAddress;

    //TODO this only works for leave ptes

    PPAGETABLE page = (PPAGETABLE) PAGE_ALIGN((ULONG64) currentPTE);


    final += currentPTE - (pte *) page;

    // the minus 1 is for zero index and we do not go to i = 0 because then we will go out of bounds
    for (int i = vm.config.number_of_page_table_layers - 1; i > 0; i--) {
        index = page - vm.pte.start_of_layer[i];
        parentPTEAddress = index + (pte *) vm.pte.start_of_layer[i - 1];
        page = (PPAGETABLE) PAGE_ALIGN((ULONG64) parentPTEAddress);

        index = parentPTEAddress - (pte *) page;

        index = index << 9 * (vm.config.number_of_page_table_layers - i);

        final |= index;
    }

    return (PVOID) (final << 12);
}


BOOL isVaValid(ULONG64 va) {
    return (va >= (ULONG64) vm.va.start) && (va < (ULONG64) vm.va.end);
}

PTE_REGION *getPTERegion(pte *pte) {
    PPAGETABLE region = (PPAGETABLE) PAGE_ALIGN((ULONG64) pte);

    ULONG64 regionIndex = region - vm.pte.table;

    return vm.pte.regions_base + regionIndex;
}

pte *getFirstPTEInRegion(PTE_REGION *region) {
    ULONG64 regionIndex = (region - vm.pte.regions_base);
    return (pte *) (vm.pte.table + regionIndex);
}


VOID enterPTERegionLock(PTE_REGION *region, PTHREAD_INFO threadInfo) {
    EnterCriticalSection(&region->lock);
}

VOID leavePTERegionLock(PTE_REGION *region, PTHREAD_INFO threadInfo) {
    LeaveCriticalSection(&region->lock);
}

boolean tryEnterPTERegionLock(PTE_REGION *region, PTHREAD_INFO threadInfo) {
    return TryEnterCriticalSection(&region->lock);
}


VOID setLockBit(pte *pte) {
    BOOL oldValue;

    do {
        oldValue = _interlockedbittestandset64((volatile LONG64 *) &pte->entireFormat, 1);
    } while (oldValue == 1);
}

VOID clearLockBit(pte *pte) {
    int val = _interlockedbittestandreset64((volatile LONG64 *) &pte->entireFormat, 1);
    // only unlock locked ptes
    ASSERT(val == 1);
}

int findPTELayer(pte *pte) {
    for (int i = vm.config.number_of_page_table_layers - 1; i > 0; i--) {
        if (vm.pte.start_of_layer[i] < (PPAGETABLE) pte) {
            return i;
        }
    }
    return 0;
}

pte *getHigherLevelPTEAddress(pte *currentPTE) {
    PPAGETABLE currentPagetable = (PPAGETABLE) PAGE_ALIGN((ULONG64) currentPTE);
    ULONG64 layer = findPTELayer(currentPTE);

    ASSERT(layer == 0);
    ULONG64 index = currentPagetable - vm.pte.start_of_layer[layer];
    return ((pte *) vm.pte.start_of_layer[layer - 1] + index);
}

pte* getStartOfLowerPagetable(pte *currentPTE) {
    return getNextLayer(currentPTE,findPTELayer(currentPTE),0);
}

int isLeafPTE(pte* currentPTE) {
    return findPTELayer(currentPTE) == (vm.config.number_of_page_table_layers - 1);
}


//TODO I think this should more or less work, the main problem is the case where someone has to lock a highest level pte
// then this will break.
VOID lockPTE(pte *currentPTE) {
    pte *parent = getHigherLevelPTEAddress(currentPTE);
    setLockBit(parent);
}

VOID unlockPTE(pte *currentPTE) {
    pte *parent = getHigherLevelPTEAddress(currentPTE);
    clearLockBit(parent);
}


/**
 * @brief NoFence write to a PTE. In debug mode, this function keeps track of how the pte has changed and the stack trace
 * @param pteAddress The address to write to
 * @param NewPteContents The new contents
 * @param expectedOldPteContents The expected old contents.
 * @return The contents of the PTE before the write happened
 */
pte writePTE(pte *pteAddress, pte NewPteContents, pte expectedOldPteContents) {
#if DBG
    recordPTEAccess(pteAddress, NewPteContents);
#endif
    //make a check to see if we always have to use interlocked compare exchange if the previous entries valid bit is 0, then we can use the normal write

    pte returnVal;

    if (expectedOldPteContents.validFormat.valid == 0) {
        WriteULong64NoFence(&pteAddress->entireFormat, NewPteContents.entireFormat);
        return expectedOldPteContents;
    }

    returnVal.entireFormat = InterlockedCompareExchange64((volatile LONG64 *) &pteAddress->entireFormat,
                                                          NewPteContents.entireFormat,
                                                          expectedOldPteContents.entireFormat);
    return returnVal;
}


#if DBG

VOID recordPTEAccess(pte *pteAddress, pte NewPteContents) {
    debugPTE *debug_pte;
    ULONG64 index;

    index = InterlockedIncrement64(&vm.pte.debugBufferIndex) - 1;
    index %= DEBUG_PTE_CIRCULAR_BUFFER_SIZE;
    debug_pte = &vm.pte.debugBuffer[index];

    debug_pte->pteAddress = pteAddress;
    debug_pte->oldPteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);
    debug_pte->pteContents = NewPteContents;
    debug_pte->threadId = GetCurrentThreadId();
    CaptureStackBackTrace(0, FRAMES_TO_CAPTURE, debug_pte->stacktrace,NULL);
}


#endif

#if CORRECTNESS

VOID
checkVA(PULONG64 va) {
    va = (PULONG64) ((ULONG64) va & ~(PAGE_SIZE - 1));
    for (int i = 0; i < PAGE_SIZE / 8; ++i) {
        if (!(*va == 0 || *va == (ULONG64) va)) {
            DebugBreak();
        }
        va += 1;
    }
}
#endif
