//
// Created by nrper on 7/16/2025.
//

#ifndef STRUCTURES_H
#define STRUCTURES_H


#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

//
// Configuration constants
//
#define PAGE_SIZE                   4096
// 52 bits is max that bus accepts and if each page is 4k or 12 bits, there is 40 bits left over
#define frame_number_size           40
#define KB(x)                       (x*1024)
#define MB(x)                       ((x) * 1024 * 1024)
#define VIRTUAL_ADDRESS_SIZE        MB(2)//MB(16)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))
#define NUMBER_OF_PHYSICAL_PAGES MB(1)/PAGE_SIZE
#define NUMBER_OF_DISK_DIVISIONS   1
#define DISK_SIZE_IN_BYTES         (VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES + 2* PAGE_SIZE)
#define DISK_SIZE_IN_PAGES         (DISK_SIZE_IN_BYTES / PAGE_SIZE)
#define DISK_DIVISION_SIZE_IN_PAGES (DISK_SIZE_IN_PAGES / (NUMBER_OF_DISK_DIVISIONS) )
#define EMPTY_PTE                  0

#define AUTO_RESET              FALSE
#define MANUAL_RESET            TRUE
#define WAIT_FOR_ALL            TRUE
#define WAIT_FOR_ONE            FALSE
#define DEFAULT_SECURITY        ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE      0
#define DEFAULT_CREATION_FLAGS  0

#define ROUND_DOWN_TO_PAGE(addr) ((ULONG_PTR)(addr) & ~(PAGE_SIZE - 1))
#define ROUND_UP_TO_PAGE(addr) (((ULONG_PTR)(addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define LOCK_FREE 0
#define LOCK_HELD 1

#define DISK_ACTIVE 1
#define VARIABLE_DID_NOT_CHANGE 0
#define DISK_FREE 0

#define FULL_DISK_SPACE ~0ULL

// List operation constants
#define REMOVE_FREE_PAGE           FALSE
#define REMOVE_ACTIVE_PAGE         TRUE

// Debug macros
#define DBG 0
#if DBG
#define ASSERT(x) if ((x) == FALSE) DebugBreak();
#else
#define ASSERT(x)
#endif

#define container_of(ptr, type, member) \
((type *)((char *)(ptr) - offsetof(type, member)))

#define NUMBER_OF_USER_THREADS 2
#define NUMBER_OF_ZEROING_THREADS 1
#define NUMBER_OF_TRIMMING_THREADS 1
#define NUMBER_OF_WRITING_THREADS 1
//#define NUMBER_OF_SCHEDULING_THREADS 1

#define NUMBER_OF_THREADS (NUMBER_OF_USER_THREADS + NUMBER_OF_ZEROING_THREADS + NUMBER_OF_TRIMMING_THREADS + NUMBER_OF_WRITING_THREADS )//+)// NUMBER_OF_SCHEDULING_THREADS)
#define NUMBER_OF_SYSTEM_THREADS (NUMBER_OF_THREADS-NUMBER_OF_USER_THREADS)

#define MAX_FAULTS 0xFFFFFF
#define BATCH_SIZE (NUMBER_OF_PHYSICAL_PAGES / 4)

#define COULD_NOT_FIND_SLOT (~0ULL)
#define LIST_IS_EMPTY 0

#define REDO_FAULT TRUE
//
// Thread configuration
//
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
// PTE format definitions
//
typedef struct {
    ULONG64 valid: 1;
    ULONG64 transition: 2;
    ULONG64 frameNumber: frame_number_size;
} validPte;

typedef struct {
    ULONG64 mustBeZero: 1;
    ULONG64 transition: 2;
    ULONG64 diskIndex: frame_number_size;
} invalidPte;

#define DISK 0
#define UNASSIGNED 1
#define MODIFIED_LIST 2
#define STAND_BY_LIST 3

typedef struct {
    ULONG64 mustBeZero: 1;
    ULONG64 contentsLocation: 2;
    ULONG64 frameNumber: frame_number_size;
} transitionPte;

typedef struct {
    union {
        validPte validFormat;
        invalidPte invalidFormat;
        transitionPte transitionFormat;
        ULONG64 entireFormat;
    };
} pte;

//
// Page table configuration
//
#define PAGE_TABLE_SIZE_IN_BYTES (VIRTUAL_ADDRESS_SIZE / PAGE_SIZE * sizeof(pte))
#define NUMBER_OF_PTES (PAGE_TABLE_SIZE_IN_BYTES / sizeof(pte))
#define NUMBER_OF_PAGE_TABLE_LOCKS NUMBER_OF_PTES / 8
#define SIZE_OF_PAGE_TABLE_DIVISION (PAGE_TABLE_SIZE_IN_BYTES/NUMBER_OF_PAGE_TABLE_LOCKS)

//
// Thread information structure
//
typedef struct _THREAD_INFO {
    ULONG ThreadNumber;
    ULONG ThreadId;
    HANDLE ThreadHandle;
    volatile ULONG ThreadCounter;
    HANDLE WorkDoneHandle;

#if 1
    // way faster, now everything consumes 1 cache line
    // What effect would consuming extra space here have ?
    volatile UCHAR Pad[32];
#endif
} THREAD_INFO, *PTHREAD_INFO;

//
// List head structure
//
typedef struct {
    LIST_ENTRY entry;
    ULONG64 length;
    SRWLOCK sharedLock;
    CRITICAL_SECTION lock;
    boolean lockExclusive;
} listHead, *pListHead;

//
// PFN (Page Frame Number) structure
//
typedef struct {
    LIST_ENTRY entry;
    pte *pte;
    ULONG64 diskIndex;
    ULONG64 isBeingWritten: 1;
    ULONG64 isBeingTrimmed: 1;
    CRITICAL_SECTION lock;
} pfn;




#define NUMBER_OF_TIME_STAMPS 16
#define BASELINE_HAZARD (1)
#define LOG_BASELINE_HAZARD (0)

#define DRIFT_COEFFICENT .33
#define VOLATILITY_COEFFICENT .33
#define RECENT_ACCESS_COEFFICIENT -0.0000001

typedef struct {
    ULONG64 timeStamps[NUMBER_OF_TIME_STAMPS];
    ULONG64:4, currentStamp;
    ULONG64:4, numValidStamps;


    double volatility;
    double drift;

} stochastic_data;

//
//PTE_REGION  a section of 64 ptes
//
#define NUMBER_OF_AGES 8
#define NUMBER_OF_PTES_PER_REGION 64
#define NUMBER_OF_PTE_REGIONS (NUMBER_OF_PTES/NUMBER_OF_PTES_PER_REGION)
#define LENGTH_OF_PREDICTION 10

typedef struct {
    LIST_ENTRY entry;
    stochastic_data statistics;
    CRITICAL_SECTION lock;

    ULONG64:1, accessed;

} PTE_REGION;




#endif //STRUCTURES_H
