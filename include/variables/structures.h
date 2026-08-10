//
// Created by nrper on 7/16/2025.
//

#ifndef STRUCTURES_H
#define STRUCTURES_H


#include <windows.h>
#include <stdbool.h>
#include <stddef.h>


// Debug macros
#define         DBG 0
#define DBG_DISK 0


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

#define PAGE_ALIGN(x) (x & (~(PAGE_SIZE - 1)))


// 52 bits is max that address bus accepts and if each page is 4k or 12 bits, there is 40 bits left over
#define frame_number_size           40
#define KB(x)                       ((x) *  (ULONG64)1024)
#define MB(x)                       (KB(x) * 1024)
#define GB(x)                       (MB(x) * 1024)


typedef struct {
    ULONG64 number_of_page_table_layers;
    //number of pages able to commit (total resources - pt size)
    ULONG64 max_commit_size_in_pages;

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
    ULONG64 number_of_aging_threads;
    ULONG64 number_of_scheduler_threads;

    ULONG64 number_of_threads;
    ULONG64 number_of_system_threads;

    ULONG64 size_of_user_thread_transfer_va_space_in_pages;
    ULONG64 stand_by_trim_threshold;
    ULONG64 number_of_pages_to_trim_from_stand_by;
    ULONG64 number_of_free_lists;

    // this is in pagetable increments
    ULONG64 cumulative_number_of_page_tables;

    // always 512 in our case
    ULONG64 pte_entries_per_pagetable;


    ULONG64 number_of_leaf_ptes;
    ULONG64 number_of_leaf_pagetables;
    ULONG64 time_until_recall_pages;

    MEM_EXTENDED_PARAMETER parameter;



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


#define NUM_WRITING_BATCHES (128)

#define SCHEDULER_PERIOD_MS (200)

#define COULD_NOT_FIND_SLOT (~0ULL)
#define LIST_IS_EMPTY 0

#define REDO_FAULT 0
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
    ULONG64 isTransition: 1;
    // the physical frame number associated with the pte
    ULONG64 age: 3;


    ULONG64 frameNumber: frame_number_size;
} validPte;

typedef struct {
    // bit that indicates to the cpu that indicates if it has a invalid frame number
    ULONG64 mustBeZero: 1;
    // 1 bit shared lock
    ULONG64 lock: 1;
    // in this format these bits do not matter
    ULONG64 access: 1;
    ULONG64 isTransition: 1;

    ULONG64 age: 3;
    // disk slot where the contents are
    ULONG64 diskIndex: frame_number_size;
} invalidPte;



typedef struct {
    // in this format a pte must be unmapped
    ULONG64 mustBeZero: 1;
    // 1 bit shared lock
    ULONG64 lock: 1;
    // tracks accesses
    ULONG64 access: 1;
    // tracks if it can be rescued out of a write or trim
    ULONG64 isTransition: 1;
    // the frame number to retrieve the contents from
    ULONG64 age: 3;
    ULONG64 frameNumber: frame_number_size;
} transitionPte;

// this is for all ptes who point at pagetables. I assume there is some funky stuff with the lock bit and access bit
typedef struct {
    // in this format a pte must be unmapped
    ULONG64 mustBeZero: 1;
    // 1 bit shared lock
    ULONG64 lock: 1;
    // tracks accesses
    ULONG64 access: 1;
    // tracks if it can be rescued out of a write or trim
    ULONG64 isTransition: 1;
    // the frame number to retrieve the contents from
    ULONG64 age: 3;
    ULONG64 frameNumber: frame_number_size;
} branchPte;

typedef struct {
    union {
        validPte validFormat;
        invalidPte invalidFormat;
        transitionPte transitionFormat;
        branchPte branchFormat;
        ULONG64 entireFormat;
    };
} pte;


typedef struct {
    pte pagetable [PAGE_SIZE/sizeof(pte)];

} PAGETABLE, *PPAGETABLE;

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
#define MODIFIED_LIST 0
#define STAND_BY_LIST 1
typedef struct {
    LIST_ENTRY entry;
    pte *pte;
    ULONG64 diskIndex;

    ULONG64 valid_transition_count;

    ULONG64 location: 1;
    ULONG64 isBeingWritten: 1;
    ULONG64 isBeingFreed: 1;
#if DBG
    ULONG64 hasBeenRescuedWhileWritten: 1;
    ULONG64 lockedDuringPrune: 1;
#endif
    CRITICAL_SECTION lock;

}pfn;


// circular buffer length of scheduler thread page consumption
#define PAGES_CONSUMED_LENGTH 16

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
#define MAX_AGE (NUMBER_OF_AGES - 1)
#define LENGTH_OF_PREDICTION 10

