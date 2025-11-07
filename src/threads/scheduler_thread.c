#include "disk/disk.h"
//
// Created by nrper on 9/21/2025.
//
#include <stdio.h>

#include "variables/structures.h"

//TODO your units are wrong here
ULONG64 getPagesPerTime(workDone work) {
    ULONG64 time = 0;
    ULONG64 pages = 0;

    for (int i = 0; i < 16; ++i) {
        time += work.timeIntervals[i];
        pages += work.numPagesProccessed[i];
    }
    if (time == 0) {
        return 0;
    }

    return pages / time;
}

DWORD scheduler_thread(LPVOID info) {
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;
    LONG isAgingInProgress;
    ULONG64 pagesLeft;
    ULONG64 localPagesConsumed;
    ULONG64 timeUntilOut;
    ULONG64 historyIndex;
    ULONG64 pageConsumptionHistory[PAGES_CONSUMED_LENGTH];
    memset(pageConsumptionHistory, 1, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);


    workDone agerWork;
    ULONG64 numActivePages;

    historyIndex = 0;
    ULONG64 pageAgeRate = 0;
    ULONG64 numToAge;
    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);

    while (TRUE) {
        Sleep(1);
        if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
            return 0;
        }

        //track history of page consumption
        localPagesConsumed = ReadULong64NoFence(&vm.pfn.pagesConsumed);
        InterlockedExchange64(&vm.pfn.pagesConsumed, 0);
        pageConsumptionHistory[historyIndex] = localPagesConsumed;
        historyIndex = (historyIndex + 1) % PAGES_CONSUMED_LENGTH;

        // basically, i am assuming I will not age in the first 16 schedule wakeups , however for the first sixteen times my numbers will be innacurate.
        ULONG64 averagePagesConsumed = 0;

        int i = 0;

        // find the average page consumption over the last 16 wakeups
        for (; i < PAGES_CONSUMED_LENGTH; i++) {
            if (pageConsumptionHistory[i] == MAXULONG64) {
                break;
            }
            averagePagesConsumed += pageConsumptionHistory[i];
        }
        if (i == 0) {
            continue;
        }
        averagePagesConsumed /= i;


        pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);

        if (averagePagesConsumed == 0) {
            continue;
        }
        timeUntilOut = (pagesLeft / averagePagesConsumed);

        agerWork = vm.threadInfo.aging->work;
        pageAgeRate = getPagesPerTime(agerWork);

        if (pageAgeRate == 0) {
            continue;
        }

        numActivePages = ReadULong64NoFence(&vm.pfn.numActivePages);
        numToAge = numActivePages * NUMBER_OF_AGES;



        // circular buffer, ages times, trim times, write times and how many pages/ptes aged/trimmed/written.
        // for ages, figure out how long until you are almost out, then divide by NUM_AGES
        // keep track of page consuption in circular buffer.



        if ((double) pagesLeft / vm.config.number_of_physical_pages < 0.8) {
            isAgingInProgress = InterlockedCompareExchange((volatile LONG *) &vm.misc.agingInProgress, TRUE, FALSE);

            if (isAgingInProgress == FALSE) {

                InterlockedExchange64(&vm.pte.numToAge, vm.config.number_of_physical_pages / 3);
                SetEvent(vm.events.agerStart);
            }
        }



    }
}
