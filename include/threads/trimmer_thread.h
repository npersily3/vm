//
// Created by nrper on 7/16/2025.
//

#ifndef TRIMMER_THREAD_H
#define TRIMMER_THREAD_H

#include "../variables/structures.h"

DWORD page_trimmer(LPVOID threadContext);
pfn* getActivePage(VOID);
BOOL isNextPageInSameRegion(PCRITICAL_SECTION trimmedPageTableLock);
VOID unmapBatch (PULONG64 virtualAddresses, ULONG64 batchSize);
VOID addBatchToModifiedList (pfn** pages, ULONG64 batchSize);

#endif //TRIMMER_THREAD_H
