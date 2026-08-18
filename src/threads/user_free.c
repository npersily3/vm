#include <stdio.h>

#include "utils/pte_utils.h"
#include "variables/structures.h"
#include "threads/user_free.h"

#include "disk/disk.h"
#include "threads/ager_thread.h"
#include "threads/user_thread.h"
#include "utils/page_utils.h"
#include "utils/pte_regions_utils.h"
#include "utils/stats.h"
#include "utils/thread_utils.h"

// A pte nobody has ever touched is all zeroes: not valid, not in transition, no disk slot. It is
// worth testing the fields rather than entireFormat, since another thread can be holding the lock
// bit on an otherwise empty pte.
static boolean isPTEEmpty(pte contents) {
    return contents.validFormat.valid == 0
           && contents.transitionFormat.isTransition == 0
           && contents.invalidFormat.diskIndex == 0;
}

/**
 * @brief Frees one va: walks the tree to its leaf pte the same way a fault does, faulting in any
 *        page table that has been trimmed out along the way, then hands the leaf to freePTE.
 *
 * The walk stops one layer short of the leaf. The lock bit on the leaf's parent IS the leaf's lock
 * (lockPTE(leaf) is setLockBit(parent)), so ending the descent there means we already hold what
 * freePTE needs and can carry it straight into the call -- there is never a moment where the pte we
 * are about to free is unlocked and its page table is fair game for the trimmer.
 *
 * @param va The virtual address to free.
 * @param thread_info The thread info of the caller.
 * @retval TRUE if a committed pte was freed, FALSE if there was nothing there to free. The caller
 *         counts pages with this, so "already free" and "just freed" have to be distinguishable.
 */
BOOL freeVA(ULONG64 va, PTHREAD_INFO thread_info) {
    pte *currentPTE;
    pte *parentPTE;
    pte *leafPTE;
    pte localPTE;
    ULONG64 row;
    ULONG64 layer;
    ULONG64 lastLayer;
    ULONG64 tablesMaterialized;

    lastLayer = vm.config.number_of_page_table_layers - 1;
    tablesMaterialized = 0;

    STAT_INC(thread_info, frees);

    ASSERT(isVaValid(va))

    if (isVaValid(va) == FALSE) {
        DebugBreak();
        printf("free of invalid va %p\n", (PVOID) va);
        return FALSE;
    }

    // the same hand-over-hand descent as pageFaultEntryPoint, and for the same reason: a leaf pte
    // cannot be read at all until every page table above it is mapped, so a table that has been
    // trimmed out to disk has to be faulted back in before we can see what there is to free
    currentPTE = (pte *) vm.pte.start_of_layer[0] + parseVA_Row_value(va, 0);

    EnterCriticalSection(&vm.pte.rootLock);

    localPTE.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

    // an empty pte means nothing below it was ever faulted in, so the whole subtree is already free
    if (isPTEEmpty(localPTE)) {
        LeaveCriticalSection(&vm.pte.rootLock);
        STAT_INC(thread_info, freeWalkPTsMaterialized[0]);
        return FALSE;
    }

    // single layer config: the root pte is itself the leaf, and its lock is rootLock rather than a
    // parent's bit. Nothing to descend and nothing to fault in -- free it while we hold rootLock
    if (lastLayer == 0) {
        freePTE(currentPTE, thread_info);
        LeaveCriticalSection(&vm.pte.rootLock);
        STAT_INC(thread_info, freeWalkPTsMaterialized[0]);
        return TRUE;
    }

    if (localPTE.validFormat.valid == 0) {
        while (pageFault(currentPTE, thread_info) == REDO_FAULT) {
        }
        tablesMaterialized++;
    }

    setLockBit(currentPTE);

    // marking the path accessed is what keeps the trimmer off the tables we are still walking
    currentPTE->validFormat.access = 1;
    parentPTE = currentPTE;

    LeaveCriticalSection(&vm.pte.rootLock);

    // one iteration fewer than the faulter's descent: it stops at the leaf, we stop at the leaf's
    // parent. The last pte this loop faults in is the one whose page table holds our leaf, so on
    // exit the leaf table is mapped and its lock is ours.
    for (layer = 1; layer < lastLayer; layer++) {
        row = parseVA_Row_value(va, layer);

        // safe to read: we hold the lock bit on the pte one layer up, which is this table's lock
        currentPTE = getNextLayer(currentPTE, layer - 1, row);
        localPTE.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

        if (isPTEEmpty(localPTE)) {
            clearLockBit(parentPTE);
            STAT_INC(thread_info, freeWalkPTsMaterialized[
                         tablesMaterialized <= STATS_MAX_LAYERS ? tablesMaterialized : STATS_MAX_LAYERS]);
            return FALSE;
        }

        if (localPTE.validFormat.valid == 0) {
            while (pageFault(currentPTE, thread_info) == REDO_FAULT) {
            }
            tablesMaterialized++;
        }

        setLockBit(currentPTE);
        currentPTE->validFormat.access = 1;
        clearLockBit(parentPTE);
        parentPTE = currentPTE;
    }

    row = parseVA_Row_value(va, lastLayer);
    leafPTE = getNextLayer(parentPTE, lastLayer - 1, row);

    // the descent must land where the closed-form address arithmetic lands. Cheapest possible check
    // on the loop bounds -- an off-by-one frees a pte in the wrong layer, silently
    ASSERT(leafPTE == getLeafPTEAddress(va))

    localPTE.entireFormat = ReadULong64NoFence(&leafPTE->entireFormat);

    // the same walk cost the faulter records, paid the other way round: every table counted here was
    // one this free had to bring back from disk before it could read what to free
    STAT_INC(thread_info, freeWalkPTsMaterialized[
                 tablesMaterialized <= STATS_MAX_LAYERS ? tablesMaterialized : STATS_MAX_LAYERS]);

    if (isPTEEmpty(localPTE)) {
        clearLockBit(parentPTE);
        return FALSE;
    }

    // we are still holding parentPTE's bit, which is exactly lockPTE(leafPTE)
    freePTE(leafPTE, thread_info);

    clearLockBit(parentPTE);

    // the other half of the commit counter pageFault maintains. Only leaf ptes ever reach here, so
    // this pairs one for one with the increment there
    thread_info->committedPages--;

    return TRUE;
}

