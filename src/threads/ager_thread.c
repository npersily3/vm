//
// Created by nrper on 9/20/2025.
//
#include <stdio.h>

#include "utils/page_utils.h"
#include "utils/pte_regions_utils.h"
#include "utils/pte_utils.h"
#include "utils/thread_utils.h"
#include "variables/structures.h"


//access bit with interlocked compare exchange.
//write pte can only use nofence if the pte was previously invalid.
//agePte should loop continuously until it succeeds.
//all pte writes should be re-examined.
//debug pte region field about what list a pte region is on and check the max age stuff


int canPageTableBeAged(pte *pteAddress) {
    // this can always be aged


    if (isLeafPTE(pteAddress)) {
        return TRUE;
    }
    pte *childPTE = getStartOfLowerPagetable(pteAddress);

    for (int i = 0; i < vm.config.pte_entries_per_pagetable; i++) {
        if (childPTE[i].validFormat.valid == 1 || childPTE[i].transitionFormat.isTransition == 1) {
            return FALSE;
        }
    }
    return TRUE;
}


/**
 * @brief A function that ages a pte.
 * @param pteAddress The address of the pte to age
 * @param region The region that the pte is in
 * @retval 1 if the PTE was aged
 * @retval 0 if the PTE was not aged
 */

ULONG64 agePTE(pte *pteAddress, PTE_REGION *region) {
    pte pteContents;
    pte newPTEContents;
    pte pteAtTimeOfWrite;
    ULONG64 currentAge;
    ULONG64 newAge;
    ULONG64 beenAccessed;
    ULONG64 returnValue;
    int isAgeable;

    pteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);

    while (true) {
        returnValue = 0;
        // if the pte is not valid, we don't need to age it
        if (pteContents.validFormat.valid == 0) {
            return returnValue;
        }
        newPTEContents.entireFormat = pteContents.entireFormat;


        currentAge = pteContents.validFormat.age;
        beenAccessed = pteContents.validFormat.access;

        // if the pte was accessed and has been previously aged,
        // we need to reset the age
        if (beenAccessed == TRUE) {
            isAgeable = canPageTableBeAged(pteAddress);
        } else {
            isAgeable = TRUE;
        }

        if (isAgeable == FALSE) {
            break;
        }

        if (currentAge != MAX_AGE) {
            returnValue = 1;
            newAge = currentAge + 1;
        } else {
            newAge = MAX_AGE;
        }


        // regardless of what happens, we need to clear the access bit
        newPTEContents.validFormat.access = FALSE;
        newPTEContents.validFormat.age = newAge;

        // keep track of region stats
        ASSERT(region->numOfAge[currentAge] != 0)


        pteAtTimeOfWrite.entireFormat = writePTE(pteAddress, newPTEContents, pteContents).entireFormat;

        // if when writing the pte, the contents have changed, we need to loop again with the new contents
        // otherwise we are trampling on someone else's work to that pte
        if (pteAtTimeOfWrite.entireFormat == pteContents.entireFormat) {
            // only write the values here or else if you collide with an access bit setter, you mistakenly will double change some data
            ASSERT(region->numOfAge[currentAge] > 0)
            region->numOfAge[currentAge]--;
            InterlockedDecrement64(&vm.pte.globalNumOfAge[currentAge].count);

            region->numOfAge[newAge]++;
            InterlockedIncrement64(&vm.pte.globalNumOfAge[newAge].count);
            break;
        }

        pteContents.entireFormat = pteAtTimeOfWrite.entireFormat;
    }

    return returnValue;
}


/**
 * @brief Gets the age of the oldest pte in a region
 * @param region The region to get the age of
 * @return The highest age pte in the region
 */
ULONG64 getRegionAge(PTE_REGION *region) {
    for (int i = MAX_AGE; i >= 0; --i) {
        if (region->numOfAge[i] != 0) {
            return i;
        }
    }


    if (region->numOfAge[0] == 0) {
        return MAXULONG64;
    } else {
        return 0;
    }
}

/**
 *@brief Ages a singular PTE region
 * @param region The region to age.
 * @param threadInfo The thread info of the caller. Used for debugging.
 * @return How many PTEs were aged
 * @pre The region in the parameter must be locked
 * @post The region in the parameter must be unlocked
 */
