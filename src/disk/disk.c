//
// Created by nrper on 6/16/2025.
//
#include "../../include/disk/disk.h"

#include "variables/structures.h"
#include "variables/globals.h"
#include <stdio.h>

ULONG64
most_free_disk_portion(VOID) {
    ULONG64 max = 0;
    ULONG64 index = 0;

    EnterCriticalSection(lockNumberOfSlots);
    for (int i = 0; i < config.number_of_disk_divisions; ++i) {
        if (max <= number_of_open_slots[i]) {
            max = number_of_open_slots[i];
            index = i;
        }
    }
    LeaveCriticalSection(lockNumberOfSlots);

    return index;
}

ULONG64 getMultipleDiskIndices(PULONG64 diskIndices) {
    ULONG64 freePortion;
    PULONG64 start;
    PULONG64 end;
    ULONG64 bitOffset;
    ULONG64 returnValue;
    ULONG64 numDiskSlotsFilled = 0;

    BOOL isNotAtEnd = TRUE;
    BOOL arrayIsNotFull = TRUE;
    freePortion = most_free_disk_portion();


    start = diskActive + freePortion * ( config.disk_division_size_in_pages / 64);

    if(number_of_open_slots[freePortion] == 0) {
        return COULD_NOT_FIND_SLOT;
    }


    // accounts for the extra slot case
    end = start + ((ULONG64) config.disk_division_size_in_pages / (ULONG64)64);
    if (freePortion == config.number_of_disk_divisions - 1) {
        end += 1;
    }



    // TODO make it wrap and have a writer thread ulong past in that is the last location
    while (isNotAtEnd && arrayIsNotFull) {

        if (*start != FULL_DISK_SPACE) {

            bitOffset = get_free_disk_bit(start);

            if (bitOffset == 64) {
                 start++;
            }

            InterlockedDecrement64((volatile LONG64 *)&number_of_open_slots[freePortion]);
            returnValue = 8 * sizeof(ULONG64) * (start - diskActive) + bitOffset;
            diskIndices[numDiskSlotsFilled] = returnValue;
            numDiskSlotsFilled++;

        } else {
            start++;
        }
        isNotAtEnd = start < end ;
        arrayIsNotFull =  numDiskSlotsFilled <  BATCH_SIZE;

    }
    if (isNotAtEnd == FALSE) {
      if ( numDiskSlotsFilled == 0) {
          return COULD_NOT_FIND_SLOT;
      }
    }   return numDiskSlotsFilled;
}

ULONG64 get_free_disk_index(VOID) {
    ULONG64 freePortion;
    PULONG64 start;
    PULONG64 end;
    ULONG64 bitOffset;
    ULONG64 returnValue;

    freePortion = most_free_disk_portion();


    start = diskActive + freePortion * (config.disk_division_size_in_pages/64);

    if(number_of_open_slots[freePortion] == 0) {
        return COULD_NOT_FIND_SLOT;
    }


    // accounts for the extra slot case
    end = start + ( config.disk_division_size_in_pages  / (ULONG64)64);
    if (freePortion == config.number_of_disk_divisions - 1) {
        end += 1;
    }

    //for every ULONG in the section
    for (; start < end; start++) {
        if (*start != FULL_DISK_SPACE) {

            bitOffset = get_free_disk_bit(start);

            if (bitOffset == 64) {
                continue;
            }

            InterlockedDecrement64((volatile LONG64 *) &number_of_open_slots[freePortion]);
            returnValue = 8 * sizeof(ULONG64) * (start - diskActive) + bitOffset;

            return returnValue;
        }
    }

    return COULD_NOT_FIND_SLOT;
}
ULONG64 get_free_disk_bit(PULONG64 diskSlot) {
    ULONG64 oldDiskSlotContents;
    ULONG64 newDiskSlotContents;
    ULONG64 oldDiskSlotComparator;
    ULONG64 modifiedDiskSlotContents;

    ULONG64 i;
    ULONG64 newBit;

    oldDiskSlotContents = *diskSlot;
    modifiedDiskSlotContents = oldDiskSlotContents;


    for (i = 0; i < 64; i++) {
        if ((modifiedDiskSlotContents & 1) == DISK_ACTIVE) {
            modifiedDiskSlotContents >>= 1;
        } else {
            newBit = ((ULONG64) 1) << i;
            newDiskSlotContents = oldDiskSlotContents | newBit;

            oldDiskSlotComparator = InterlockedCompareExchange64((volatile LONG64 *) diskSlot,
                (LONG64)newDiskSlotContents,
                (LONG64) oldDiskSlotContents);

            if (oldDiskSlotComparator == oldDiskSlotContents) {
                return i;
            } else {
                oldDiskSlotContents = oldDiskSlotComparator;
                modifiedDiskSlotContents = oldDiskSlotContents;
                i = MAXULONG64;
            }
        }
    }
    return i;
}



VOID
set_disk_space_free(ULONG64 diskIndex) {

    ULONG64 diskMetaDataIndex;
    ULONG64 newDiskSlotContents;
    PULONG64 diskMetaDateAddress;
    ULONG64 oldDiskSlotContents;

    ULONG64 bitOffset;
    ULONG64 diskHasChanged;


    ULONG64 diskIndexSection;
    // this rounds down the disk index given to the nearest disk division. it works by taking advantage of the fact that
    // there is truncation when stuff cant go in easily


    diskIndexSection = (ULONG64) diskIndex /  (ULONG64) config.disk_division_size_in_pages;

    if (diskIndexSection >= config.number_of_disk_divisions) {
        diskIndexSection = config.number_of_disk_divisions - 1;
    }


    // round down to the nearest ULONG64
    diskMetaDataIndex = (diskIndex >> 6);

    diskMetaDateAddress = diskActive + diskMetaDataIndex;

    oldDiskSlotContents = *diskMetaDateAddress;


    bitOffset = diskIndex & (63);
    newDiskSlotContents = oldDiskSlotContents & ~((ULONG64)1 << bitOffset);

    //T1
        //oldDiskSlotContents 1111
        //NewDiskSlotContents 1110
    //T2
        //oldDiskSlotContents 1111
        //new diskSlotsContents 1101




    //dASSERT(FALSE)
    while (true) {
        diskHasChanged = InterlockedCompareExchange64((PLONG64) diskMetaDateAddress,
            (LONG64) newDiskSlotContents,
            (LONG64) oldDiskSlotContents);

        //t1
        //diskhasChanged = 1111
        if (diskHasChanged == oldDiskSlotContents) {
            break;
        }
        oldDiskSlotContents = diskHasChanged;
    }


    //if the disk previously had zero slots
    if (InterlockedIncrement64((PLONG64) &number_of_open_slots[diskIndexSection]) == 1) {
        SetEvent(writingStartEvent);
    }
}


