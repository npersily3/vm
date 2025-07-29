//
// Created by nrper on 7/16/2025.
//

#ifndef GLOBALS_H
#define GLOBALS_H
#include "structures.h"


//
extern HANDLE physical_page_handle;

//
// Page list heads
//
extern listHead headFreeList;
extern listHead headActiveList;
extern listHead headModifiedList;
extern listHead headStandByList;
extern listHead headToBeZeroedList;

//
// Page table and PFN database
//
extern pte *pageTable;
extern pfn *pfnStart;
extern pfn *endPFN;
extern PTE_REGION* pteRegionsBase;

//
// Virtual address space
//
extern PULONG_PTR vaStart;
extern PULONG_PTR vaEnd;
extern PVOID transferVaWriting;
extern PVOID userThreadTransferVa[NUMBER_OF_USER_THREADS];
extern PVOID zeroThreadTransferVa;

//
// Physical memory information
//
extern ULONG_PTR physical_page_count;
extern PULONG_PTR physical_page_numbers;

//
// Disk-related globals
//
extern PVOID diskStart;
extern ULONG64 diskEnd;
extern PULONG64 diskActive;
extern PULONG64 diskActiveEnd;
extern PULONG64* diskActiveVa;
extern ULONG64* number_of_open_slots;

//
// Thread handles
//
extern HANDLE workDoneThreadHandles[NUMBER_OF_THREADS];
extern HANDLE userThreadHandles[NUMBER_OF_USER_THREADS];
extern HANDLE systemThreadHandles[NUMBER_OF_SYSTEM_THREADS];

//
// Synchronization events
//
extern HANDLE GlobalStartEvent;
extern HANDLE trimmingStartEvent;
extern HANDLE writingStartEvent;
extern HANDLE writingEndEvent;
extern HANDLE userStartEvent;
extern HANDLE userEndEvent;
extern HANDLE zeroingStartEvent;
extern HANDLE systemShutdownEvent;
extern HANDLE statisticsStartEvent;
//
// Critical sections
//

extern PCRITICAL_SECTION lockDiskActive;
extern PCRITICAL_SECTION lockNumberOfSlots;
extern PCRITICAL_SECTION lockWritingTransferVa;
extern CRITICAL_SECTION lockTransferReadingVa[NUMBER_OF_USER_THREADS];


//
// Interlocked locks
//

extern ULONG64 lockToBeZeroedList;

//
// Arrays of thread info
//
extern THREAD_INFO UserThreadInfo[NUMBER_OF_USER_THREADS];
extern THREAD_INFO TrimmerThreadInfo[NUMBER_OF_TRIMMING_THREADS];
extern THREAD_INFO WriterThreadInfo[NUMBER_OF_WRITING_THREADS];
extern THREAD_INFO ZeroingThreadInfo[NUMBER_OF_ZEROING_THREADS];

#endif //GLOBALS_H
