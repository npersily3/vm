//
// Created by nrper on 7/17/2025.
//

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/utils/pte_utils.h"

pte*
va_to_pte(ULONG64 va) {
    ULONG64 index = ((ULONG_PTR)va - (ULONG_PTR) vm.va.start)/PAGE_SIZE;
    pte* pte = vm.pte.table + index;
    return pte;
}

PVOID
pte_to_va(pte* pte) {
    ULONG64 index = (pte - vm.pte.table);
    return (PVOID)((index * PAGE_SIZE) + (ULONG_PTR) vm.va.start);
}


BOOL isVaValid(ULONG64 va) {

    return (va >= (ULONG64) vm.va.start) && (va <= (ULONG64) vm.va.end);
}
PTE_REGION* getPTERegion(pte* pte) {
    ULONG64 pageTableIndex = (pte - vm.pte.table);
    ULONG64 index = pageTableIndex / vm.config.number_of_ptes_per_region;
    return  vm.pte.RegionsBase + index;
}