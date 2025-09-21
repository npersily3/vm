//
// Created by nrper on 9/20/2025.
//
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

VOID ageRegion(PTE_REGION* region) {
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
        //removeFromMiddleOfList(region);
        //addToList(region);
    }

// VOID ageLoop()

}