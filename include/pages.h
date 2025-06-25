#ifndef PAGES_H
#define PAGES_H

#include "util.h"

#define BATCH_SIZE 5

extern HANDLE trimmingStartEvent;
extern HANDLE writingStartEvent;
extern HANDLE writingEndEvent;
extern HANDLE userStartEvent;
extern HANDLE userEndEvent;

extern CRITICAL_SECTION lockFreeList;
extern CRITICAL_SECTION lockActiveList;
extern CRITICAL_SECTION lockModifiedList;
extern CRITICAL_SECTION lockStandByList;
extern CRITICAL_SECTION lockDiskActive;
extern CRITICAL_SECTION lockNumberOfSlots;
extern CRITICAL_SECTION lockPageTable;
//
// Page management functions
//
VOID modified_read(pte* currentPTE, ULONG64 frameNumber);
DWORD page_trimmer(LPVOID lpParam);
VOID checkVa(PULONG64 va);
DWORD diskWriter(LPVOID lpParam);
ULONG64 modifiedLongerThanBatch();

#endif // PAGES_H