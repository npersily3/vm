//
// Created by nrper on 7/16/2025.
//

#ifndef STRUCTURES_H
#define STRUCTURES_H


#include <windows.h>
#include <stdbool.h>
#include <stddef.h>


// Debug macros
#define DBG 0
#if DBG
#define ASSERT(x) if ((x) == FALSE) DebugBreak();
#else
#define ASSERT(x)
#endif
#define SUCCESS 10

//
// Configuration constants
//
#define PAGE_SIZE                   4096
// 52 bits is max that bus accepts and if each page is 4k or 12 bits, there is 40 bits left over
#define frame_number_size           40
#define KB(x)                       ((x) *  (ULONG64)1024)
#define MB(x)                       (KB(x) * 1024)
#define GB(x)                       (MB(x) * 1024)


typedef struct {
    ULONG64 virtual_address_size;
    ULONG64 number_of_physical_pages;
    ULONG64 virtual_address_size_in_unsigned_chunks;

    ULONG64 number_of_disk_divisions;
    ULONG64 disk_size_in_bytes;
    ULONG64 disk_size_in_pages;
    ULONG64 disk_division_size_in_pages;

    ULONG64 number_of_user_threads;
    ULONG64 number_of_trimming_threads;
    ULONG64 number_of_writing_threads;
    ULONG64 number_of_threads;
    ULONG64 number_of_system_threads;

    ULONG64 size_of_transfer_va_space_in_pages;
    ULONG64 stand_by_trim_threshold;
    ULONG64 number_of_pages_to_trim_from_stand_by;
    ULONG64 number_of_free_lists;

    ULONG64 page_table_size_in_bytes;
    ULONG64 number_of_ptes;
    ULONG64 number_of_ptes_per_region;
    ULONG64 number_of_pte_regions;
    ULONG64 time_until_recall_pages;

} configuration;



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


#define container_of(ptr, type, member) \
((type *)((char *)(ptr) - offsetof(type, member)))


#define MAX_FAULTS 0xFFFFFF
#define BATCH_SIZE (512)

#define COULD_NOT_FIND_SLOT (~0ULL)
#define LIST_IS_EMPTY 0

#define REDO_FAULT TRUE
#define REDO_FREE TRUE
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


#define spinEvents 0


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
    // bit that indicates to the cpu that indicates if it has a valid frame number
    ULONG64 valid: 1;
    // 1 bit shared lock
    ULONG64 lock: 1;
    // indicates if the pte was accessed
    ULONG64 access: 1;
    // indicates if where the virtual pages contents are
    ULONG64 transition: 2;
    // the physical frame number associated with the pte
    ULONG64 frameNumber: frame_number_size;
} validPte;

typedef struct {
    // bit that indicates to the cpu that indicates if it has a invalid frame number
    ULONG64 mustBeZero: 1;
    // 1 bit shared lock
    ULONG64 lock: 1;
    // in this format these bits do not matter
    ULONG64 access: 1;
    ULONG64 transition: 2;

    // disk slot where the contents are
    ULONG64 diskIndex: frame_number_size;
} invalidPte;

#define DISK 0
#define UNASSIGNED 1
#define MODIFIED_LIST 2
#define STAND_BY_LIST 3

typedef struct {
    // in this format a pte must be unmapped
    ULONG64 mustBeZero: 1;
    // 1 bit shared lock
    ULONG64 lock: 1;
    // tracks accesses
    ULONG64 access: 1;
    // tracks if it can be rescued out of a write or trim
    ULONG64 contentsLocation: 2;
    // the frame number to retrieve the contents from
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


//
// Thread information structure
//

typedef struct {
    ULONG64 state;
    ULONG64 counter;
} THREAD_RNG_STATE;



//
// List head structure
//


//
// PFN (Page Frame Number) structure
//
typedef struct {
    LIST_ENTRY entry;
    pte *pte;
    ULONG64 diskIndex;
    ULONG64 isBeingWritten: 1;
    ULONG64 isBeingFreed: 1;
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
    ULONG64: 4, currentStamp;
    ULONG64: 4, numValidStamps;


    double volatility;
    double drift;
} stochastic_data;
typedef struct {
    SRWLOCK sharedLock;
#if DBG
    LONG64 threadId;
    LONG64 numHeldShared;

    // Debug tracking list
    LIST_ENTRY sharedHolders;
    CRITICAL_SECTION debugLock;
#endif
} sharedLock;

#if DBG
typedef struct _SHARED_HOLDER_DEBUG {
    LIST_ENTRY entry;
    ULONG64 threadId;
    ULONG64 acquireTime;
    const char* fileName;
    int lineNumber;
} SHARED_HOLDER_DEBUG;
#endif

//
//PTE_REGION  a section of 64 ptes
//
#define NUMBER_OF_AGES 8
#define LENGTH_OF_PREDICTION 10

typedef struct {
    LIST_ENTRY entry;
    stochastic_data statistics;
    sharedLock lock;

    ULONG64: 1, accessed;
} PTE_REGION;



typedef struct __declspec(align(64)) {
    LIST_ENTRY entry;
    volatile ULONG64 length;
    sharedLock sharedLock;
    volatile ULONG64 timeOfLastAccess;
    pfn page;
} listHead, *pListHead;

typedef struct {
    listHead* heads;
    volatile LONG AddIndex;
    volatile LONG RemoveIndex;
    volatile LONG64 length;

} freeList;

typedef struct {
     listHead active;
     listHead modified;
     listHead standby;
     listHead zeroed;
    freeList free;
} page_lists;
typedef struct {
    PULONG_PTR start,end;
    PVOID* userThreadTransfer;
    PVOID writing;


} va;

typedef struct {
    PVOID start;
    ULONG64 end;
    PULONG64 active;
    PULONG64 activeEnd;
    PULONG64* activeVa;
    ULONG64* number_of_open_slots;
} disk;

typedef struct {
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    pfn *start;
    pfn *end;
} pfns;

typedef struct {
    PTE_REGION* RegionsBase;
    pte* table;

} ptes;

typedef struct {

    HANDLE* workDoneThreadHandles;
    HANDLE* userThreadHandles;
    HANDLE* systemThreadHandles;


    HANDLE globalStart;
    HANDLE trimmingStart;
    HANDLE writingStart;
    HANDLE writingEnd;
    HANDLE userStart;
    HANDLE userEnd;
    HANDLE zeroingStartEvent;
    HANDLE systemShutdown;


    HANDLE physical_page_handle;
} events;

typedef struct {

    volatile ULONG64 pageWaits;
    volatile ULONG64 totalTimeWaiting;

    volatile boolean standByPruningInProgress;
} misc;

// Main components are the transfer va index,
typedef struct __declspec(align(64)) {
    ULONG ThreadNumber;
    ULONG ThreadId;
    ULONG64 TransferVaIndex;
    HANDLE ThreadHandle;
    THREAD_RNG_STATE rng;
    listHead localList;
} THREAD_INFO, *PTHREAD_INFO;


typedef struct {
    PTHREAD_INFO user;
    PTHREAD_INFO trimmer;
    PTHREAD_INFO writer;

} threadInfo;


typedef struct {
    configuration config;
    page_lists lists;
    va va;
    disk disk;
    pfns pfn;
    ptes pte;
    events events;
    misc misc;
    threadInfo threadInfo;
} state;


extern state vm;



#define useSharedLock 1

#endif //STRUCTURES_H
