//
// Created by nrper on 7/17/2025.
//

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/utils/pte_utils.h"

pte*
va_to_pte(ULONG64 va) {
    ULONG64 index = ((ULONG_PTR)va - (ULONG_PTR) vaStart)/PAGE_SIZE;
    pte* pte = pageTable + index;
    return pte;
}

PVOID
pte_to_va(pte* pte) {
    ULONG64 index = (pte - pageTable);
    return (PVOID)((index * PAGE_SIZE) + (ULONG_PTR) vaStart);
}


BOOL isVaValid(ULONG64 va) {

    return (va >= (ULONG64) vaStart) && (va <= (ULONG64) vaEnd);
}
PTE_REGION* getPTERegion(pte* pte) {
    ULONG64 pageTableIndex = (pte - pageTable);
    ULONG64 index = pageTableIndex / NUMBER_OF_PTES_PER_REGION;
    return  pteRegionsBase + index;
}