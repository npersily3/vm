//
// Created by nrper on 7/16/2025.
//

#ifndef TRIMMER_THREAD_H
#define TRIMMER_THREAD_H

#include "../variables/structures.h"

DWORD page_trimmer(LPVOID threadContext);
pfn* getActivePage(PTHREAD_INFO threadInfo);
BOOL isNextPageInSameRegion(PTE_REGION* nextRegion, PTHREAD_INFO info);
VOID unmapBatch (PULONG64 virtualAddresses, ULONG64 batchSize);
VOID addBatchToModifiedList (pfn** pages, ULONG64 batchSize, PTHREAD_INFO info);

#endif //TRIMMER_THREAD_H
