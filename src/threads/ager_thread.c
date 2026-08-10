//
// Created by nrper on 9/20/2025.
//
#include <stdio.h>

#include "utils/page_utils.h"
#include "utils/pte_regions_utils.h"
#include "utils/pte_utils.h"
#include "utils/stats.h"
#include "utils/thread_utils.h"
#include "variables/structures.h"


/**
 * @brief Gets the age of the oldest pte in a region by scanning its page table.
 * @param region The region to get the age of
 * @return The highest age among the region's valid PTEs, or -1 if it has none
 */
LONG64 getRegionAge(PTE_REGION *region) {
    pte *pteAddress = getFirstPTEInRegion(region);
    LONG64 champ = -1;   // reigning champ: highest age seen so far; -1 => no valid pte yet

    for (ULONG64 i = 0; i < vm.config.pte_entries_per_pagetable; i++) {
        pte contents;
        contents.entireFormat = ReadULong64NoFence(&pteAddress[i].entireFormat);

        if (contents.validFormat.valid == 1 && (LONG64) contents.validFormat.age > champ) {
            champ = contents.validFormat.age;
            if (champ == MAX_AGE) {
                break;   // nothing can beat MAX_AGE, stop scanning
            }
        }
    }

    return champ;
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
 * @brief Moves a region from the oldAge age-list to the newAge one. Same list shuffle as the tail
 *        of ageLeafRegion. No-op when the age is unchanged.
 * @param region The region to re-list. The caller names it explicitly: deriving it here from a pte
 *        was ambiguous, since a pte belongs to one region (getPTERegion) but points at another
 *        (getStartOfLowerPagetable). Callers were measuring one and moving the other, which removed
 *        regions from lists they were never on -- corrupting the ring and driving length negative.
 * @param oldAge The age-list the region is currently on.
 * @param newAge The age-list the region should move to.
 * @param threadInfo The caller's thread info, forwarded to the list helpers.
 * @pre The region must be locked.
 */
VOID shiftAgeList(PTE_REGION *region, ULONG64 oldAge, ULONG64 newAge, PTHREAD_INFO threadInfo) {
    // an off-list region (freshly faulted, or dropped by the trimmer after being emptied) still
    // has to be re-listed even when its age did not move, or nothing ever puts it back and the
    // trimmer sees empty age lists while the region is full of active PTEs

//    && region->entry.Flink != NULL
    if (oldAge == newAge) {
        return;
    }

    // A region that isn't on any list (freshly faulted, or dropped after being emptied) has NULL
    // links, so there is nothing to unlink -- removeFromMiddle would deref NULL. Just add it.
    if (region->entry.Flink != NULL) {
        removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[oldAge], region, threadInfo);
    }
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
            return 1;
        }

        // raced: loop again with the fresh contents
        pteContents.entireFormat = pteAtTimeOfWrite.entireFormat;
    }
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
    previousAge = getRegionAge(region);




    pteAddress = getFirstPTEInRegion(region);

    for (int i = 0; i < vm.config.pte_entries_per_pagetable; i++) {
        numPTEsAged += ageLeafPTE(pteAddress, region);
        pteAddress++;
    }

    newAge = getRegionAge(region);

    // same region that previousAge/newAge were measured on
    shiftAgeList(region, previousAge, newAge, threadInfo);

    return numPTEsAged;
}

