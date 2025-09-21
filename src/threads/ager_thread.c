//
// Created by nrper on 9/20/2025.
//
#include "utils/pte_regions_utils.h"
#include "utils/pte_utils.h"
#include "variables/structures.h"
/**
 *
 * @param region
 * @pre
 * @post
 */
VOID agePTE(pte* pteAddress, PTE_REGION* region) {
    pte pteContents;
    pte newPTEContents;
    ULONG64 currentAge;
    ULONG64 newAge;
    ULONG64 beenAccessed;

    pteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);
    newPTEContents.entireFormat = pteContents.entireFormat;


    currentAge = pteContents.validFormat.age;
    beenAccessed = pteContents.validFormat.access;

    if (beenAccessed == TRUE) {
        newPTEContents.validFormat.access = FALSE;
        newAge = 0;
    } else {
        if (currentAge != MAX_AGE) {
            newAge = currentAge + 1;
        }
    }
    newPTEContents.validFormat.age = newAge;
    region->numOfAge[currentAge]--;
    region->numOfAge[newAge]++;

    writePTE(pteAddress, newPTEContents);
}
ULONG64 getRegionAge(PTE_REGION* region) {

    for (int i = NUMBER_OF_AGES; i > 0; ++i) {
        if (region->numOfAge[i] != 0) {
            return i;
        }
    }
    return 0;
}

/**
 *
 * @param region
 * @param threadInfo
 */
VOID ageRegion(PTE_REGION* region, PTHREAD_INFO threadInfo) {
    pte* pteAddress;
    ULONG64 previousAge;
    ULONG64 newAge;

    previousAge = getRegionAge(region);


    pteAddress = getFirstPTEInRegion(region);

    for (int i = 0; i < vm.config.number_of_ptes_per_region; i++) {
        agePTE(pteAddress, region);
        pteAddress++;
    }

    newAge = getRegionAge(region);

    if (newAge != previousAge) {
        removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[previousAge], region, threadInfo);
        addRegionToTail(&vm.pte.ageList[newAge], region, threadInfo);
    }

}

DWORD ager_thread(LPVOID info) {




    HANDLE events[2];
    DWORD returnEvent;
    events[0] = vm.events.agerStart;
    events[1] = vm.events.systemShutdown;

    PTE_REGION* currentRegion;
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO)info;
    currentRegion = vm.pte.RegionsBase;

    while (TRUE) {
        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);


        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

        for (int i = 0; i < vm.config.number_of_pte_regions; i++) {

            enterPTERegionLock(currentRegion, threadInfo);
            if (currentRegion->hasActiveEntry == TRUE) {
                ageRegion(currentRegion, threadInfo);
            }
            leavePTERegionLock(currentRegion, threadInfo);

            currentRegion++;
        }

    }
}