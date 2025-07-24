//
// Created by nrper on 7/17/2025.
//

#include "../../include/threads/zero_thread.h"

#include <stdio.h>


#include "../../include/variables/globals.h"
#include "../../include/variables/structures.h"
#include "../../include/variables/macros.h"

#include "utils/page_utils.h"
#include "utils/thread_utils.h"

DWORD zeroingThread (LPVOID threadContext) {

    PTHREAD_INFO thread_info;
    pfn* page[BATCH_SIZE];
    ULONG64 frameNumbers[BATCH_SIZE];


    thread_info = (PTHREAD_INFO) threadContext;

    HANDLE* events[2];
    DWORD returnEvent;
    events[0] = zeroingStartEvent;
    events[1] = systemShutdownEvent;


    while (TRUE) {

        returnEvent = WaitForMultipleObjects(2,events, FALSE, INFINITE);

        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }



        //nptodo If you ever expand to more threads of this type, there is a race condition where batch size could change
        for (int i = 0; i < BATCH_SIZE; ++i) {
            acquireLock(&lockToBeZeroedList);
            page[i] = container_of(RemoveHeadList(&headToBeZeroedList), pfn, entry);
            releaseLock(&lockToBeZeroedList);
            frameNumbers[i] = getFrameNumber(page[i]);
        }
        zeroMultiplePages(frameNumbers, BATCH_SIZE);

        AcquireSRWLockExclusive(&headFreeList.sharedLock);
        for (int i = 0; i < BATCH_SIZE; ++i) {
            InsertTailList(&headFreeList, &page[i]->entry);
        }
        ReleaseSRWLockExclusive(&headFreeList.sharedLock);
    }
}


BOOL zeroMultiplePages (PULONG64 frameNumbers, ULONG64 batchSize) {



    if (MapUserPhysicalPages(zeroThreadTransferVa, batchSize, frameNumbers) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", zeroThreadTransferVa, frameNumbers[0]);
        return FALSE;
    }
    memset(zeroThreadTransferVa, 0, PAGE_SIZE);

    if (MapUserPhysicalPages(zeroThreadTransferVa, batchSize, NULL) == FALSE) {
        DebugBreak();
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n",  zeroThreadTransferVa, frameNumbers[0]);
        return FALSE;
    }

    return TRUE;
}