__forceinline
boolean isSecondToLastLayer(ULONG64 layer) {
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

    pfn *page;

    pte localPTE;
    pte newPTEContents;
    pte PTEContentsAtTimeOfWrite;

    PTE_REGION* region;

    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;
    currentPageTable = vm.pte.table;
    ULONG64 initialTotalPTEsToAge;
    ULONG64 totalPTEsLeftToAge;
    // ptesVisited is work attempted, ptesAdvanced is work that stuck. The quota and the rate metric
    // both key off visited: an accessed pte resets to 0 and a pte at MAX_AGE stays put, so on a hot
    // tree advanced can sit at zero forever and the loop would never terminate.
    ULONG64 ptesVisited;
    ULONG64 ptesAdvanced;
    ULONG64 newAge;
    ULONG64 newPTEAge;
    LONG64 oldAge;

    LARGE_INTEGER start, end;

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
        ptesVisited = 0;
        ptesAdvanced = 0;

        STAT_INC(threadInfo, agerWakeups);

        QueryPerformanceCounter(&start);

        while (ptesVisited < totalPTEsLeftToAge) {

            //TODO big bug where this could fail do to the trimmer working fast
            lockPTE(&currentPageTable->pagetable[row]);

            for (; row < vm.config.pte_entries_per_pagetable; row++) {
                //put in agepte
                currentPTE = &currentPageTable->pagetable[row];
                localPTE.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

                // every pte we look at is work, valid or not -- a raced retry below re-visits this
                // row and counts again, which is correct: it cost us a read and a failed CAS
                ptesVisited++;

                // cannot age what is not valid and changing
                if (localPTE.validFormat.valid == 0 || localPTE.validFormat.lock == 1) {
                    continue;
                }

                page = getPFNfromFrameNumber(localPTE.validFormat.frameNumber);


               // if it is accessed I might have to delve deeper otherwise just age in place
                if (localPTE.validFormat.access == 1) {

                    //I am delving further deeper into the tree to see if I can trim this one
                    enterPageLock(page, threadInfo);

                    // check if it has any valid or modified entries. The page lock protects this because the count will only change on a fault (gets that lock, or a repurpose)
                    if (page->valid_transition_count == 0) {
                        // the subtree skip: everything below this pte is empty, so the whole
                        // branch is dropped without descending. This is what the comb buys us
                        STAT_INC(threadInfo, agerBranch[STAT_LAYER(layer)][AGER_PRUNED]);

                        //then I clear the access bit and put it on age 0 list using interlocked compare exchange
                        newPTEContents.entireFormat = localPTE.entireFormat;
                        newPTEContents.validFormat.access = 0;


                        PTEContentsAtTimeOfWrite = writePTE(currentPTE, newPTEContents, localPTE);

                        leavePageLock(page, threadInfo);

                        region = getPTERegion(currentPTE);

                        oldAge = getRegionAge(region);

                        if (oldAge == -1) {
                            addRegionToTail(&vm.pte.ageList[0], region,threadInfo);
                        }

                        continue;
                    } else {
                        leavePageLock(page, threadInfo);
                        if (isSecondToLastLayer(layer)) {
                            STAT_INC(threadInfo, agerBranch[STAT_LAYER(layer)][AGER_LEAF_REGION]);
                            setLockBit(currentPTE);
                            ptesAdvanced += ageLeafRegion(currentPTE, threadInfo);
                            clearLockBit(currentPTE);

                            // ageLeafRegion walks the whole child table unconditionally, so that is
                            // exactly how many ptes it looked at
                            ptesVisited += vm.config.pte_entries_per_pagetable;
                        } else {
                            // in this branch, I am diving deeper
                            STAT_INC(threadInfo, agerBranch[STAT_LAYER(layer)][AGER_DESCENDED]);

                            row = 0;
                            break;
                        }
                    }

                } else {
                    // valid and unaccessed: bump in place; the helper also shifts the global histogram
                    // currentPTE lives in this region; it does not point at it. Aging currentPTE
                    // can change this region's max age, and changes nothing in the child table.
                    region = getPTERegion(currentPTE);
                    oldAge = getRegionAge(region);
                    if (ageUnnaccessedPTE(currentPTE, localPTE, &newPTEAge) == FALSE) {
                        //raced: redo this row (the loop's row++ brings us back). No shift -- nothing moved.
                        row--;
                        continue;
                    }
                    // pte ages for the work counter, region ages for the list move -- keeping them in
                    // separate locals, since oldAge > newPTEAge would underflow the unsigned subtract
                    ptesAdvanced += (newPTEAge - localPTE.validFormat.age);
                    newAge = getRegionAge(region);
                    STAT_INC(threadInfo, agerBranch[STAT_LAYER(layer)][AGER_AGED_INPLACE]);

                    // no setLockBit here: that bit guards currentPTE's *child* table, which is not the
                    // region we are moving. This region's lock is the parent of currentPageTable, and
                    // lockPTE at the top of the while loop is already holding it for the whole scan.
                    shiftAgeList(region, oldAge, newAge, threadInfo);
                    continue;
                }
            }


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

                    currentPTE = getHigherLevelPTEAddress(currentPTE);
                    // I am getting the start of the upper page
                    currentPageTable = (PPAGETABLE) PAGE_ALIGN((ULONG64)currentPTE);

                    // I want to return to the previous row I was iterating at
                    layer--;

                    // I am resetting row to the correct value (we were mid loop, broke, went deeper and now we have to return to that same place for fair aging)
                    row = (currentPTE - ((pte *) PAGE_ALIGN((ULONG64) currentPTE)));
                }
            }
        }


        QueryPerformanceCounter(&end);

        STAT_ADD(threadInfo, agerPTEsAged, ptesAdvanced);

        // the scheduler's pageAgeRate comes from here. Without it getPagesPerSecond sees an empty
        // buffer and hands back its 1000 p/s placeholder forever, which pins numToAge at a constant
        recordWork(threadInfo, end.QuadPart - start.QuadPart, ptesVisited);

        InterlockedExchange64(&vm.pte.numToAge, 0);
        InterlockedExchange((volatile LONG *) &vm.misc.agingInProgress,FALSE);
    }
}
