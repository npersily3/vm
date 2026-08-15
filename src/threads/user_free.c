#include <stdio.h>

#include "utils/pte_utils.h"
#include "variables/structures.h"
#include "threads/user_free.h"

#include "disk/disk.h"
#include "threads/user_thread.h"
#include "utils/page_utils.h"
#include "utils/thread_utils.h"

BOOL freeVA(ULONG64 va, PTHREAD_INFO thread_info) {
}

BOOL freePTE(pte *currentPTE, PTHREAD_INFO thread_info) {
    pte newContents;
    newContents.entireFormat = 0;

    pte oldContents;
    pte writeContents;
    PVOID va;

    pfn* parentPage;
    pte* parentPTE;


    va = pte_to_va(currentPTE);


    lockPTE(currentPTE);

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
        freeDiskPage(oldContents, thread_info);
    } else {
        if (oldContents.validFormat.valid == 1) {
            freeActivePage(oldContents, thread_info);
        } else {
            freeTransitionPage(oldContents, thread_info);
        }
        parentPTE = getHigherLevelPTEAddress(currentPTE);
        ASSERT(parentPTE.validFormat.valid == TRUE);
        parentPage = getPFNfromFrameNumber(parentPTE->validFormat.frameNumber);

        enterPageLock(parentPage, thread_info);

        parentPage->valid_transition_count--;
    }






    // at this point in time the pte is zero and we have the previous contents
    unlockPTE(currentPTE);
}

BOOL freeActivePage(pte pteContents, PTHREAD_INFO thread_info) {
    
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
            freeDiskPage(pteContents, thread_info);
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
