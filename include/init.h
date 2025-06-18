#ifndef INIT_H
#define INIT_H

#include "util.h"

//
// Configuration for multiple VA support
//
#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

//
// Initialization functions
//
BOOL GetPrivilege(VOID);
VOID init_virtual_memory(VOID);
VOID createThreads(VOID);
ULONG64 getMaxFrameNumber(VOID);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
HANDLE CreateSharedMemorySection(VOID);
#endif

#endif // INIT_H