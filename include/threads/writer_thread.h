//
// Created by nrper on 7/16/2025.
//

#ifndef WRITER_THREAD_H
#define WRITER_THREAD_H

#include "../variables/structures.h"



DWORD diskWriter (LPVOID threadContext);
pfn* getPageFromModifiedList(VOID);
VOID updatePage (pfn* page,ULONG64 index);
ULONG64 getPagesFromModifiedList (ULONG64 localBatchSize, pfn** pfnArray, PULONG64 diskIndexArray, PULONG64 frameNumberArray, PTHREAD_INFO info);
VOID writeToDisk(ULONG64 localBatchSize, PULONG64 frameNumberArray, PULONG64 diskAddressArray, PTHREAD_INFO info);
ULONG64 addToStandBy(ULONG64 localBatchSize, pfn** pfnArray, PTHREAD_INFO threadInfo);
VOID freeUnusedDiskSlots(PULONG64 diskIndexArray, ULONG64 start, ULONG64 end) ;
VOID getDiskAddressesFromDiskIndices(PULONG64 indices, PULONG64 addresses, ULONG64 size);



#endif //WRITER_THREAD_H
