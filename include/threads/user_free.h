//
// Created by nrper on 7/30/2025.
//

#ifndef USER_FREE_H
#define USER_FREE_H
#include "variables/structures.h"


BOOL freeVa(PULONG64 arbitrary_va, PTHREAD_INFO threadInfo);
VOID unmapActivePage(pte* currentPTE, PTHREAD_INFO threadInfo, pfn* page) ;
BOOL unmapRescuePage(pte* currentPTE, PTHREAD_INFO threadInfo, pfn* page, boolean* addToFreeList);
BOOL unmapDiskFormatPTE(pte* currentPTE, PTHREAD_INFO threadInfo);

#endif //USER_FREE_H
