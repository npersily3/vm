#include "disk/disk.h"
//
// Created by nrper on 9/21/2025.
//
#include <stdio.h>

#include "variables/structures.h"


ULONG64 historyIndex;
ULONG64 pageConsumptionHistory[PAGES_CONSUMED_LENGTH];

DWORD scheduler_thread(LPVOID info) {
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;
    LONG isAgingInProgress;
    ULONG64 pagesLeft;
    ULONG64 localPagesConsumed;

    historyIndex = 0;

    while (TRUE) {
        if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
            return 0;
        }

        localPagesConsumed = ReadULong64NoFence(&vm.pfn.pagesConsumed);
        InterlockedExchange64(&vm.pfn.pagesConsumed, 0);
        pageConsumptionHistory[historyIndex] = localPagesConsumed;
        historyIndex = (historyIndex + 1) % PAGES_CONSUMED_LENGTH;

        pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);

        // circular buffer, ages times, trim times, write times and how many pages/ptes aged/trimmed/written.
        // for ages, figure out how long until you are almost out, then divide by NUM_AGES
        // keep track of page consuption in circular buffer.



        if ((double) pagesLeft / vm.config.number_of_physical_pages < 0.5) {
            isAgingInProgress = InterlockedCompareExchange((volatile LONG *) &vm.misc.agingInProgress, TRUE, FALSE);

            if (isAgingInProgress == FALSE) {

                InterlockedExchange64(&vm.pte.numToAge, vm.config.number_of_physical_pages / 3);
                SetEvent(vm.events.agerStart);
            }
        }


        Sleep(1);
    }
}
