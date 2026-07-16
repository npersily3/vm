//
// Created by nrper on 7/17/2025.
//

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/utils/pte_utils.h"

#include "utils/thread_utils.h"
// Simple conversion and validation functions

// TODO make a version that only returns the address
pte *
va_to_pte(ULONG64 va) {
    pte *currentPTE;
    pte *oldPTE;
    ASSERT(isVaValid(va))


    va = PAGE_ALIGN(va)
    )
    ;

    ULONG64 index = (va - (ULONG64) vm.va.start);

    // now get only the page table row numbers
    // TODO replace magic numbers
    index = index >> 12;

    ULONG64 row = (index >> (9 * (vm.config.number_of_page_table_layers - 1)));

    EnterCriticalSection(&vm.pte.rootLock);

    currentPTE = ((pte *) vm.pte.table) + row;

    if (currentPTE->validFormat.valid == 0) {
        //some function that makes a pagetable valid
    }
    ASSERT(topPTE.validFormat.valid == 1);

    // lock the pte
    currentPTE->validFormat.lock = 1;
    oldPTE = currentPTE;

    LeaveCriticalSection(&vm.pte.rootLock);

    // a ptes accessed in the loop will be locked. SO there is not a possibility of someone slipping in and trimming it
    for (int i = 1; i < vm.config.number_of_page_table_layers; i++) {
        // the minus one is becasue we are excluding ourselves,
        // the bit mask is to onlay take the nine bits we care about

        row = (index >> (9 * (vm.config.number_of_page_table_layers - i - 1))) & ((1 << (9 + 1)) - 1);
        currentPTE = (pte *) vm.pte.start_of_layer[i] + row;

        if (currentPTE->validFormat.valid == 0) {
            //some function that makes a pagetable valid
        }
        ASSERT(topPTE.validFormat.valid == 1);

        // lock the pte
        currentPTE->validFormat.lock = 1;

        // unlock the old one
        oldPTE->validFormat.lock = 0;

        // start the descent into a new page table layer
        oldPTE = currentPTE;
    }


    pte *pte = vm.pte.table + index;
    ASSERT(isPTEValid(pte))
    return pte;
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

VOID lockPTE(pte *pte) {
    PTE_REGION *region = getPTERegion(pte);

#if 0
    BOOL oldValue;
    acquire_srw_shared(&region->lock);

    do {
        oldValue = _interlockedbittestandset64((volatile LONG64 *) &pte->entireFormat, 1);
    } while (oldValue == 1);
#endif


    EnterCriticalSection(&region->lock);
    //ASSERT(pte->transitionFormat.access == 0);
}

VOID unlockPTE(pte *pte) {
    PTE_REGION *region = getPTERegion(pte);
    //ASSERT(pte->transitionFormat.access == 0);

    // _interlockedbittestandreset64((volatile LONG64*)&pte->entireFormat, 1);
    LeaveCriticalSection(&region->lock);
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
