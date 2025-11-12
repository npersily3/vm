#include "disk/disk.h"
//
// Created by nrper on 9/21/2025.
//
#include <stdio.h>

#include "variables/structures.h"

#define S_TO_100NS (10000000)

//TODO your units are wrong here
ULONG64 getPagesPerSecond(workDone work) {
    ULONG64 time = 0;
    ULONG64 pages = 0;


    for (int i = 0; i < 16; ++i) {
        time += work.timeIntervals[i];
        pages += work.numPagesProccessed[i];
    }
    if (time == 0) {
        return 1000;
    }

    ULONG64 scaleFactor = (S_TO_100NS) / time;

    // upscale our current pages to represent the number in seconds.
    return pages * scaleFactor;
}


DWORD scheduler_thread(LPVOID info) {
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;

    ULONG64 pagesLeft;
    ULONG64 localPagesConsumedPerWakeUp;
    ULONG64 timeUntilOutInSecs;
    ULONG64 historyIndex;
    ULONG64 pageConsumptionHistory[PAGES_CONSUMED_LENGTH];
    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);

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

    ULONG64 numRoundsToAge;

    LONG64 localnumOfAge[NUMBER_OF_AGES];

    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);


    ULONG64 averagePagesConsumedPerWakeup = 0;
    ULONG64 averagePagesConsumedPer100Ns = 0;


    ULONG64 futureTotalOfMaxAge;
    // this variable is used to store how many pages will be of max age we think by the end of aging.

    // Wait a bit until the system has good data
    Sleep(100);


    while (TRUE) {



        Sleep(1000);
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


    //    printf("averagePagesPerWakup: %llu. averPagesPer100ns %llu \n", averagePagesConsumedPerWakeup,
             //  averagePagesConsumedPer100Ns);

#if 1
        pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);
        timeUntilOutInSecs = (pagesLeft / averagePagesConsumedPerWakeup);


        trimmerWork = vm.threadInfo.trimmer->work;
        pageTrimRate = getPagesPerSecond(trimmerWork);
        if (pageTrimRate == 0) {
            pageTrimRate = 10000;
        }
//        printf("pageTrimRate %llu \n", pageTrimRate);

        writerWork = vm.threadInfo.writer->work;
        pageWriteRate = getPagesPerSecond(writerWork);
        if (pageWriteRate == 0) {
            pageWriteRate = 10000;
        }
      //  printf("pageWriteRate %llu \n", pageWriteRate);

        // Calculate time to process the average pages consumed: time = pages / (pages/time)
        // Time to trim the consumed pages (in 100ns units)
        ULONG64 timeToTrim = (pageTrimRate > 0) ? (averagePagesConsumedPerWakeup / pageTrimRate) : 0;
        // Time to write the consumed pages (in 100ns units)
        ULONG64 timeToWrite = (pageWriteRate > 0) ? (averagePagesConsumedPerWakeup / pageWriteRate) : 0;

        // Total time to make pages available after aging (trim + write, or max if parallel?)
        timeToMakePagesAvailable = timeToTrim + timeToWrite;


        // create a copy of the agers circular buffer
        agerWork = vm.threadInfo.aging->work;
        pageAgeRate = getPagesPerSecond(agerWork);

        numActivePages = ReadULong64NoFence(&vm.pfn.numActivePages);



        // this copy is not a perfect copy as inbetween loops, anything can happen, but it is good enough
        for (int j = 0; j < NUMBER_OF_AGES; ++j) {
            localnumOfAge[j] = ReadULong64NoFence(&vm.pte.globalNumOfAge[j]);
        }


        futureTotalOfMaxAge = 0;
        numRoundsToAge = 0;

        //here we figure out how much aging we have to do, based on the current age lists
        for (; numRoundsToAge < NUMBER_OF_AGES; ++numRoundsToAge) {
            futureTotalOfMaxAge += localnumOfAge[NUMBER_OF_AGES - numRoundsToAge - 1];

            if (futureTotalOfMaxAge > averagePagesConsumedPerWakeup) {
                break;
            }
        }

        numToAgeTotal = numActivePages * numRoundsToAge;


        // if we have done some aging, use it
        if (pageAgeRate != 0) {
            timeToAge = numToAgeTotal / pageAgeRate;
        }



        //todo make sure this is not zero and not negative.
        //this is the time it should take to age what we want, so that the trimmer and writer can make pages available just in time.
        perfectTimeToAge = timeUntilOutInSecs - timeToMakePagesAvailable;

        // if we have't aged yet, or should't age some small amount to get more data
        if (pageAgeRate == 0 || numToAgeTotal == 0) {
            numToAgeThisWakeup = 1000000;
        } else {

            // Basically, if I can age faster than I can consume, only age the perfect amount.
            // otherwise, age the perceived max number of pages.
            if(timeToAge < perfectTimeToAge) {
                numToAgeThisWakeup = numToAgeTotal / (perfectTimeToAge);
            } else {
                numToAgeThisWakeup = numToAgeTotal / (timeToAge);
            }
        }

        //TODO I need to multiply this by the trim rate. Think of the hypothetical where you can trim 50 p/s and you have 100 pages left and you will be out in 10s, you only need to trim in the last 2 seconds.


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
