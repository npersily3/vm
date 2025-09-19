//
// Created by nrper on 7/16/2025.
//

#ifndef USER_THREAD_H
#define USER_THREAD_H

#include "../variables/structures.h"


BOOL pageFault(PULONG_PTR arbitrary_va, LPVOID threadContext);
BOOL isRescue(pte* currentPTE);
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE, PTHREAD_INFO info);
pfn* getVictimFromStandByList (PTHREAD_INFO threadInfo);
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext);
VOID modified_read(pte* currentPTE, ULONG64 frameNumber, PTHREAD_INFO threadContext);
BOOL zeroOnePage (pfn* page, PTHREAD_INFO threadInfo);
pfn* getPageFromFreeList(PTHREAD_INFO threadContext);
pfn* getPageFromLocalList(PTHREAD_INFO threadContext);
VOID addPageToFreeList(pfn* page, PTHREAD_INFO threadInfo);

#endif //USER_THREAD_H
