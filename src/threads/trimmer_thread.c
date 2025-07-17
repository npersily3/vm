//
// Created by nrper on 7/16/2025.
//

#include "../../include/threads/trimmer_thread.h"

#include <stdio.h>

#include "../../include/variables/structures.h"
#include "../../include/variables/globals.h"
#include "../../include/variables/macros.h"
#include "../../include/utils/pte_utils.h"
#include "../../include/utils/page_utils.h"
#include "../../include/utils/thread_utils.h"


DWORD page_trimmer(LPVOID threadContext) {
    ULONG64 BatchIndex;
    BOOL doubleBreak;
    PCRITICAL_SECTION trimmedPageTableLock;
    pfn* page;
    pfn* pages[BATCH_SIZE];
    ULONG64 virtualAddresses[BATCH_SIZE];

    HANDLE events[2];
    DWORD returnEvent;
    events[0] = trimmingStartEvent;
    events[1] = systemShutdownEvent;


    while (TRUE) {
        BatchIndex = 0;
        doubleBreak = FALSE;
        trimmedPageTableLock = NULL;

        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

        // while there is still stuff to trim and the arrays are not at capacity
        while (headModifiedList.length < NUMBER_OF_PHYSICAL_PAGES / 4 && BatchIndex < BATCH_SIZE) {
            page = getActivePage();

            if (page == NULL) {
                if (BatchIndex == 0) {
                    doubleBreak = TRUE;
                }
                break;
            }

            // if we are not on a streak
            if (trimmedPageTableLock == NULL) {

                trimmedPageTableLock = getPageTableLock(pages[BatchIndex]->pte);
                EnterCriticalSection(trimmedPageTableLock);
            }


            virtualAddresses[BatchIndex] = (ULONG64) pte_to_va(pages[BatchIndex]->pte);
            pages[BatchIndex]->pte->transitionFormat.mustBeZero = 0;
            pages[BatchIndex]->pte->transitionFormat.contentsLocation = MODIFIED_LIST;
            BatchIndex++;

            if (isNextPageInSameRegion(trimmedPageTableLock) == FALSE) {
                unmapBatch(virtualAddresses, BatchIndex);
                addBatchToModifiedList(pages, BatchIndex);

                LeaveCriticalSection(trimmedPageTableLock);
                trimmedPageTableLock = NULL;
                BatchIndex = 0;

            }
        }
        if (doubleBreak == TRUE) {
            SetEvent(writingStartEvent);
            continue;
        }
        if (BatchIndex == 0) {
            SetEvent(writingStartEvent);
            continue;
        }


        unmapBatch(virtualAddresses, BatchIndex);
        addBatchToModifiedList(pages, BatchIndex);

        LeaveCriticalSection(trimmedPageTableLock);

        //nptodo add a condition around this
        SetEvent(writingStartEvent);


    }
}

pfn* getActivePage(VOID) {

    PLIST_ENTRY trimmedEntry;
    pfn* page;

    EnterCriticalSection(lockActiveList);
    trimmedEntry = headActiveList.entry.Flink;

    if (trimmedEntry == &headActiveList.entry) {

        LeaveCriticalSection(lockActiveList);

        return NULL;
    }
    ASSERT(trimmedEntry != &headActiveList.entry);

    page = container_of(trimmedEntry, pfn, entry);
    removeFromMiddleOfList(&headActiveList, trimmedEntry);
    LeaveCriticalSection(lockActiveList);

    return page;
}

BOOL isNextPageInSameRegion(PCRITICAL_SECTION trimmedPageTableLock) {

    pfn* nextPage;
    PCRITICAL_SECTION nextPageTableLock;

    EnterCriticalSection(lockActiveList);
    nextPage = container_of(headActiveList.entry.Flink, pfn, entry);
    LeaveCriticalSection(lockActiveList);

    nextPageTableLock = getPageTableLock(nextPage->pte);

    return nextPageTableLock == trimmedPageTableLock;
}
VOID unmapBatch (PULONG64 virtualAddresses, ULONG64 batchSize) {

    if (MapUserPhysicalPagesScatter(virtualAddresses, batchSize, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
        return;
    }
}

VOID addBatchToModifiedList (pfn* pages, ULONG64 batchSize) {

    pfn* page;

    acquireLock(&lockModList);
    for (int i = 0; i < batchSize; ++i) {
        page = &pages[i];

        InsertTailList(&headModifiedList, &page->entry);

    }
    releaseLock(&lockModList);
}
