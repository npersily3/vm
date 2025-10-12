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

    while (TRUE) {
        if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
            return 0;
        }

        ULONG64 pagesLeft;

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
