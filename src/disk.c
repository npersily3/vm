//
// Created by nrper on 6/16/2025.
//
#include "disk.h"
#include <stdio.h>

ULONG64
most_free_disk_portion(VOID) {
    ULONG64 max = 0;
    ULONG64 index = 0;

    for (int i = 0; i < NUMBER_OF_DISK_DIVISIONS; ++i) {
        if (max <= number_of_open_slots[i]) {
            max = number_of_open_slots[i];
            index = i;
        }
    }
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
        end = start + DISK_DIVISION_SIZE_IN_PAGES;
    } else {
        end = start + DISK_DIVISION_SIZE_IN_PAGES - 1;
    }

    // this will make it go over 1 so it can find the transfer slot
    if(number_of_open_slots[freePortion] == 0) {
        DebugBreak();
    }

    while (start <= end) {
        if (*start == FALSE) {
            *start = TRUE;
            number_of_open_slots[freePortion] -= 1;
            return start - diskActive;
        }
        start++;
    }

    printf("couldn't find free page");
    DebugBreak();
    return -1;
}

VOID
set_disk_space_free(ULONG64 diskIndex) {
    // mark the place as inactive
    diskActive[diskIndex] = FALSE;

    ULONG64 diskIndexRoundedDown;
    // this rounds down the disk index given to the nearest disk division. it works by taking advantage of the fact that
    // there is truncation when stuff cant go in easily
    diskIndexRoundedDown = diskIndex / DISK_DIVISION_SIZE_IN_PAGES;

    if (diskIndexRoundedDown >= NUMBER_OF_DISK_DIVISIONS) {
        diskIndexRoundedDown = NUMBER_OF_DISK_DIVISIONS - 1;
    }

    number_of_open_slots[diskIndexRoundedDown] += 1;
}