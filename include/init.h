#ifndef INIT_H
#define INIT_H

#include "util.h"

//
// Configuration for multiple VA support
//
#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1

#define EVENT_START_ON TRUE
#define EVENT_START_OFF FALSE




//
// Initialization functions
//
BOOL GetPrivilege(VOID);
VOID init_virtual_memory(VOID);
VOID createThreads(VOID);
ULONG64 getMaxFrameNumber(VOID);
VOID createEvents(VOID);
VOID initCriticalSections(VOID);
VOID initVA (VOID);


#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
HANDLE CreateSharedMemorySection(VOID);
#endif

#endif // INIT_H