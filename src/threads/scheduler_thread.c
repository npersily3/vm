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

#define MS_TO_100NS (10000)

DWORD scheduler_thread(LPVOID info) {
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;
    LONG isAgingInProgress;
    ULONG64 pagesLeft;
    ULONG64 localPagesConsumedPerWakeUp;
    ULONG64 timeUntilOutIn100ns;
    ULONG64 historyIndex;
    ULONG64 pageConsumptionHistory[PAGES_CONSUMED_LENGTH];
    memset(pageConsumptionHistory, 1, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);

    ULONG64 timeToMakePagesAvailable;

    workDone agerWork;
    workDone trimmerWork;
    workDone writerWork;

    ULONG64 numActivePages;

    historyIndex = 0;

    ULONG64 pageAgeRate;
    ULONG64 pageTrimRate;
    ULONG64 pageWriteRate;


    ULONG64 numToAgeTotal;
    ULONG64 numToAgeThisWakeup;

    ULONG64 numToTrimTotal;
    ULONG64 numToTrimThisWakeup;

    ULONG64 numToWriteTotal;
    ULONG64 numToWriteThisWakeup;

    ULONG64 timeToAge;
    ULONG64 perfectTimeToAge;
    ULONG64 timeToTrim;
    ULONG64 timeToWrite;


    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);


    ULONG64 averagePagesConsumedPerWakeup = 0;
    ULONG64 averagePagesConsumedPerWakupIn100Ns = 0;

    // Wait a bit until the system has good data
    Sleep(1000);


    while (TRUE) {
        Sleep(1);
        if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
            return 0;
        }

        //track history of page consumption
        localPagesConsumedPerWakeUp = ReadULong64NoFence(&vm.pfn.pagesConsumed);
        InterlockedExchange64(&vm.pfn.pagesConsumed, 0);
        pageConsumptionHistory[historyIndex] = localPagesConsumedPerWakeUp;
        historyIndex = (historyIndex + 1) % PAGES_CONSUMED_LENGTH;


        averagePagesConsumedPerWakeup = 0;

        int i = 0;

        // find the average page consumption over the last 16 wakeups
        for (; i < PAGES_CONSUMED_LENGTH; i++) {
            if (pageConsumptionHistory[i] == MAXULONG64) {
                break;
            }
            averagePagesConsumedPerWakeup += pageConsumptionHistory[i];
        }
        if (i == 0) {
            continue;
        }
        if (averagePagesConsumedPerWakeup == 0) {
            continue;
        }
        averagePagesConsumedPerWakeup /= i;


        averagePagesConsumedPerWakupIn100Ns = averagePagesConsumedPerWakeup / MS_TO_100NS;


        pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);
        timeUntilOutIn100ns = (pagesLeft / averagePagesConsumedPerWakupIn100Ns);


        trimmerWork = vm.threadInfo.trimmer->work;
        pageTrimRate = getPagesPerTime(trimmerWork);

        writerWork = vm.threadInfo.writer->work;
        pageWriteRate = getPagesPerTime(writerWork);

        // once aging is done, this is the amount of time it takes to make those pages available.
        timeToMakePagesAvailable = pageTrimRate * averagePagesConsumedPerWakupIn100Ns +
                                   pageWriteRate * averagePagesConsumedPerWakupIn100Ns;


        // create a copy of the agers circular buffer
        agerWork = vm.threadInfo.aging->work;
        pageAgeRate = getPagesPerTime(agerWork);

        numActivePages = ReadULong64NoFence(&vm.pfn.numActivePages);
        numToAgeTotal = numActivePages * NUMBER_OF_AGES;
        timeToAge = numToAgeTotal / pageAgeRate;
        perfectTimeToAge = timeUntilOutIn100ns - timeToMakePagesAvailable;

        //TODO does the time it takes to age matter?
        //TODO a bunch of if statements
        if (timeToAge < perfectTimeToAge) {
            numToAgeThisWakeup = numToAgeTotal / (perfectTimeToAge / MS_TO_100NS);
        } else {
           numToAgeThisWakeup = numToAgeTotal / (perfectTimeToAge / MS_TO_100NS);
        }

        // I really think it is this easy.
        numToTrimThisWakeup = averagePagesConsumedPerWakeup;
        numToWriteThisWakeup = averagePagesConsumedPerWakeup;

        InterlockedExchange64(&vm.pte.numToAge, numToAgeThisWakeup);
        InterlockedExchange64(&vm.pte.numToTrim, numToTrimThisWakeup);
        InterlockedExchange64(&vm.pte.numToWrite, numToWriteThisWakeup);

        SetEvent(vm.events.agerStart);
        SetEvent(vm.events.trimmingStart);
        SetEvent(vm.events.writingStart);
    }
}