ULONG64 ageRegion(PTE_REGION *region, PTHREAD_INFO threadInfo) {
    pte *pteAddress;
    ULONG64 previousAge;
    ULONG64 newAge;
    ULONG64 numPTEsAged;


    numPTEsAged = 0;
    previousAge = getRegionAge(region);


    ASSERT(region->ageListNumber == previousAge)

    pteAddress = getFirstPTEInRegion(region);

    for (int i = 0; i < vm.config.pte_entries_per_pagetable; i++) {
        numPTEsAged += agePTE(pteAddress, region);
        pteAddress++;
    }

    newAge = getRegionAge(region);


    if (newAge == previousAge) {
    } else {
        removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[previousAge], region, threadInfo);
        addRegionToTail(&vm.pte.ageList[newAge], region, threadInfo);
    }


    return numPTEsAged;
}


/**
 * @brief Bumps a valid, already-unaccessed PTE's age by one (capped at MAX_AGE), clears the access
 *        bit, and -- only if the write lands -- shifts the global age histogram. One interlocked
 *        compare-exchange, no looping; the caller decides what to do on a failed (raced) write.
 * @param pteAddress The PTE to age. Assumed valid with its access bit already clear.
 * @param pteContents The caller's already-read snapshot; the CAS is made against it, so a race
 *        since that read fails the write instead of clobbering the newer value.
 * @param newAge Filled with the age written into the PTE.
 * @return TRUE if the compare-exchange landed, FALSE if it raced.
 */
boolean ageUnnaccessedPTE(pte *pteAddress, pte pteContents, ULONG64 *newAge) {
    pte newPTEContents;
    pte pteAtTimeOfWrite;
    ULONG64 currentAge;

    currentAge = pteContents.validFormat.age;
    *newAge = (currentAge != MAX_AGE) ? currentAge + 1 : MAX_AGE;

    newPTEContents.entireFormat = pteContents.entireFormat;
    newPTEContents.validFormat.access = FALSE;
    newPTEContents.validFormat.age = *newAge;

    pteAtTimeOfWrite.entireFormat = writePTE(pteAddress, newPTEContents, pteContents).entireFormat;

    if (pteAtTimeOfWrite.entireFormat != pteContents.entireFormat) {
        return FALSE;
    }

    InterlockedDecrement64(&vm.pte.globalNumOfAge[currentAge].count);
    InterlockedIncrement64(&vm.pte.globalNumOfAge[*newAge].count);
    return TRUE;
}

/**
 * @brief Moves the region backing parentPTE's child page table from the oldAge age-list to the
 *        newAge one. Same list shuffle as the tail of ageLeafRegion. No-op when the age is unchanged.
 * @param parentPTE The PTE whose child page table's region should be re-listed.
 * @param oldAge The age-list the region is currently on.
 * @param newAge The age-list the region should move to.
 * @param threadInfo The caller's thread info, forwarded to the list helpers.
 * @pre The region must be locked.
 */
VOID shiftAgeList(pte *parentPTE, ULONG64 oldAge, ULONG64 newAge, PTHREAD_INFO threadInfo) {
    PTE_REGION *region;

    if (oldAge == newAge) {
        return;
    }

    region = getPTERegion(getStartOfLowerPagetable(parentPTE));

    removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[oldAge], region, threadInfo);
    addRegionToTail(&vm.pte.ageList[newAge], region, threadInfo);
}


/**
 * @brief A leaf-only copy of the former agePTE. Same aging logic (accessed -> reset to 0,
 *        otherwise advance up to MAX_AGE, always clearing the access bit), but it does NOT
 *        touch the region's numOfAge counters. The global age counts are still maintained.
 * @param pteAddress The address of the pte to age
 * @param region The region the pte belongs to (kept for parity with agePTE; unused otherwise)
 * @retval 1 if the PTE advanced in age
 * @retval 0 otherwise
 * @pre The region must be locked
 */