#define NOT_ON_LIST (-1)
//TODO after new changes this only has to be the size of two pointers
typedef struct {
    LIST_ENTRY entry;
    stochastic_data statistics;


}  PTE_REGION;


// all this the lists will be next to each other in global memory therefore we should cache allign them so there is not
// a huge bus slowdown.
typedef struct __declspec(align(64)) {
    LIST_ENTRY entry;
    volatile ULONG64 length;
    sharedLock sharedLock;
    pfn page;
    PTE_REGION region;
} listHead, *pListHead;


//this is the structure containg a dimensioned freelist.
typedef struct {
    listHead* heads;
    volatile ULONG64 length;

} freeList;

typedef struct {

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
    pte** activeVa;
    ULONG64* number_of_open_slots;
} disk;


#if DBG
#define FRAMES_TO_CAPTURE 17
typedef  struct __declspec(align(128)){

    pte* pteAddress;
    pte oldPteContents;
    pte pteContents;

    pfn pfnContents;
    ULONG64 frameNumber;
    PVOID stacktrace[FRAMES_TO_CAPTURE];
    ULONG64 threadId;

} debugPTE;


typedef struct __declspec(align(256)){

    pfn* pfnAddress;
    pfn oldContents;
    pfn newContents;



    PVOID stacktrace[FRAMES_TO_CAPTURE];
    ULONG64 threadId;

} debugPFN;


#define DEBUG_PTE_CIRCULAR_BUFFER_SIZE 0x80000
#define DEBUG_PFN_CIRCULAR_BUFFER_SIZE 0x80000

#endif



typedef struct {
    volatile ULONG64 pagesConsumed;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    pfn *start;
    pfn *end;
    volatile ULONG64 numActivePages;
#if DBG

    volatile ULONG64 debugBufferIndex;
    debugPFN* debugBuffer;
#endif


} pfns;
// padded to a full cache line so incrementing/decrementing one age bucket doesn't
// false-share the line with the other ages, which are hit by every user/ager/trimmer thread
typedef struct {
    volatile LONG64 count;
    char _padding[64 - sizeof(LONG64)];
} AGE_COUNTER;

typedef struct {
    PTE_REGION* regions_base;
    PPAGETABLE table;
    PPAGETABLE* start_of_layer;
    listHead ageList[NUMBER_OF_AGES];
    AGE_COUNTER globalNumOfAge[NUMBER_OF_AGES];
    volatile ULONG64 numToAge;
    volatile ULONG64 numToTrim;
    volatile ULONG64 numToWrite;
    CRITICAL_SECTION rootLock;

#if DBG
    volatile ULONG64 debugBufferIndex;
    debugPTE* debugBuffer;
#endif

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
    HANDLE agerStart;
    HANDLE userEnd;
    HANDLE zeroingStartEvent;
    HANDLE systemShutdown;


    HANDLE physical_page_handle;
} events;

typedef struct {

    volatile ULONG64 pageWaits;
    volatile ULONG64 totalTimeWaiting;

    volatile boolean standByPruningInProgress;
    volatile boolean agingInProgress;
    volatile ULONG64 numTrims;
    volatile ULONG64 numWrites;
    volatile ULONG64 pagesFromStandBy;
    volatile ULONG64 pagesFromFree;
    volatile ULONG64 pagesFromLocalCache;
    volatile ULONG64 numRescues;
    volatile LONG64 trimmerPending;
} misc;

typedef struct {
    ULONG64 timeIntervals[16];
    ULONG64 numPagesProccessed[16];
    ULONG64 index;
} workDone;


//
// Statistics instrumentation.
//
// Flip STATS to 0 and every STAT_* macro (utils/stats.h) compiles to nothing, the per-thread
// counters vanish out of THREAD_INFO, and printStats falls back to the bare summary built from the
// vm.misc counters that exist either way. Nothing is conditional at runtime.
//
// Counters live per-thread rather than in vm.misc precisely so recording is a plain non-interlocked
// increment on a private cache line -- THREAD_INFO is already 512-byte aligned, so no false sharing
// and no bus traffic. They are summed once, at exit, in printStats.
//
#define STATS 1

// Deepest tree we index per-layer stats for. A deeper config folds its bottom layers into the last
// slot rather than growing the struct; nothing here is correctness-relevant.
#define STATS_MAX_LAYERS 8

// Log2 buckets shared by every histogram (batch sizes, wait latencies). Bucket 0 is exactly zero,
// bucket i > 0 covers [2^(i-1), 2^i). 20 buckets reaches ~500k, past both BATCH_SIZE and any
// plausible QPC wait.
#define STATS_BUCKETS 20

// The three shapes a rescue can take. They cost wildly different amounts, so numRescues alone hides
// the interesting part.
#define RESCUE_IN_FLIGHT 0
#define RESCUE_STANDBY   1
#define RESCUE_MODIFIED  2
#define RESCUE_KINDS     3

