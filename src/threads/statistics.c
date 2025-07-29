//
// Created by nrper on 7/21/2025.
//

#include "../../include/threads/statistics.h"
#include "utils/statistics_utils.h"
#include "variables/globals.h"


#include <process.h>
#include <stdio.h>

DWORD
recordProbability(LPVOID lpParameter) {

    ULONG64 start, end;
    PTE_REGION* region;

 //   DebugBreak();
    double hazard;

    Sleep(1000 * 30);


    //DebugBreak();
    start = GetTickCount64();

    region = pteRegionsBase;
    for (int i = 0; i < NUMBER_OF_PTE_REGIONS; i++) {

        if (region->accessed == 1) {
            hazard = likelihoodOfAccess(region);
            printf("iteration %i:This is the likelihood of a pte region being acces in the next 10 seconds: %d\n",i, hazard);

          //  DebugBreak();
        }
        region++;
    }
    end = GetTickCount64();

    ULONGLONG totalTime = end - start;



    return 0;
}
