#include "disk/disk.h"
//
// Created by nrper on 9/21/2025.
//
#include <stdio.h>

#include "variables/structures.h"




DWORD scheduler_thread(LPVOID info) {
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;
    LONG isAgingInProgress;
    ULONG64 pagesLeft;
    ULONG64 localPagesConsumed;
    ULONG64 timeUntilOut;
    ULONG64 historyIndex;
    ULONG64 pageConsumptionHistory[PAGES_CONSUMED_LENGTH];

    historyIndex = 0;
    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);

    while (TRUE) {
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
        for (int i = 0; i < PAGES_CONSUMED_LENGTH; i++) {
            averagePagesConsumed += pageConsumptionHistory[i];
        }
        averagePagesConsumed /= PAGES_CONSUMED_LENGTH;
        averagePagesConsumed += 1;

        pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);

        timeUntilOut = (pagesLeft / averagePagesConsumed);

        printf("time until out %llu \n", timeUntilOut);

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


        Sleep(1);
    }
}
