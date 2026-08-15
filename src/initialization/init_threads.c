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
#include "threads/ager_thread.h"
#include "utils/random_utils.h"
#include "threads/scheduler_thread.h"


VOID initThreads(VOID) {
    createEvents();
    initCriticalSections();
    createThreads();
}

VOID createThreads(VOID) {
    PTHREAD_INFO ThreadContext;
    HANDLE Handle;


    PTHREAD_INFO UserThreadInfo = init_memory(vm.config.number_of_user_threads * sizeof(THREAD_INFO));
    PTHREAD_INFO TrimmerThreadInfo = init_memory(vm.config.number_of_trimming_threads * sizeof(THREAD_INFO));
    PTHREAD_INFO WriterThreadInfo = init_memory(vm.config.number_of_writing_threads * sizeof(THREAD_INFO));
    PTHREAD_INFO AgingThreadInfo = init_memory(vm.config.number_of_aging_threads * sizeof(THREAD_INFO));
    PTHREAD_INFO SchedulerThreadInfo = init_memory(vm.config.number_of_scheduler_threads * sizeof(THREAD_INFO));

    ULONG maxThread = 0;
    ULONG threadHandleArrayOffset;


    vm.events.userThreadHandles = init_memory(sizeof(HANDLE) * vm.config.number_of_user_threads);

    for (int i = 0; i < vm.config.number_of_user_threads; ++i) {
        ThreadContext = &UserThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->TransferVaIndex = 0;
        init_list_head(&ThreadContext->localList);

        // split what the system can back evenly, then convert to whole leaf regions -- the unit the
        // thread tracks and frees in. A config too small to give a thread one region still gets one,
        // otherwise it can never commit anything at all
        ThreadContext->commitLimitInPages = vm.config.max_commit_size_in_pages / vm.config.number_of_user_threads;
        ThreadContext->regionBudget = ThreadContext->commitLimitInPages / vm.config.pte_entries_per_pagetable;

        if (ThreadContext->regionBudget == 0) {
            ThreadContext->regionBudget = 1;
        }

        ThreadContext->visitedRegions = init_memory(ThreadContext->regionBudget * sizeof(ULONG64));
        ThreadContext->visitedHead = 0;
        ThreadContext->visitedCount = 0;

        Handle = createNewThread(testVM, ThreadContext);
        ThreadContext->ThreadHandle = Handle;

        vm.threadInfo.user = UserThreadInfo;
        vm.events.userThreadHandles[i] = Handle;

        maxThread++;
    }


    threadHandleArrayOffset = maxThread;

    vm.events.systemThreadHandles = init_memory(sizeof(HANDLE) * vm.config.number_of_system_threads);

    for (int i = 0; i < vm.config.number_of_trimming_threads; ++i) {
        ThreadContext = &TrimmerThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;


        Handle = createNewThread(page_trimmer, ThreadContext);


        ThreadContext->ThreadHandle = Handle;

        vm.events.systemThreadHandles[maxThread - threadHandleArrayOffset] = Handle;

        maxThread++;
    }
    vm.threadInfo.trimmer = TrimmerThreadInfo;

    for (int i = 0; i < vm.config.number_of_writing_threads; ++i) {
        ThreadContext = &WriterThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->TransferVaIndex = 0;

        Handle = createNewThread(diskWriter, ThreadContext);

        ThreadContext->ThreadHandle = Handle;

        vm.events.systemThreadHandles[maxThread - threadHandleArrayOffset] = Handle;

        maxThread++;
    }
    vm.threadInfo.writer = WriterThreadInfo;

    for (int i = 0; i < vm.config.number_of_aging_threads; ++i) {
        ThreadContext = &AgingThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->TransferVaIndex = 0;

        Handle = createNewThread(ager_thread, ThreadContext);

        ThreadContext->ThreadHandle = Handle;

        vm.events.systemThreadHandles[maxThread - threadHandleArrayOffset] = Handle;

        maxThread++;
    }
    vm.threadInfo.aging = AgingThreadInfo;

    for (int i = 0; i < vm.config.number_of_scheduler_threads; ++i) {
        ThreadContext = &SchedulerThreadInfo[i];
        ThreadContext->ThreadNumber = maxThread;
        ThreadContext->TransferVaIndex = 0;

        Handle = createNewThread(scheduler_thread, ThreadContext);

        ThreadContext->ThreadHandle = Handle;

        vm.events.systemThreadHandles[maxThread - threadHandleArrayOffset] = Handle;

        maxThread++;
    }
    vm.threadInfo.scheduler = SchedulerThreadInfo;

    vm.misc.agingInProgress = FALSE;
}

VOID createEvents(VOID) {
    vm.events.userStart = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    vm.events.writingStart = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    vm.events.trimmingStart = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    vm.events.writingEnd = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
    vm.events.agerStart = CreateEvent(NULL, AUTO_RESET, EVENT_START_OFF, NULL);
    vm.events.systemShutdown = CreateEvent(NULL, MANUAL_RESET, EVENT_START_OFF, NULL);
}

VOID initCriticalSections(VOID) {
    //initializePageTableLocks();
}

VOID initializePageTableLocks(VOID) {
    //    PTE_REGION* p;
    return;
    // p = vm.pte.RegionsBase;
    // for (int i = 0; i < vm.config.number_of_pte_regions; ++i) {
    //     InitializeSRWLock(&p->lock.sharedLock);
    //     p++;
    // }
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
        ReturnValue = GetLastError();
        printf("could not create thread %x\n", ReturnValue);
        return NULL;
    }

    return Handle;
}
