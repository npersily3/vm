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
VOID lockPTE(pte* pte) {

    PTE_REGION* region = getPTERegion(pte);

#if 0
    BOOL oldValue;
    acquire_srw_shared(&region->lock);

    do {
        oldValue = _interlockedbittestandset64((volatile LONG64*)&pte->entireFormat, 1);
    } while (oldValue == 1);
#endif


    acquire_srw_exclusive(&region->lock, NULL);
    //ASSERT(pte->transitionFormat.access == 0);
}

VOID unlockPTE(pte* pte) {
    PTE_REGION* region = getPTERegion(pte);
    //ASSERT(pte->transitionFormat.access == 0);

   // _interlockedbittestandreset64((volatile LONG64*)&pte->entireFormat, 1);
    release_srw_exclusive(&region->lock);
}