ULONG64 ageLeafPTE(pte *pteAddress, PTE_REGION *region) {
    pte pteContents;
    pte newPTEContents;
    pte pteAtTimeOfWrite;
    ULONG64 currentAge;
    ULONG64 newAge;

    pteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);

    while (true) {
        // if the pte is not valid, we don't need to age it
        if (pteContents.validFormat.valid == 0) {
            return 0;
        }

        currentAge = pteContents.validFormat.age;

        // unaccessed: bump via the shared helper, which also shifts the global histogram
        if (pteContents.validFormat.access == FALSE) {
            if (ageUnnaccessedPTE(pteAddress, pteContents, &newAge)) {
                return newAge > currentAge;
            }
            // raced: re-read and try again
            pteContents.entireFormat = ReadULong64NoFence(&pteAddress->entireFormat);
            continue;
        }

        // accessed: reset the age to 0, clearing the access bit
        newAge = 0;
        newPTEContents.entireFormat = pteContents.entireFormat;
        newPTEContents.validFormat.access = FALSE;
        newPTEContents.validFormat.age = newAge;

        pteAtTimeOfWrite.entireFormat = writePTE(pteAddress, newPTEContents, pteContents).entireFormat;

        // only adjust the counters once the write actually lands
        if (pteAtTimeOfWrite.entireFormat == pteContents.entireFormat) {
            InterlockedDecrement64(&vm.pte.globalNumOfAge[currentAge].count);
            InterlockedIncrement64(&vm.pte.globalNumOfAge[newAge].count);
            return 0;
        }

        // raced: loop again with the fresh contents
        pteContents.entireFormat = pteAtTimeOfWrite.entireFormat;
    }
}


/**
 * @brief The leaf-region equivalent of getRegionAge. Since leaf regions do not maintain the
 *        numOfAge counters, it derives the age by scanning the PTEs for the highest age among
 *        the valid ones.
 * @param region The region to inspect
 * @return The highest age of any valid PTE in the region, or MAXULONG64 if the region has none
 **/
static ULONG64 getLeafRegionAge(PTE_REGION *region) {
    pte *pteAddress = getFirstPTEInRegion(region);
    LONG64 maxAge = -1;

    for (int i = 0; i < vm.config.pte_entries_per_pagetable; i++) {
        pte contents;
        contents.entireFormat = ReadULong64NoFence(&pteAddress[i].entireFormat);
        if (contents.validFormat.valid == 1 && (LONG64) contents.validFormat.age > maxAge) {
            maxAge = contents.validFormat.age;
        }
    }

    return (maxAge < 0) ? MAXULONG64 : (ULONG64) maxAge;
}


/**
 *@brief Ages a singular leaf PTE region. Same shape as the former ageRegion, minus the per-region
 *       numOfAge bookkeeping; the region's age for list placement is scanned from the PTEs instead.
 * @param parentPTE The PTE one layer up, pointing at the leaf page table to age.
 * @param threadInfo The thread info of the caller. Used for debugging / the list helpers.
 * @return How many PTEs were aged
 * @pre The leaf region (the page table parentPTE points to) must be locked, and must hold at
 *      least one valid PTE
 * @post The leaf region remains locked
 */
ULONG64 ageLeafRegion(pte *parentPTE, PTHREAD_INFO threadInfo) {
    PTE_REGION *region;
    pte *pteAddress;
    ULONG64 previousAge;
    ULONG64 newAge;
    ULONG64 numPTEsAged;


    // descend from the parent into its leaf page table, then map that table back to its region
    region = getPTERegion(getStartOfLowerPagetable(parentPTE));

    numPTEsAged = 0;
    previousAge = getLeafRegionAge(region);


#if DBG
    ASSERT(region->ageListNumber == (LONG64) previousAge)
#endif

    pteAddress = getFirstPTEInRegion(region);

    for (int i = 0; i < vm.config.pte_entries_per_pagetable; i++) {
        numPTEsAged += ageLeafPTE(pteAddress, region);
        pteAddress++;
    }

    newAge = getLeafRegionAge(region);

    shiftAgeList(parentPTE, previousAge, newAge, threadInfo);

    return numPTEsAged;
}

__forceinline
boolean isSecondToLastLayer(int layer) {
    return layer == vm.config.number_of_page_table_layers - 2;
}

