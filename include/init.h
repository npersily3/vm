#ifndef INIT_H
#define INIT_H

#include "util.h"


//
// Configuration for multiple VA support
//
#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1

#define EVENT_START_ON TRUE
#define EVENT_START_OFF FALSE



#define SPIN_COUNTS                              1
#define SPIN_COUNT                               0xFFFFFF
#if SPIN_COUNTS
#define INITIALIZE_LOCK(x) \
(x) = malloc(sizeof(CRITICAL_SECTION)); \
InitializeCriticalSectionAndSpinCount((x), SPIN_COUNT)
#else
#define INITIALIZE_LOCK(x) \
(x) = malloc(sizeof(CRITICAL_SECTION)); \
InitializeCriticalSection(x)
#endif

// this one does not malloc and is used for the array that is statically declared
#if SPIN_COUNTS
#define INITIALIZE_LOCK_DIRECT(x) \
InitializeCriticalSectionAndSpinCount(&(x), SPIN_COUNT)
#else
#define INITIALIZE_LOCK_DIRECT(x) \
InitializeCriticalSection(&(x))
#endif

//
// Initialization functions
//
BOOL GetPrivilege(VOID);
VOID init_virtual_memory(VOID);
VOID createThreads(VOID);
ULONG64 getMaxFrameNumber(VOID);
VOID createEvents(VOID);
VOID initCriticalSections(VOID);
BOOL initVA (VOID);
VOID initializePageTableLocks(VOID);


VOID init_list_head(pListHead head);

HANDLE createNewThread(LPTHREAD_START_ROUTINE ThreadFunction, PTHREAD_INFO ThreadContext);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
HANDLE CreateSharedMemorySection(VOID);
#endif

#endif // INIT_H