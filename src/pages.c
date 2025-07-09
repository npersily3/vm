//
// Created by nrper on 6/16/2025.
//
#include "pages.h"
#include "util.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>
#include"macros.h"

//nptodo, ideally you release locks before copying

VOID
modified_read(pte* currentPTE, ULONG64 frameNumber, ULONG64 threadNumber) {
    // Modified reading
    ULONG64 diskIndex = currentPTE->invalidFormat.diskIndex;
    ULONG64 diskAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;




    // MUPP(va, size, physical page)
    if (MapUserPhysicalPages(userThreadTransferVa[threadNumber], 1, &frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", userThreadTransferVa[threadNumber], frameNumber);
        return;
    }

    // memcpy(va,va,size)

    memcpy(userThreadTransferVa[threadNumber], (PVOID) diskAddress, PAGE_SIZE);

    if (MapUserPhysicalPages(userThreadTransferVa[threadNumber], 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", userThreadTransferVa[threadNumber]);
        return;
    }

    set_disk_space_free(diskIndex);
}

DWORD
//nptodo search for mapUserPP scatter in order to batch unmap. look at race
page_trimmer(LPVOID lpParam) {

    pfn* page;
    PULONG64 va;
    PCRITICAL_SECTION trimmedPageTableLock;
    PLIST_ENTRY trimmedEntry;
    boolean doubleBreak;

    ULONG64 localBatchSizeInPages;
    ULONG64 localBatchSizeInBytes;

    while (TRUE) {


        doubleBreak = FALSE;
        WaitForSingleObject(trimmingStartEvent, INFINITE);

        localBatchSizeInPages = min(BATCH_SIZE,headActiveList.length);
        localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;


        for (int i = 0; i < localBatchSizeInPages; ++i) {


            EnterCriticalSection(lockActiveList);
            trimmedEntry = RemoveHeadList(&headActiveList);

            if (trimmedEntry == LIST_IS_EMPTY) {
                LeaveCriticalSection(lockActiveList);

                if (i == 0) {
                    doubleBreak = TRUE;
                }
                break;
            }

            page = container_of(trimmedEntry, pfn, entry);
            LeaveCriticalSection(lockActiveList);

            trimmedPageTableLock = getPageTableLock(page->pte);

            EnterCriticalSection(trimmedPageTableLock);
            va = (PULONG64) pte_to_va(page->pte);


            // unmap everpage, ask how I can string these together
            if (MapUserPhysicalPages(va, 1, NULL) == FALSE) {
                DebugBreak();
                printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
                return 1;
            }

            page->pte->transitionFormat.mustBeZero = 0;
            page->pte->transitionFormat.contentsLocation = MODIFIED_LIST;

          //  EnterCriticalSection(lockModifiedList);
            acquireLock(&lockModList);
            InsertTailList(&headModifiedList, &page->entry);
            releaseLock(&lockModList);
            //LeaveCriticalSection(lockModifiedList);

            LeaveCriticalSection(trimmedPageTableLock);

        }
        if (doubleBreak) {
            continue;
        }

        SetEvent(writingStartEvent);

    }
}


//modified to standBy
DWORD diskWriter(LPVOID lpParam) {


    pfn* page;
    ULONG64 frameNumber;
    ULONG64 diskIndex;
    ULONG64 diskAddressArray[BATCH_SIZE];
    ULONG64 frameNumberArray[BATCH_SIZE];
    pfn* pfnArray[BATCH_SIZE];
    PULONG64 vaArray[BATCH_SIZE];
    ULONG64 diskByteAddress;
    boolean doubleBreak;
    PCRITICAL_SECTION writingPageTableLock;


    ULONG64 counter;
    ULONG64 localBatchSizeInPages;
    ULONG64 localBatchSizeInBytes;

    PLIST_ENTRY newPageEntry;

    while (TRUE) {


        doubleBreak = FALSE;
        counter = 0;
        WaitForSingleObject(writingStartEvent, INFINITE);

        localBatchSizeInPages = BATCH_SIZE;


        for (int i = 0; i < localBatchSizeInPages; ++i) {


          //  EnterCriticalSection(lockModifiedList);
              acquireLock(&lockModList);
            newPageEntry = headModifiedList.entry.Flink;
            // similar to mapPage, tryEnter page table, if I can then procceed, otherwise i-- and continue, release locks before hand


            if (newPageEntry == &headModifiedList.entry) {
                if (i == 0) {
                    doubleBreak = TRUE;
                }
                localBatchSizeInPages = i;
                localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;
              //  LeaveCriticalSection(lockModifiedList);
                releaseLock(&lockModList);
                break;
            }
            counter = 0;

            page = container_of(newPageEntry, pfn, entry);

            writingPageTableLock = getPageTableLock(page->pte);

            if (TryEnterCriticalSection(writingPageTableLock) == FALSE) {

                if (counter == MB(1)) {
                    DebugBreak();
                }
                i--;
                counter++;
                ///LeaveCriticalSection(lockModifiedList);
                releaseLock(&lockModList);
                continue;
            }
            counter = 0;
            removeFromMiddleOfList(&headModifiedList, newPageEntry);

            //LeaveCriticalSection(lockModifiedList);
            releaseLock(&lockModList);

            ULONG64 va = (ULONG64) pte_to_va(page->pte);
            vaArray[i] = va;

            pfnArray[i] = page;

            frameNumber = getFrameNumber(page);
            frameNumberArray[i] = frameNumber;



            //nptodo put finding a disk slot not on the pte lock and do it at the beinnign
            diskIndex = get_free_disk_index();


            if (diskIndex == COULD_NOT_FIND_SLOT) {
                localBatchSizeInPages = i;
                localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;

 //               EnterCriticalSection(lockModifiedList);
                acquireLock(&lockModList);
                InsertTailList(&headModifiedList, &page->entry);
                releaseLock(&lockModList);
                //LeaveCriticalSection(lockModifiedList);

                if (i == 0) {
                    doubleBreak = TRUE;
                }
                LeaveCriticalSection(writingPageTableLock);

                break;
            }
            //
            // if (diskActiveVa[diskIndex] == NULL || ((ULONG64)diskActiveVa[diskIndex] & 0x1)) {
            //     diskActiveVa[diskIndex] = va;
            // } else {
            //     DebugBreak();
            // }


            // modified writing
             diskByteAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;
            diskAddressArray[i] = diskByteAddress;



            page->isBeingWritten = TRUE;
            page->diskIndex = diskIndex;
            page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;

            LeaveCriticalSection(writingPageTableLock);
        }

        // breaks if i is zero
        if (doubleBreak) {
            SetEvent(writingEndEvent);
            continue;
        }

        // map to transfer va

        if (MapUserPhysicalPages(transferVaWriting, localBatchSizeInPages, frameNumberArray) == FALSE) {
            DWORD error = GetLastError();
            DebugBreak();
            printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaWriting, frameNumber);
            return 1;
        }

        for (int i = 0; i < localBatchSizeInPages; ++i) {
            // copy from transfer to disk

            memcpy(diskAddressArray[i], (PVOID) ((ULONG64) transferVaWriting + i * PAGE_SIZE) , PAGE_SIZE);

       //     ASSERT(checkVa( (PULONG64)((ULONG64)transferVaWriting + i * PAGE_SIZE), vaArray[i]));
        }

        if (MapUserPhysicalPages(transferVaWriting, localBatchSizeInPages, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
            return 1;
        }



        for (int i = 0; i < localBatchSizeInPages; ++i) {
            page = pfnArray[i];


            writingPageTableLock = getPageTableLock(page->pte);
            EnterCriticalSection(writingPageTableLock);

            //if it has been rescued, free up the disk space and do not put it on the standby list
            if(pfnArray[i]->isBeingWritten == FALSE) {
                set_disk_space_free(page->diskIndex);
            } else {
                //check if disk page is empty
                //checkIfPageIsZero(diskAddressArray[i]);

                pfnArray[i]->isBeingWritten = FALSE;
                EnterCriticalSection(lockStandByList);
                InsertTailList(&headStandByList, &pfnArray[i]->entry);
                LeaveCriticalSection(lockStandByList);

            }
            // need to hold pte lock you could optimize to reduce enters and leave by grouping the locks, you need, but that is for later
            //make sure pte then list lock
            LeaveCriticalSection(writingPageTableLock);
        }

        // this needs to be in a lock to avoid
        // the race condition where it is reset right after it is
        EnterCriticalSection(lockStandByList);
        SetEvent(writingEndEvent);
        LeaveCriticalSection(lockStandByList);


    }
}
ULONG64 modifiedLongerThanBatch() {

    PLIST_ENTRY entry;
    entry = headModifiedList.entry.Flink;

    int i;

    for (i = 0; i < BATCH_SIZE; ++i) {
        if (entry == &headModifiedList.entry) {
           break;
        }
        entry = entry->Flink;
    }
    return i;
}


BOOL
checkVa(PULONG64 start,PULONG64 va) {
    ULONG64 base;

    base = ((ULONG64)va >> 12);

    for (int i = 0; i < PAGE_SIZE / 8; ++i) {
        if (!(*start == 0 || ((*start) >> 12) == base)) {
            return false;
            DebugBreak();
        }
        start += 1;
    }
    return true;
}