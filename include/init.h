#ifndef INIT_H
#define INIT_H

#include "util.h"

//
// Configuration for multiple VA support
//
#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

#define EVENT_START_ON TRUE
#define EVENT_START_OFF FALSE


#define NUMBER_OF_USER_THREADS 1
#define NUMBER_OF_TRIMMING_THREADS 1
#define NUMBER_OF_WRITING_THREADS 1

//
// Initialization functions
//
BOOL GetPrivilege(VOID);
VOID init_virtual_memory(VOID);
VOID createThreads(VOID);
ULONG64 getMaxFrameNumber(VOID);
VOID createEvents(VOID);
VOID initCriticalSections(VOID);

HANDLE createTrimmingThread(PTHREAD_INFO ThreadContext);
HANDLE createWritingThread(PTHREAD_INFO ThreadContext);
HANDLE createUserThread(PTHREAD_INFO ThreadContext);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
HANDLE CreateSharedMemorySection(VOID);
#endif

#endif // INIT_H