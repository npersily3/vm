#ifndef INIT_H
#define INIT_H


#include <windows.h>


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


typedef struct _THREAD_INFO {

    ULONG ThreadNumber;

    ULONG ThreadId;
    HANDLE ThreadHandle;

    volatile ULONG ThreadCounter;

    HANDLE WorkDoneHandle;

#if 1

    // // way faster, now everything consumes 1 cache line
    // What effect would consuming extra space here have ?
    //

    volatile UCHAR Pad[32];

#endif

} THREAD_INFO, *PTHREAD_INFO;

typedef struct {

    LIST_ENTRY entry;
    ULONG64 length;

} listHead, *pListHead;

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

HANDLE createNewThread(LPTHREAD_START_ROUTINE ThreadFunction, PTHREAD_INFO ThreadContext);
VOID init_list_head(pListHead head);



HANDLE CreateSharedMemorySection(VOID);


#endif // INIT_H