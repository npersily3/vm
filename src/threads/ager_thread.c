//
// Created by nrper on 9/20/2025.
//
#include "utils/pte_regions_utils.h"
#include "utils/pte_utils.h"
#include "variables/structures.h"

/**
 * @brief A function that ages a pte.
 * @param pteAddress The address of the pte to age
 * @param region The region that the pte is in
 * @retval 1 if the PTE was aged
 * @retval 0 if the PTE was not aged
 */
ULONG64 agePTE(pte* pteAddress, PTE_REGION* region) {
    pte pteContents;
    pte newPTEContents;
    ULONG64 currentAge;
    ULONG64 newAge;
    ULONG64 beenAccessed;
    ULONG64 returnValue;

    pteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);

    // if the pte is not valid, we don't need to age it
    if (pteContents.validFormat.valid == 0) {
        returnValue = 0;
        return returnValue;
    }
    newPTEContents.entireFormat = pteContents.entireFormat;


    currentAge = pteContents.validFormat.age;
    beenAccessed = pteContents.validFormat.access;

    // if the pte was accessed and has been previously aged,
    // we need to reset the age
    if (beenAccessed == TRUE && currentAge != 0) {
        returnValue = 0;
        newAge = 0;
    } else {
        if (currentAge != MAX_AGE) {
            returnValue = 1;
            newAge = currentAge + 1;
        }
    }
    // regardless of what happens, we need to clear the access bit
    newPTEContents.validFormat.access = FALSE;
    newPTEContents.validFormat.age = newAge;

    // keep track of region stats
    region->numOfAge[currentAge]--;
    region->numOfAge[newAge]++;

    writePTE(pteAddress, newPTEContents);

    return returnValue;
}


/**
 * @brief Gets the age of the oldest pte in a region
 * @param region The region to get the age of
 * @return The highest age pte in the region
 */
ULONG64 getRegionAge(PTE_REGION* region) {

    for (int i = NUMBER_OF_AGES; i > 0; ++i) {
        if (region->numOfAge[i] != 0) {
            return i;
        }
    }
    return 0;
}

/**
 *@brief Ages a singular PTE region
 * @param region The region to age
 * @param threadInfo The thread info of the caller. Used for debugging.
 * @return How many PTEs were aged
 */
ULONG64 ageRegion(PTE_REGION* region, PTHREAD_INFO threadInfo) {
    pte* pteAddress;
    ULONG64 previousAge;
    ULONG64 newAge;
    ULONG64 numPTEsAged;

    numPTEsAged = 0;
    previousAge = getRegionAge(region);
    pteAddress = getFirstPTEInRegion(region);

    for (int i = 0; i < vm.config.number_of_ptes_per_region; i++) {
        numPTEsAged += agePTE(pteAddress, region);
        pteAddress++;
    }

    newAge = getRegionAge(region);


    if (newAge != previousAge) {
        removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[previousAge], region, threadInfo);
        addRegionToTail(&vm.pte.ageList[newAge], region, threadInfo);

    }

    return numPTEsAged;

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
    ULONG64 totalPTEsToAge;
    ULONG64 numPTEsAged;

    while (TRUE) {
        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);


        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }

        totalPTEsToAge = ReadULong64NoFence(&vm.pte.numToAge);


        while (totalPTEsToAge > 0) {
            numPTEsAged = 0;
            enterPTERegionLock(currentRegion, threadInfo);

            if (currentRegion->hasActiveEntry == TRUE) {
                numPTEsAged = ageRegion(currentRegion, threadInfo);
            }
            leavePTERegionLock(currentRegion, threadInfo);

            totalPTEsToAge -= numPTEsAged;
            currentRegion++;
        }
        InterlockedExchange64(&vm.pte.numToAge, 0);

    }
}