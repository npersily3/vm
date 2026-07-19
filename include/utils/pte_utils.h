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
pte* getFirstPTEInRegion(PTE_REGION* region);
BOOL isPTEValid(pte* pte);
VOID unlockPTE(pte* pte);
VOID lockPTE(pte* pte);
VOID setLockBit (pte* pte);
VOID clearLockBit(pte *pte);
pte writePTE(pte* pteAddress, pte NewPteContents, pte expectedOldPteContents);
#if DBG
VOID recordPTEAccess(pte* pteAddress, pte NewPteContents);
#endif


VOID enterPTERegionLock(PTE_REGION* region, PTHREAD_INFO threadInfo);
VOID leavePTERegionLock(PTE_REGION* region, PTHREAD_INFO threadInfo);
boolean tryEnterPTERegionLock(PTE_REGION* region, PTHREAD_INFO threadInfo);

#if CORRECTNESS
VOID checkVA(PULONG_PTR va);
#endif


pte* getNextLayer(pte* va, int current_layer, ULONG64 row);
ULONG64 parseVA_Row_value(ULONG64 va, int layer);

int findPTELayer(pte *pte);
pte *getHigherLevelPTEAddress(pte *currentPTE);
pte* getStartOfLowerPagetable(pte *currentPTE);
int isLeafPTE(pte* currentPTE);
#endif //PTE_UTILS_H
