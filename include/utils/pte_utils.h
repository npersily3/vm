//
// Created by nrper on 7/17/2025.
//

#ifndef PTE_UTILS_H
#define PTE_UTILS_H

#include "../variables/structures.h"

pte* va_to_pte(ULONG64 va);
PVOID pte_to_va(pte* pte);
PCRITICAL_SECTION getPageTableLock(pte* pte);
BOOL isVaValid(ULONG64 va);
PTE_REGION* getPTERegion(pte* pte);
BOOL isPTEValid(pte* pte);

#endif //PTE_UTILS_H
