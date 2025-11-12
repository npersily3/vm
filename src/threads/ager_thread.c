//
// Created by nrper on 9/20/2025.
//
#include <stdio.h>

#include "utils/pte_regions_utils.h"
#include "utils/pte_utils.h"
#include "variables/structures.h"




#if DBG

VOID sumOfAges(PTE_REGION* region) {
    ULONG64 sum = 0;

    for (int i = 0; i < NUMBER_OF_AGES; i++) {
        sum+= region->numOfAge[i];
    }
    ASSERT(sum == vm.config.number_of_ptes_per_region)

}


#endif
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

#if DBG
    sumOfAges(region);

    ULONG64 index;

    index = pteAddress - getFirstPTEInRegion(region);


#endif

    returnValue = 0;

    pteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);

    // if the pte is not valid, we don't need to age it
    if (pteContents.validFormat.valid == 0) {
        return returnValue;
    }
    newPTEContents.entireFormat = pteContents.entireFormat;


    currentAge = pteContents.validFormat.age;
    beenAccessed = pteContents.validFormat.access;

    // if the pte was accessed and has been previously aged,
    // we need to reset the age
    if (beenAccessed == TRUE && currentAge != 0) {
        newAge = 0;
    } else {
        if (currentAge != MAX_AGE) {
            returnValue = 1;
            newAge = currentAge + 1;
        } else {
            newAge = MAX_AGE;
        }
    }
    // regardless of what happens, we need to clear the access bit
    newPTEContents.validFormat.access = FALSE;
    newPTEContents.validFormat.age = newAge;

    // keep track of region stats
    ASSERT(region->numOfAge[currentAge] != 0)
    ASSERT(region->numOfAge[newAge] != vm.config.number_of_ptes_per_region)

    region->numOfAge[currentAge]--;
    InterlockedDecrement64(&vm.pte.globalNumOfAge[currentAge]);

    region->numOfAge[newAge]++;
    InterlockedIncrement64(&vm.pte.globalNumOfAge[newAge]);

    writePTE(pteAddress, newPTEContents);
#if DBG
    sumOfAges(region);
#endif

    return returnValue;
}


/**
 * @brief Gets the age of the oldest pte in a region
 * @param region The region to get the age of
 * @return The highest age pte in the region
 */
ULONG64 getRegionAge(PTE_REGION* region) {

    for (int i = MAX_AGE; i > 0; --i) {
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
 * @pre The region in the parameter must be locked
 * @post The region in the parameter must be unlocked
 */
ULONG64 ageRegion(PTE_REGION* region, PTHREAD_INFO threadInfo) {
    pte* pteAddress;
    ULONG64 previousAge;
    ULONG64 newAge;
    ULONG64 numPTEsAged;


#if DBG
    sumOfAges(region);
#endif

    numPTEsAged = 0;
    previousAge = getRegionAge(region);
    pteAddress = getFirstPTEInRegion(region);

    for (int i = 0; i < vm.config.number_of_ptes_per_region; i++) {
        numPTEsAged += agePTE(pteAddress, region);
        pteAddress++;

    }

    newAge = getRegionAge(region);



    if (newAge == previousAge) {


    } else {
        removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[previousAge], region, threadInfo);
        addRegionToTail(&vm.pte.ageList[newAge], region, threadInfo);
    }

#if DBG
    sumOfAges(region);
#endif

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
    ULONG64 initialTotalPTEsToAge;
    ULONG64 totalPTEsLeftToAge;
    ULONG64 numPTEsAged;

    while (TRUE) {
        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);


        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }


        initialTotalPTEsToAge = ReadULong64NoFence(&vm.pte.numToAge);
        totalPTEsLeftToAge = initialTotalPTEsToAge;

        while (totalPTEsLeftToAge > 0) {

            numPTEsAged = 0;
            enterPTERegionLock(currentRegion, threadInfo);

            if (currentRegion->hasActiveEntry == TRUE) {
                numPTEsAged = ageRegion(currentRegion, threadInfo);
            }
            leavePTERegionLock(currentRegion, threadInfo);




            if (totalPTEsLeftToAge < numPTEsAged) {
                totalPTEsLeftToAge = 0;
            } else {
                totalPTEsLeftToAge -= numPTEsAged;
            }

            if (currentRegion == vm.pte.RegionsBase + vm.config.number_of_pte_regions - 1) {
                currentRegion = vm.pte.RegionsBase;
            } else {
                currentRegion++;
            }

        }

        InterlockedExchange64(&vm.pte.numToAge, 0);
        InterlockedExchange((volatile LONG *) &vm.misc.agingInProgress,FALSE);

    }
}