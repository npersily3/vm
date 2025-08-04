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
#include "initialization/init.h"
#include "threads/statistics.h"
#include "utils/random_utils.h"



VOID initThreads(VOID) {
    createEvents();
    initCriticalSections();
    createThreads();
}

VOID createThreads(VOID) {


    PTHREAD_INFO ThreadContext;
    HANDLE Handle;


    PTHREAD_INFO ThreadInfo = init_memory(sizeof(THREAD_INFO) * vm.config.number_of_threads);

    PTHREAD_INFO UserThreadInfo = init_memory(vm.config.number_of_user_threads * sizeof(THREAD_INFO));
    PTHREAD_INFO TrimmerThreadInfo = init_memory(vm.config.number_of_trimming_threads * sizeof(THREAD_INFO));
    PTHREAD_INFO WriterThreadInfo =  init_memory(vm.config.number_of_writing_threads * sizeof(THREAD_INFO));
   // THREAD_INFO ZeroIngThreadInfo[NUMBER_OF_ZEROING_THREADS] = {0};
   // THREAD_INFO SchedulerThreadInfo[NUMBER_OF_SCHEDULING_THREADS] = {0};

    ULONG maxThread = 0;
    ULONG threadHandleArrayOffset;



    vm.events.userThreadHandles = init_memory(sizeof(HANDLE) * vm.config.number_of_user_threads);

    for (int i = 0; i < vm.config.number_of_user_threads; ++i) {
        ThreadContext = &UserThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->TransferVaIndex = 0;


        Handle = createNewThread(testVM, ThreadContext);
        ThreadContext->ThreadHandle = Handle;


        vm.events.userThreadHandles[i] = Handle;

        maxThread++;
    }


    threadHandleArrayOffset = maxThread;

    vm.events.systemThreadHandles = init_memory(sizeof(HANDLE) * vm.config.number_of_system_threads);

    for (int i = 0; i < vm.config.number_of_trimming_threads ; ++i) {
        ThreadContext = &TrimmerThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;


        Handle = createNewThread(page_trimmer, ThreadContext);


        ThreadContext->ThreadHandle = Handle;

        vm.events.systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;

        maxThread++;
    }
    for (int i = 0; i < vm.config.number_of_writing_threads; ++i) {
        ThreadContext = &WriterThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;

        Handle = createNewThread(diskWriter,ThreadContext);

        ThreadContext->ThreadHandle = Handle;

        vm.events.systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;

        maxThread++;
    }

    // for (int i = 0; i < NUMBER_OF_ZEROING_THREADS; ++i) {
    //     ThreadContext = &ZeroIngThreadInfo[i];
    //     ThreadContext->ThreadNumber = maxThread;
    //
    //
    //     Handle = createNewThread(zeroingThread,ThreadContext);
    //
    //     ThreadContext->ThreadHandle = Handle;
    //
    //     systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;
    //
    //     maxThread++;
    // }
    //
    // for (int i = 0; i < NUMBER_OF_SCHEDULING_THREADS; ++i) {
    //     ThreadContext = &SchedulerThreadInfo[i];
    //     ThreadContext->ThreadNumber = maxThread;
    //
    //
    //     Handle = createNewThread(recordProbability,ThreadContext);
    //
    //     ThreadContext->ThreadHandle = Handle;
    //
    //     systemThreadHandles[maxThread-threadHandleArrayOffset] = Handle;
    //
    //     maxThread++;
    // }

}
VOID createEvents(VOID) {
    vm.events.userStart = CreateEvent (NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    vm.events.writingStart = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    vm.events.trimmingStart = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    vm.events.writingEnd = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    vm.events.userEnd = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    //  zeroingStartEvent = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    vm.events.systemShutdown = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
}
VOID initCriticalSections(VOID) {



    initializePageTableLocks();

}

VOID initializePageTableLocks(VOID) {
    PTE_REGION* p;

    p = vm.pte.RegionsBase;
    for (int i = 0; i < vm.config.number_of_pte_regions; ++i) {
        INITIALIZE_LOCK_DIRECT(p->lock);
        p++;
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