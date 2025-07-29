//
// Created by nrper on 7/16/2025.
//

#ifndef WRITER_THREAD_H
#define WRITER_THREAD_H

#include "../variables/structures.h"



DWORD diskWriter (LPVOID threadContext);
pfn* getPageFromModifiedList(VOID);
BOOL getDiskSlotAndUpdatePage (pfn* page, PULONG64 diskAddressArray, ULONG64 index);
BOOL getAllPagesAndDiskIndices (PULONG64 localBatchSizePointer, pfn** pfnArray, PULONG64 diskAddressArray, PULONG64 frameNumberArray, PTHREAD_INFO info);
VOID writeToDisk(ULONG64 localBatchSize, PULONG64 frameNumberArray, PULONG64 diskAddressArray);
VOID addToStandBy(ULONG64 localBatchSize, pfn** pfnArray, PTHREAD_INFO threadInfo);



#endif //WRITER_THREAD_H
