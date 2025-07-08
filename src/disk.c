//
// Created by nrper on 6/16/2025.
//
#include "disk.h"
#include "util.h"
#include "pages.h"

#include <stdio.h>

ULONG64
most_free_disk_portion(VOID) {
    ULONG64 max = 0;
    ULONG64 index = 0;

    EnterCriticalSection(lockNumberOfSlots);
    for (int i = 0; i < NUMBER_OF_DISK_DIVISIONS; ++i) {
        if (max <= number_of_open_slots[i]) {
            max = number_of_open_slots[i];
            index = i;
        }
    }
    LeaveCriticalSection(lockNumberOfSlots);

    return index;
}

ULONG64
get_free_disk_index(VOID) {
    // get the subsection of the diskSlots array that you will be searching through
    ULONG64 freePortion;
    boolean* start;
    boolean* end;

    freePortion = most_free_disk_portion();

    start = diskActive + freePortion * DISK_DIVISION_SIZE_IN_PAGES;


    // accounts for extra slot case
    if (freePortion == NUMBER_OF_DISK_DIVISIONS - 1) {
        end = start + DISK_DIVISION_SIZE_IN_PAGES + 2;
    } else {
        end = start + DISK_DIVISION_SIZE_IN_PAGES;
    }

    // this will make it go over 1 so it can find the transfer slot
    if(number_of_open_slots[freePortion] == 0) {
        return COULD_NOT_FIND_SLOT;
    }

    //ask if they should go in the loop
    //nptodo make diskActive and numOpenSlots only one lock
    EnterCriticalSection(lockDiskActive);
    while (start < end) {
        if (*start == FALSE) {
            *start = TRUE;

            EnterCriticalSection(lockNumberOfSlots);
            number_of_open_slots[freePortion] -= 1;
            LeaveCriticalSection(lockNumberOfSlots);

            LeaveCriticalSection(lockDiskActive);
            return start - diskActive;
        }
        start++;
    }
    LeaveCriticalSection(lockDiskActive);

    printf("couldn't find free page");
    DebugBreak();
    return -1;
}

VOID
set_disk_space_free(ULONG64 diskIndex) {


    ULONG64 diskIndexSection;
    // this rounds down the disk index given to the nearest disk division. it works by taking advantage of the fact that
    // there is truncation when stuff cant go in easily

    //if (diskActiveVa[diskIndex] == NULL) {DebugBreak();}
   // if ( ((ULONG64) diskActiveVa[diskIndex] & 0x1)) { DebugBreak();}
    //diskActiveVa[diskIndex] = (PULONG64) ((ULONG64) diskActiveVa[diskIndex] | 0x1);



    diskIndexSection = diskIndex / DISK_DIVISION_SIZE_IN_PAGES;

    if (diskIndexSection >= NUMBER_OF_DISK_DIVISIONS) {
        diskIndexSection = NUMBER_OF_DISK_DIVISIONS - 1;
    }


    EnterCriticalSection(lockDiskActive);

    EnterCriticalSection(lockNumberOfSlots);
    number_of_open_slots[diskIndexSection] += 1;
    LeaveCriticalSection(lockNumberOfSlots);

    if (diskActive[diskIndex] == FALSE) {
        DebugBreak();
    }


    diskActive[diskIndex] = FALSE;
    LeaveCriticalSection(lockDiskActive);



    SetEvent(writingStartEvent);
}
