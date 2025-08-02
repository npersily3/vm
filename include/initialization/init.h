#ifndef INIT_H
#define INIT_H


#include <windows.h>
#include "../variables/structures.h"


extern volatile ULONG64 pageWaits;
extern volatile ULONG64 totalTimeWaiting;

//
// Initialization functions
//
BOOL GetPrivilege(VOID);
VOID init_virtual_memory(VOID);

ULONG64 getMaxFrameNumber(VOID);
BOOL initVA (VOID);
VOID initializePageTableLocks(VOID);
VOID init_pfns(VOID);
BOOL getPhysicalPages (VOID);
VOID init_lists(VOID);
VOID init_free_list(VOID);
VOID init_list_head(pListHead head);
VOID init_num_open_slots(VOID);
VOID init_disk_active(VOID);
PVOID init_memory(ULONG64 numBytes);
VOID init_pageTable(VOID);
VOID init_pte_regions(VOID);
VOID init_disk(VOID);
HANDLE CreateSharedMemorySection(VOID);


#endif // INIT_H