// The four mutually exclusive things the ager can do to a pte, counted per layer. AGER_PRUNED is the
// subtree skip -- the whole point of the tree comb, and the one number that says whether it pays.
#define AGER_LEAF_REGION  0
#define AGER_PRUNED       1
#define AGER_DESCENDED    2
#define AGER_AGED_INPLACE 3
#define AGER_BRANCHES     4

#if STATS
// INVARIANT: every member is ULONG64. printStats sums these by walking the struct as a flat ULONG64
// array, so a member of any other type would be summed wrongly.
typedef struct {
    // -- user threads: faults --
    ULONG64 faults;
    ULONG64 redoFaults;
    ULONG64 rescues[RESCUE_KINDS];
    ULONG64 firstTouch;
    ULONG64 diskReads;

    // -- user threads: page waits (QPC ticks; converted at print time) --
    ULONG64 pageWaits;
    ULONG64 waitTicks;
    ULONG64 maxWaitTicks;
    ULONG64 waitHist[STATS_BUCKETS];

    // -- user threads: multilevel walk cost --
    ULONG64 walkPTsMaterialized[STATS_MAX_LAYERS + 1];

    // -- user threads: free list contention --
    ULONG64 freeListSearches;
    ULONG64 freeListProbes;

    // -- trimmer --
    ULONG64 trimBatchSum;
    ULONG64 trimBatchCount;
    ULONG64 trimEmptyRegions;
    ULONG64 trimBatchHist[STATS_BUCKETS];
    ULONG64 trimmedAge[NUMBER_OF_AGES];
    ULONG64 trimSkippedAccessed;
    ULONG64 victimFromAge[NUMBER_OF_AGES];
    ULONG64 victimNotFound;
    ULONG64 quotaMet;
    ULONG64 quotaShort;
    ULONG64 recalled;
    ULONG64 recallWakeups;

    // -- writer --
    ULONG64 writeBatchSum;
    ULONG64 writeBatchCount;
    ULONG64 writeBatchHist[STATS_BUCKETS];
    ULONG64 diskFull;
    ULONG64 modifiedEmpty;

    // -- ager --
    ULONG64 agerWakeups;
    ULONG64 agerPTEsAged;
    ULONG64 agerBranch[STATS_MAX_LAYERS][AGER_BRANCHES];

    // -- list locks: how often the trylock dance fell back to SRW exclusive --
    ULONG64 listShared;
    ULONG64 listFallback;

    // -- standby prune --
    ULONG64 pruneWakeups;
    ULONG64 pruneRequested;
    ULONG64 pruneObtained;

    // -- scheduler: the quotas it handed out, and how much slack it had when it did --
    ULONG64 schedWakeups;
    ULONG64 schedCalibrating;
    ULONG64 schedNoHistory;
    ULONG64 schedBehind;
    ULONG64 schedConsumedSum;
    ULONG64 schedConsumedCount;
    ULONG64 schedConsumedHist[STATS_BUCKETS];
    ULONG64 schedHeadroomSum;
    ULONG64 schedHeadroomCount;
    ULONG64 schedHeadroomHist[STATS_BUCKETS];
    ULONG64 schedAgeSum;
    ULONG64 schedAgeCount;
    ULONG64 schedAgeHist[STATS_BUCKETS];
    ULONG64 schedTrimSum;
    ULONG64 schedTrimCount;
    ULONG64 schedTrimHist[STATS_BUCKETS];
    ULONG64 schedAgeRateSum;
    ULONG64 schedTrimRateSum;
    ULONG64 schedWriteRateSum;
    ULONG64 schedRoundsToAge[NUMBER_OF_AGES + 1];
} statistics;
#endif


typedef struct __declspec(align(512)) {
    ULONG ThreadNumber;
    ULONG ThreadId;
    ULONG64 TransferVaIndex;
    HANDLE ThreadHandle;
    THREAD_RNG_STATE rng;
    listHead localList;
    workDone work;

#if STATS
    statistics stats;
#endif

#if DBG
    ULONG64 pagelocksHeld;
    ULONG64 regionlocksHeld;
    debugPFN pagelockIndices[512];
#endif

} THREAD_INFO, *PTHREAD_INFO;


typedef struct {
    PTHREAD_INFO user;
    PTHREAD_INFO trimmer;
    PTHREAD_INFO writer;
    PTHREAD_INFO aging;
    PTHREAD_INFO scheduler;

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

//this is for the mode where I want to ensure correction at the expense of perf
#define CORRECTNESS 0
#define CORRECTNESS_SIZE 500

#define useSharedLock 1

#endif //STRUCTURES_H