/**
 * @pre currentPTE must be locked by the caller -- freeVA holds it from the descent, since the lock
 *      it needs (the parent's bit) is the one the walk already ends on. Taking it here instead would
 *      mean releasing and re-acquiring, and in that gap the trimmer can take the page table this
 *      pte lives in.
 * @post The caller unlocks currentPTE.
 */
BOOL freePTE(pte *currentPTE, PTHREAD_INFO thread_info) {
    pte newContents;
    newContents.entireFormat = 0;

    pte oldContents;
    pte writeContents;
    PVOID va;

    pfn* parentPage;
    pte* parentPTE;


    va = pte_to_va(currentPTE);


    // at this point in time we want another access to this region to trigger a pagefault so the lock stops it
    // this is not a problem in a real os because the pte 0 already does the unmap
    if (MapUserPhysicalPages(va, 1, NULL) == FALSE) {
        printf("Failed to unmap");
        DebugBreak();
    }

    oldContents.entireFormat = ReadULong64NoFence(&currentPTE->entireFormat);

    writeContents = writePTE(currentPTE, newContents, oldContents);

    // do something maybe spin
    if (oldContents.entireFormat != writeContents.entireFormat) {
    }


    if (oldContents.validFormat.valid == FALSE && oldContents.transitionFormat.isTransition == FALSE) {
        STAT_INC(thread_info, freed[FREE_DISK]);
        freeDiskPage(oldContents, thread_info);
    } else {
        if (oldContents.validFormat.valid == 1) {
            STAT_INC(thread_info, freed[FREE_ACTIVE]);
            freeActivePage(oldContents, thread_info);
        } else {
            STAT_INC(thread_info, freed[FREE_TRANSITION]);
            freeTransitionPage(oldContents, thread_info);
        }
        parentPTE = getHigherLevelPTEAddress(currentPTE);
        ASSERT(parentPTE->validFormat.valid == TRUE)
        parentPage = getPFNfromFrameNumber(parentPTE->validFormat.frameNumber);

        enterPageLock(parentPage, thread_info);

        parentPage->valid_transition_count--;

        leavePageLock(parentPage, thread_info);
    }



    // at this point in time the pte is zero and we have the previous contents. The caller still holds
    // the lock and unlocks it once it is done with this pte
    return TRUE;
}
//This is an AI oneshot attempt, I was curious to see the progression of models
// (Opus 5)
/**
 * @pre The freed pte must be locked by the caller, and already zeroed.
 * @note That caller-held pte lock IS this region's lock -- lockPTE and enterPTERegionLock both
 *       resolve to setLockBit on the same parent pte, since the freed pte and
 *       getFirstPTEInRegion(region) share a page table. So the age-list work below is already
 *       serialized, and taking enterPTERegionLock here would spin forever on a bit we hold
 *       ourselves. Do not add it.
 */
