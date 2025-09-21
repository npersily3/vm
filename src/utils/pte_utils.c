//
// Created by nrper on 7/17/2025.
//

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/utils/pte_utils.h"

#include "utils/thread_utils.h"
// Simple conversion and validation functions
pte*
va_to_pte(ULONG64 va) {
    ASSERT(isVaValid(va))
    ULONG64 index = ((ULONG_PTR)va - (ULONG_PTR) vm.va.start)/PAGE_SIZE;
    pte* pte = vm.pte.table + index;
    ASSERT(isPTEValid(pte))
    return pte;
}

PVOID
pte_to_va(pte* pte) {
    ULONG64 index = (pte - vm.pte.table);
    ASSERT(isPTEValid(pte))
    return (PVOID)((index * PAGE_SIZE) + (ULONG_PTR) vm.va.start);
}


BOOL isVaValid(ULONG64 va) {

    return (va >= (ULONG64) vm.va.start) && (va < (ULONG64) vm.va.end);
}
PTE_REGION* getPTERegion(pte* pte) {
    ULONG64 pageTableIndex = (pte - vm.pte.table);
    ULONG64 index = pageTableIndex / vm.config.number_of_ptes_per_region;
    return  vm.pte.RegionsBase + index;
}

pte* getFirstPTEInRegion(PTE_REGION* region) {
    ULONG64 regionIndex = (region - vm.pte.RegionsBase);
    ULONG64 pteIndex = regionIndex * vm.config.number_of_ptes_per_region;

    return vm.pte.table + pteIndex;
}

BOOL isPTEValid(pte* pte) {

    return ((ULONG64)pte >= (ULONG64) vm.pte.table) && ((ULONG64)pte < ((ULONG64) vm.pte.table + vm.config.page_table_size_in_bytes) );
}

VOID enterPTERegionLock(PTE_REGION* region, PTHREAD_INFO threadInfo) {

    EnterCriticalSection(&region->lock);

}
VOID leavePTERegionLock(PTE_REGION* region, PTHREAD_INFO threadInfo) {
    LeaveCriticalSection(&region->lock);
}
boolean tryEnterPTERegionLock(PTE_REGION* region, PTHREAD_INFO threadInfo) {
    return TryEnterCriticalSection(&region->lock);
}



VOID lockPTE(pte* pte) {

    PTE_REGION* region = getPTERegion(pte);

#if 0
    BOOL oldValue;
    acquire_srw_shared(&region->lock);

    do {
        oldValue = _interlockedbittestandset64((volatile LONG64*)&pte->entireFormat, 1);
    } while (oldValue == 1);
#endif


    EnterCriticalSection(&region->lock);
    //ASSERT(pte->transitionFormat.access == 0);
}

VOID unlockPTE(pte* pte) {
    PTE_REGION* region = getPTERegion(pte);
    //ASSERT(pte->transitionFormat.access == 0);

   // _interlockedbittestandreset64((volatile LONG64*)&pte->entireFormat, 1);
    LeaveCriticalSection(&region->lock);
}

/**
 * @brief NoFence write to a PTE. In debug mode, this function keeps track of how the pte has changed and the stack trace
 * @param pteAddress The address to write to
 * @param NewPteContents The new contents
 */
VOID writePTE(pte* pteAddress, pte NewPteContents) {
#if DBG
    recordPTEAccess(pteAddress, NewPteContents);
#endif
    WriteULong64NoFence(&pteAddress->entireFormat, NewPteContents.entireFormat);

}


#if DBG

VOID recordPTEAccess(pte* pteAddress, pte NewPteContents) {

    debugPTE* debug_pte;
    ULONG64 index;

    index = InterlockedIncrement64(&vm.pte.debugBufferIndex) - 1;
    index %= DEBUG_PTE_CIRCULAR_BUFFER_SIZE;
    debug_pte = &vm.pte.debugBuffer[index];

    debug_pte->pteAddress = pteAddress;
    debug_pte->oldPteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);
    debug_pte->pteContents = NewPteContents;
    debug_pte->threadId = GetCurrentThreadId();
    CaptureStackBackTrace(0,FRAMES_TO_CAPTURE,debug_pte->stacktrace,NULL);

}


#endif
