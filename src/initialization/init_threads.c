//
// Created by nrper on 7/16/2025.
//

#include "../../include/initialization/init_threads.h"

#include <stdio.h>

#include "../../include/variables/globals.h"
#include "../../include/variables/structures.h"
#include "../../include/threads/user_thread.h"
#include "../../include/threads/trimmer_thread.h"
#include "../../include/threads/writer_thread.h"
#include "../../include/threads/zero_thread.h"
#include "../../include/vm.h"



VOID initThreads(VOID) {
    createEvents();
    initCriticalSections();
    createThreads();
}

VOID createThreads(VOID) {


    PTHREAD_INFO ThreadContext;
    HANDLE Handle;

    THREAD_INFO ThreadInfo[NUMBER_OF_THREADS] = {0};

    THREAD_INFO UserThreadInfo[NUMBER_OF_USER_THREADS] = {0};
    THREAD_INFO TrimmerThreadInfo[NUMBER_OF_TRIMMING_THREADS] = {0};
    THREAD_INFO WriterThreadInfo[NUMBER_OF_WRITING_THREADS] = {0};
    THREAD_INFO ZeroIngThreadInfo[NUMBER_OF_ZEROING_THREADS] = {0};

    ULONG maxThread = 0;
    ULONG threadHandleArrayOffset;




    for (int i = 0; i < NUMBER_OF_USER_THREADS; ++i) {
        ThreadContext = &UserThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;


        Handle = createNewThread(testVM, ThreadContext);
        ThreadContext->ThreadHandle = Handle;

        userThreadHandles[i] = Handle;

        maxThread++;
    }


    threadHandleArrayOffset = maxThread;

    for (int i = 0; i < NUMBER_OF_TRIMMING_THREADS; ++i) {
        ThreadContext = &TrimmerThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;


        Handle = createNewThread(page_trimmer, ThreadContext);


        ThreadContext->ThreadHandle = Handle;

        systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;

        maxThread++;
    }
    for (int i = 0; i < NUMBER_OF_WRITING_THREADS; ++i) {
        ThreadContext = &WriterThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;

        Handle = createNewThread(diskWriter,ThreadContext);

        ThreadContext->ThreadHandle = Handle;

        systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;

        maxThread++;
    }

    for (int i = 0; i < NUMBER_OF_ZEROING_THREADS; ++i) {
        ThreadContext = &ZeroIngThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;


        Handle = createNewThread(zeroingThread,ThreadContext);

        ThreadContext->ThreadHandle = Handle;

        systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;

        maxThread++;
    }

}
VOID createEvents(VOID) {

    userStartEvent = CreateEvent (NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    writingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    trimmingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    writingEndEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    userEndEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    zeroingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    systemShutdownEvent = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
}
VOID initCriticalSections(VOID) {
    INITIALIZE_LOCK(lockFreeList);
    INITIALIZE_LOCK(lockActiveList);
    INITIALIZE_LOCK(lockModifiedList);
    INITIALIZE_LOCK(lockStandByList);
    INITIALIZE_LOCK(lockDiskActive);
    INITIALIZE_LOCK(lockNumberOfSlots);

    lockModList = LOCK_FREE;
    lockToBeZeroedList = LOCK_FREE;

    initializePageTableLocks();

}
VOID initializePageTableLocks(VOID) {
    for (int i = 0; i < NUMBER_OF_PAGE_TABLE_LOCKS; ++i) {
        INITIALIZE_LOCK_DIRECT(pageTableLocks[i]);
    }
}



HANDLE createNewThread(LPTHREAD_START_ROUTINE ThreadFunction, PTHREAD_INFO ThreadContext) {
    HANDLE Handle;
    BOOL ReturnValue;

    Handle = CreateThread(DEFAULT_SECURITY,
                           DEFAULT_STACK_SIZE,
                           ThreadFunction,
                           ThreadContext,
                           DEFAULT_CREATION_FLAGS,
                           &ThreadContext->ThreadId);
    if (Handle == NULL) {
        ReturnValue = GetLastError ();
        printf ("could not create thread %x\n", ReturnValue);
        return NULL;
    }

    return Handle;
}