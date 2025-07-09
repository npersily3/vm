#ifndef PAGES_H
#define PAGES_H

#include "util.h"

#define BATCH_SIZE 5

extern HANDLE trimmingStartEvent;
extern HANDLE writingStartEvent;
extern HANDLE writingEndEvent;
extern HANDLE userStartEvent;
extern HANDLE userEndEvent;

extern  PCRITICAL_SECTION lockFreeList;
extern  PCRITICAL_SECTION lockActiveList;
extern PCRITICAL_SECTION lockModifiedList;
extern PCRITICAL_SECTION lockStandByList;
extern PCRITICAL_SECTION lockDiskActive;
extern PCRITICAL_SECTION lockNumberOfSlots;
//
// Page management functions
//
VOID modified_read(pte* currentPTE, ULONG64 frameNumber, ULONG64 threadNumber);
DWORD page_trimmer(LPVOID lpParam);
BOOL checkVa(PULONG64 start, PULONG64 va);
DWORD diskWriter(LPVOID lpParam);
DWORD zeroingThread (LPVOID lpParam);
ULONG64 modifiedLongerThanBatch();

#endif // PAGES_H