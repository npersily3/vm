//
// Created by nrper on 7/16/2025.
//

#ifndef USER_THREAD_H
#define USER_THREAD_H

#include "../variables/structures.h"


BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID lpParam);
BOOL isRescue(pte* currentPTE);
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE, PTHREAD_INFO info);
BOOL mapPageFromFreeList (ULONG64 arbitrary_va, PTHREAD_INFO threadInfo, PULONG64 frameNumber);
pfn* getVictimFromStandByList (PCRITICAL_SECTION currentPageTableLock , PTHREAD_INFO threadInfo);
BOOL mapPageFromStandByList (ULONG64 arbitrary_va, PCRITICAL_SECTION currentPageTableLock, PTHREAD_INFO threadInfo, PULONG64 frameNumber);
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock);
VOID modified_read(pte* currentPTE, ULONG64 frameNumber, PTHREAD_INFO threadContext);
BOOL zeroOnePage (pfn* page, ULONG64 threadNumber);


#endif //USER_THREAD_H