BOOL freeActivePage(pte pteContents, PTHREAD_INFO thread_info) {
    pfn* page;
    pte* freedPTE;
    PTE_REGION* region;
    LONG64 oldAge;
    LONG64 newAge;

    page = getPFNfromFrameNumber(pteContents.validFormat.frameNumber);

    enterPageLock(page, thread_info);

    // an active page is on no list and holds no disk slot: whoever made it active already freed the
    // slot (modified_read / the standby rescue), and the trimmer has to take it out of the valid
    // state before the writer can ever see it. So there is nothing to unlink and nothing to release.
    ASSERT(page->isBeingWritten == FALSE)
    ASSERT(page->isBeingFreed == FALSE)

    // read it while it is still ours: once the page is on the free list a faulter can repurpose it
    // and overwrite page->pte
    freedPTE = page->pte;

    addPageToFreeList(page, thread_info);

    leavePageLock(page, thread_info);

    // the caller already unmapped the va, so this page stops counting against the active total
    InterlockedDecrement64(&vm.pfn.numActivePages);

    // this pte just left the valid state exactly as if it had been trimmed, so its age bucket has to
    // give the count back -- the trimmer's InterlockedDecrement64(globalNumOfAge[age]). Skipping it
    // leaks a count into a bucket forever, and the scheduler sizes numToAge off these.
    InterlockedDecrement64(&vm.pte.globalNumOfAge[pteContents.validFormat.age].count);

    // getRegionAge only looks at valid ptes, so this is the only free path that can move a region:
    // freeing a transition or disk pte leaves the region's age untouched.
    region = getPTERegion(freedPTE);

    // the caller already zeroed the pte, so what we measure now is the post-free age. The pre-free
    // age was that same max with our own pte still in the running, which is just max(newAge, ourAge)
    newAge = getRegionAge(region);
    oldAge = newAge > (LONG64) pteContents.validFormat.age ? newAge : (LONG64) pteContents.validFormat.age;

    if (newAge == -1) {
        // no valid ptes left: off the age lists entirely, the same drop the trimmer does when it
        // empties a region. This cannot go through shiftAgeList -- there is no ageList[-1] to add to.
        if (region->entry.Flink != NULL) {
            removeFromMiddleOfPageTableRegionList(&vm.pte.ageList[oldAge], region, thread_info);
        }
    } else {
        shiftAgeList(region, oldAge, newAge, thread_info);
    }

    return TRUE;
}


BOOL freeTransitionPage(pte pteContents, PTHREAD_INFO thread_info) {
    pfn* page;




    page = getPFNfromFrameNumber(pteContents.transitionFormat.frameNumber);

    enterPageLock(page, thread_info);

    // if a disk io is in place, we cannot have the contents of the physical page be changed, so we cannot add it immeadiately
    //to the free list, our options are to wait until disk io is done which blocks this thread, or set a flag so the writer knows to put
    // it on the free list
    if (page->isBeingWritten == 1) {
        page->isBeingWritten = FALSE;
        page->isBeingFreed = TRUE;
    } else {
        if (page->location == MODIFIED_LIST) {
            removeFromMiddleOfPageList(&vm.lists.modified, page, thread_info);
        } else {
            removeFromMiddleOfPageList(&vm.lists.standby, page, thread_info);
            set_disk_space_free(page->diskIndex);
        }
        addPageToFreeList(page, thread_info);
    }
    leavePageLock(page, thread_info);

    return TRUE;
}

BOOL freeDiskPage(pte pteContents, PTHREAD_INFO thread_info) {
    set_disk_space_free(pteContents.invalidFormat.diskIndex);

    return TRUE;
}