DWORD ager_thread(LPVOID info) {
    SetThreadDescription(GetCurrentThread(), L"Ager");

    HANDLE events[2];
    DWORD returnEvent;
    events[0] = vm.events.agerStart;
    events[1] = vm.events.systemShutdown;

    PPAGETABLE currentPageTable;
    pte *currentPTE;
    pte *parentPTE;
    pfn *page;

    pte localPTE;
    pte newPTEContents;
    pte PTEContentsAtTimeOfWrite;

    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;
    currentPageTable = vm.pte.table;
    ULONG64 initialTotalPTEsToAge;
    LONG64 totalPTEsLeftToAge;
    ULONG64 numPTEsAged;
    ULONG64 newAge;
    ULONG64 oldAge;

    ULONG64 row = 0;
    ULONG64 layer = 0;

    boolean has_valid_or_transition;
    boolean can_clear_access_bit;

    has_valid_or_transition = true;
    can_clear_access_bit = !has_valid_or_transition;
    while (TRUE) {
        returnEvent = WaitForMultipleObjects(2, events, FALSE, INFINITE);


        //if the system shutdown event was signaled, exit
        if (returnEvent - WAIT_OBJECT_0 == 1) {
            return 0;
        }


        initialTotalPTEsToAge = ReadULong64NoFence(&vm.pte.numToAge);
        totalPTEsLeftToAge = initialTotalPTEsToAge;
        numPTEsAged = 0;


        while (numPTEsAged < totalPTEsLeftToAge) {
            lockPTE(&currentPageTable->pagetable[row]);

            for (; row < vm.config.pte_entries_per_pagetable; row++) {
                //put in agepte
                currentPTE = &currentPageTable->pagetable[row];
                localPTE.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

                // cannot age what is not valid
                if (localPTE.validFormat.valid == 0) {
                    continue;
                }

                page = getPFNfromFrameNumber(localPTE.validFormat.frameNumber);


                // I want to do recursion but with out the actual recursion, maybe a double break and a change to the stack variables
                if (localPTE.validFormat.access == 1) {
                    if (isSecondToLastLayer(layer)) {
                        setLockBit(currentPTE);
                        numPTEsAged += ageLeafRegion(currentPTE, threadInfo);
                        clearLockBit(currentPTE);
                    } else {
                        //I am delving further deeper into the tree to see if I can trim this one
                        enterPageLock(page, threadInfo);

                        // check if zero
                        if (page->valid_transition_count == 0) {
                            //then I clear the access bit and put it on age 0 list using interlocked compare exchange
                            newPTEContents.entireFormat = localPTE.entireFormat;
                            newPTEContents.validFormat.access = 0;
                            //TODO do I have to check return value
                            PTEContentsAtTimeOfWrite = writePTE(currentPTE, newPTEContents, localPTE);

                            leavePageLock(page, threadInfo);
                            continue;
                        } else {
                            // in this branch, I am diving deeper
                            leavePageLock(page, threadInfo);
                            row = 0;
                            break;
                        }
                    }
                } else {
                    // valid and unaccessed: bump in place; the helper also shifts the global histogram
                    oldAge = localPTE.validFormat.age;
                    if (ageUnnaccessedPTE(currentPTE, localPTE, &newAge) == FALSE) {
                        //raced: redo this row (the loop's row++ brings us back). No shift -- nothing moved.
                        row--;
                        continue;
                    }
                    numPTEsAged += (newAge - oldAge);

                    // parent aged oldAge -> newAge, so move its child page table's region between
                    // age-lists; currentPTE's lock bit is that region's lock. The bump MUST come before
                    // the lock: setLockBit flips a bit writePTE's CAS compares, so a held lock fails it.
                    setLockBit(currentPTE);
                    shiftAgeList(currentPTE, oldAge, newAge, threadInfo);
                    clearLockBit(currentPTE);
                    continue;
                }
            }

            //NOTICE (Resolved but keeping comment) I think there is a huge bug, where there is no way for me to clear a parent access bit with absolute certainty that there is no valid ptes,
            // there is a pathological case where someone slips in and accesses that pte after
            // The slip in does not matter because the only person who can change a valid to invalid needs the lock we hold.



            unlockPTE(currentPTE);

            //if the row is zero then we are going into the tree otherwise we are going out/looping around  (that means we have to change tables' lists
            if (row == 0) {

                currentPageTable = (PPAGETABLE) getStartOfLowerPagetable(currentPTE);
                layer++;
            } else {
                //in this case we have finished aging everything and we can pop back up to the first pte
                if (layer == 0) {
                    ASSERT(currentPTE == &vm.pte.table[0].pagetable[511])
                    row = 0;
                    currentPageTable = &vm.pte.table[0];
                } else {
                    // this is the case where we are going in the more inside pte

                    // I am getting the start of the upper page
                    currentPageTable = (PPAGETABLE) PAGE_ALIGN((ULONG64) getHigherLevelPTEAddress(currentPTE));
                    // I want to return to the previous row I was iterating at
                    layer--;

                    // I am resetting row to the correct value (we were mid loop, broke, went deeper and now we have to return to that same place for fair aging)
                    row = (currentPTE - ((pte *) PAGE_ALIGN((ULONG64) currentPTE)));
                }
            }
        }


        InterlockedExchange64(&vm.pte.numToAge, 0);
        InterlockedExchange((volatile LONG *) &vm.misc.agingInProgress,FALSE);
    }
}
