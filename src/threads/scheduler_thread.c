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

    // ULONG64 numToTrimTotal;
    ULONG64 numToTrimThisWakeup;

    // ULONG64 numToWriteTotal;
    ULONG64 numToWriteThisWakeup;

    ULONG64 timeToAge;
    ULONG64 perfectTimeToAge;
    // ULONG64 timeToTrim;
    // ULONG64 timeToWrite;


    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);


    ULONG64 averagePagesConsumedPerWakeup = 0;
    ULONG64 averagePagesConsumedPer100Ns = 0;

    // Wait a bit until the system has good data
    Sleep(100);


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
            if (pageConsumptionHistory[i] == 0) {
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


        // Convert pages/ms to pages/100ns rate
        averagePagesConsumedPer100Ns = averagePagesConsumedPerWakeup / MS_TO_100NS;

        printf("averagePagesPerWakup: %llu. averPagesPer100ns %llu \n", averagePagesConsumedPerWakeup, averagePagesConsumedPer100Ns);

#if 1
        pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);
        timeUntilOutIn100ns = (pagesLeft / averagePagesConsumedPer100Ns);


        trimmerWork = vm.threadInfo.trimmer->work;
        pageTrimRate = getPagesPerTime(trimmerWork);  // pages/100ns

        writerWork = vm.threadInfo.writer->work;
        pageWriteRate = getPagesPerTime(writerWork);  // pages/100ns

        // Calculate time to process the average pages consumed: time = pages / (pages/time)
        // Time to trim the consumed pages (in 100ns units)
        ULONG64 timeToTrim = (pageTrimRate > 0) ? (averagePagesConsumedPerWakeup / pageTrimRate) : 0;
        // Time to write the consumed pages (in 100ns units)
        ULONG64 timeToWrite = (pageWriteRate > 0) ? (averagePagesConsumedPerWakeup / pageWriteRate) : 0;

        // Total time to make pages available after aging (trim + write, or max if parallel?)
        timeToMakePagesAvailable = timeToTrim + timeToWrite;


        // create a copy of the agers circular buffer
        agerWork = vm.threadInfo.aging->work;
        pageAgeRate = getPagesPerTime(agerWork);

        numActivePages = ReadULong64NoFence(&vm.pfn.numActivePages);
        numToAgeTotal = numActivePages * NUMBER_OF_AGES;

        if (pageAgeRate = 0) {
            timeToAge = MAXULONG64;
        } else {
            timeToAge = numToAgeTotal / pageAgeRate;
        }


        perfectTimeToAge = timeUntilOutIn100ns - timeToMakePagesAvailable;



        // Basically, if I can age faster than I can consume, only age the perfect amount.
        // otherwise, age the perceived max number of pages.
        if (timeToAge < perfectTimeToAge) {
            numToAgeThisWakeup = numToAgeTotal / (perfectTimeToAge / MS_TO_100NS);
        } else {
           numToAgeThisWakeup = numToAgeTotal / (timeToAge / MS_TO_100NS);
        }

        // I really think it is this easy. law of equivalent exchange
        numToTrimThisWakeup = averagePagesConsumedPerWakeup;
        numToWriteThisWakeup = averagePagesConsumedPerWakeup;

        InterlockedExchange64(&vm.pte.numToAge, numToAgeThisWakeup);
        InterlockedExchange64(&vm.pte.numToTrim, numToTrimThisWakeup);
        InterlockedExchange64(&vm.pte.numToWrite, numToWriteThisWakeup);
#endif

        SetEvent(vm.events.agerStart);
        SetEvent(vm.events.trimmingStart);
        SetEvent(vm.events.writingStart);
    }
}
