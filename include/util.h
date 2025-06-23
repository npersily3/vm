#ifndef UTIL_H
#define UTIL_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
//
// Configuration constants
//
#define PAGE_SIZE                   4096
#define frame_number_size           40
#define MB(x)                       ((x) * 1024 * 1024)
#define VIRTUAL_ADDRESS_SIZE        MB(16)
#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)
#define NUMBER_OF_DISK_DIVISIONS   8
#define DISK_SIZE_IN_BYTES         (VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES + PAGE_SIZE)
#define DISK_SIZE_IN_PAGES         (DISK_SIZE_IN_BYTES / PAGE_SIZE)
#define DISK_DIVISION_SIZE_IN_PAGES (DISK_SIZE_IN_PAGES / NUMBER_OF_DISK_DIVISIONS)
#define EMPTY_PTE                  0xFFFFFFFFFF
#define NUMBER_OF_THREADS 2
#define AUTO_RESET              FALSE
#define MANUAL_RESET            TRUE
#define WAIT_FOR_ALL            TRUE
#define WAIT_FOR_ONE            FALSE
#define DEFAULT_SECURITY        ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE      0
#define DEFAULT_CREATION_FLAGS  0


#define ROUND_DOWN_TO_PAGE(addr) ((ULONG_PTR)(addr) & ~(PAGE_SIZE - 1))
#define ROUND_UP_TO_PAGE(addr) (((ULONG_PTR)(addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))


// List operation constants
#define REMOVE_FREE_PAGE           FALSE
#define REMOVE_ACTIVE_PAGE         TRUE

// Debug macros
#define DBG 1
#if DBG
#define ASSERT(x) if ((x) == FALSE) DebugBreak();
#else
#define ASSERT(x)
#endif

#define container_of(ptr, type, member) \
((type *)((char *)(ptr) - offsetof(type, member)))

//
// Data structures
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

typedef struct {
    LIST_ENTRY entry;
    pte *pte;
   ULONG64 diskIndex;
} pfn;

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


//
// Global variables (declared here, defined in init.c)
//
extern LIST_ENTRY headFreeList;
extern LIST_ENTRY headActiveList;
extern LIST_ENTRY headModifiedList;
extern LIST_ENTRY headStandByList;


extern pte *pageTable;
extern pfn *pfnStart;
extern pfn *endPFN;
extern PULONG_PTR vaStart;
extern PVOID transferVa;
extern ULONG_PTR physical_page_count;
extern PULONG_PTR physical_page_numbers;
extern HANDLE  workDoneThreadHandles[NUMBER_OF_THREADS];

//
// Utility function declarations
//

VOID listAdd(pfn* pfn, boolean active);
pfn* listRemove(boolean active);
pte* va_to_pte(PVOID va);
PVOID pte_to_va(pte* pte);
PVOID init_memory(ULONG64 numBytes);
VOID pfnInbounds(pfn* trimmed);
ULONG64 getFrameNumber(pfn* pfn);
pfn* getPFNfromFrameNumber(ULONG64 frameNumber);
VOID removeFromMiddleOfList(LIST_ENTRY* entry) ;

#endif // UTIL_H