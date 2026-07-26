#include "disk/disk.h"
//
// Created by nrper on 9/21/2025.
//
#include <stdio.h>

#include "variables/structures.h"

#define S_TO_100NS (10000000)
#define FANCY_TRIMMING 1

//this is in seconds
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



    // upscale our current pages to represent the number in seconds.
    return pages * (S_TO_100NS) / time;
}


DWORD scheduler_thread(LPVOID info) {
    SetThreadDescription(GetCurrentThread(), L"Scheduler");

    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;

    ULONG64 pagesLeft;
    ULONG64 localPagesConsumedPerWakeUp;
    ULONG64 timeUntilOutInMS;
    ULONG64 historyIndex;
    ULONG64 pageConsumptionHistory[PAGES_CONSUMED_LENGTH];
    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);

    ULONG64 timeToMakePagesAvailableInMS;

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


    ULONG64 numToTrimThisWakeup;

    ULONG64 numToWriteThisWakeup;

    ULONG64 timeToAgeInMS;
    ULONG64 perfectTimeToAge;
    // ULONG64 timeToTrim;
    // ULONG64 timeToWrite;

    ULONG64 numRoundsToAge;


    LONG64 localnumOfAge[NUMBER_OF_AGES];

    memset(pageConsumptionHistory, 0, sizeof(ULONG64) * PAGES_CONSUMED_LENGTH);


    ULONG64 averagePagesConsumedPerWakeup = 0;
    ULONG64 averagePagesConsumedPer100Ns = 0;

    ULONG64 averagePagesTrimmedPerWakeup = 0;
    ULONG64 averagePagesWrittenPerWakeup = 0;


    ULONG64 futureTotalOfMaxAge;
    // this variable is used to store how many pages will be of max age we think by the end of aging.

    // Wait a bit until sufficient pages have been accessed
    Sleep(100);

    bool isCalibrating = true;
    int calibrationCounter = 0;

    while (TRUE) {
        Sleep(SCHEDULER_PERIOD_MS);
        if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
            return 0;
        }

        if (isCalibrating) {
            // Fixed amounts during warmup, scaled to the wakeup period (roughly 1000 pages/sec assumed typical workload)
            numToAgeThisWakeup = SCHEDULER_PERIOD_MS;
            numToTrimThisWakeup = SCHEDULER_PERIOD_MS;
            numToWriteThisWakeup = SCHEDULER_PERIOD_MS;


            calibrationCounter++;
            if (calibrationCounter >= 4) {
                isCalibrating = false;
            }
            //ASSERT(FALSE);
        } else {
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


#if 1
            // averagePagesConsumedPerWakeup is pages per SCHEDULER_PERIOD_MS, not pages per second --
            // convert to a true rate before mixing it with anything measured in real seconds (like pageTrimRate/
            // pageWriteRate/pageAgeRate, which come from QueryPerformanceCounter and don't depend on the period).
            ULONG64 averagePagesConsumedPerSecond = averagePagesConsumedPerWakeup * 1000 / SCHEDULER_PERIOD_MS;

            pagesLeft = ReadULong64NoFence(&vm.lists.standby.length) + ReadULong64NoFence(&vm.lists.free.length);
            timeUntilOutInMS = pagesLeft * SCHEDULER_PERIOD_MS / (averagePagesConsumedPerWakeup);


            trimmerWork = vm.threadInfo.trimmer->work;
            pageTrimRate = getPagesPerSecond(trimmerWork);
            if (pageTrimRate == 0) {
                pageTrimRate = 10000;
            }

            averagePagesTrimmedPerWakeup = pageTrimRate * SCHEDULER_PERIOD_MS / 1000;

            writerWork = vm.threadInfo.writer->work;
            pageWriteRate = getPagesPerSecond(writerWork);
            if (pageWriteRate == 0) {
                pageWriteRate = 10000;
            }
            averagePagesWrittenPerWakeup = pageWriteRate * SCHEDULER_PERIOD_MS / 1000;

            ULONG64 timeToTrimInMS = (averagePagesConsumedPerWakeup * 1000 / pageTrimRate);

            ULONG64 timeToWriteInMS = (averagePagesConsumedPerWakeup * 1000 / pageWriteRate);

            // Total time to make pages available after aging (trim + write, or max if parallel?)
            if (timeToWriteInMS > timeToTrimInMS) {
                timeToMakePagesAvailableInMS = timeToWriteInMS;
            } else {
                timeToMakePagesAvailableInMS = timeToTrimInMS;
            }
           // timeToMakePagesAvailable = timeToWrite + timeToTrim;

            // create a copy of the agers circular buffer
            agerWork = vm.threadInfo.aging->work;
            pageAgeRate = getPagesPerSecond(agerWork);

            numActivePages = ReadULong64NoFence(&vm.pfn.numActivePages);


            // this copy is not a perfect copy as inbetween loops, anything can happen, but it is good enough
            for (int j = 0; j < NUMBER_OF_AGES; ++j) {
                localnumOfAge[j] = ReadULong64NoFence(&vm.pte.globalNumOfAge[j].count);
            }


            futureTotalOfMaxAge = 0;
            numRoundsToAge = 0;

            //here we figure out how much aging we have to do, based on the current age lists
            // the way it works it that we add the number of the oldest age to the current count and if that exceeds
            // how many pages we need, we are set, otherwise we need at least one more round, so we increment the round count.
            for (; numRoundsToAge < NUMBER_OF_AGES; ++numRoundsToAge) {
                futureTotalOfMaxAge += localnumOfAge[NUMBER_OF_AGES - numRoundsToAge - 1];

                if (futureTotalOfMaxAge > averagePagesConsumedPerWakeup) {
                    break;
                }
            }

            numToAgeTotal = numActivePages * numRoundsToAge;


            // if we have done some aging, use it
            if (pageAgeRate != 0) {
                if (numToAgeTotal > pageAgeRate) {
                    timeToAgeInMS = numToAgeTotal * 1000 / pageAgeRate;
                } else {
                    timeToAgeInMS = 1;
                }
            }


            //this is the time it should take to age what we want, so that the trimmer and writer can make pages available just in time.
            if (timeUntilOutInMS < timeToMakePagesAvailableInMS) {
                perfectTimeToAge = 0;
            } else {
                perfectTimeToAge = timeUntilOutInMS - timeToMakePagesAvailableInMS;
            }


            // if we have't aged yet, or should't, age some small amount to get more data
            if (pageAgeRate == 0 || numToAgeTotal == 0) {
                numToAgeThisWakeup = 10000;
            } else {
                // Basically, if I can age faster than I can consume, only age the perfect amount.
                // otherwise, age the perceived max number of ptes that can be aged in one second.
                // numToAgeTotal / perfectTimeToAge (or / timeToAge) is a true rate in pages/sec (both
                // are real seconds); scale it down to how much of that we should do in one wakeup period.
                if (timeToAgeInMS < perfectTimeToAge) {
                    numToAgeThisWakeup = numToAgeTotal * SCHEDULER_PERIOD_MS / (perfectTimeToAge);
                } else {
                    numToAgeThisWakeup = numToAgeTotal * SCHEDULER_PERIOD_MS / (timeToAgeInMS);
                }
            }

            if (numToAgeThisWakeup == 0) {
                numToAgeThisWakeup = 10000;
            }



#if FANCY_TRIMMING

        if (averagePagesConsumedPerWakeup > averagePagesWrittenPerWakeup) {
            numToTrimThisWakeup = averagePagesConsumedPerWakeup;
            numToWriteThisWakeup = averagePagesConsumedPerWakeup;
        } else {
            numToTrimThisWakeup = averagePagesWrittenPerWakeup;
            numToWriteThisWakeup = averagePagesWrittenPerWakeup;
        }

#else
        numToTrimThisWakeup = 1000;
        numToWriteThisWakeup = 1000;

#endif



        }

        InterlockedExchange64(&vm.pte.numToAge, numToAgeThisWakeup);

        InterlockedExchange64(&vm.pte.numToTrim, numToTrimThisWakeup);
        InterlockedExchange64(&vm.pte.numToWrite, numToWriteThisWakeup);
#endif


        SetEvent(vm.events.agerStart);
        SetEvent(vm.events.trimmingStart);
    }
}
