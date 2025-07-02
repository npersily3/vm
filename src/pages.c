//
// Created by nrper on 6/16/2025.
//
#include "pages.h"
#include "util.h"
#include "disk.h"
#include <stdio.h>
#include <string.h>
#include"macros.h"

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

            EnterCriticalSection(lockModifiedList);
            InsertTailList(&headModifiedList, &page->entry);
            LeaveCriticalSection(lockModifiedList);

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



        EnterCriticalSection(lockModifiedList);
        localBatchSizeInPages = min(BATCH_SIZE, headModifiedList.length);

        localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;

        if (localBatchSizeInPages == 0) {
            LeaveCriticalSection(lockModifiedList);
            continue;
        }
        LeaveCriticalSection(lockModifiedList);


        for (int i = 0; i < localBatchSizeInPages; ++i) {


            EnterCriticalSection(lockModifiedList);
            newPageEntry = headModifiedList.entry.Flink;
            // similar to mapPage, tryEnter page table, if I can then procceed, otherwise i-- and continue, release locks before hand

            // i dont think I should always doublebreak
            if (newPageEntry == &headModifiedList.entry) {
                doubleBreak = TRUE;
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
                LeaveCriticalSection(lockModifiedList);
                continue;
            }
            counter = 0;
            removeFromMiddleOfList(&headModifiedList, newPageEntry);

            LeaveCriticalSection(lockModifiedList);

            ULONG64 va = (ULONG64) pte_to_va(page->pte);

            pfnArray[i] = page;

            frameNumber = getFrameNumber(page);
            frameNumberArray[i] = frameNumber;



            diskIndex = get_free_disk_index();
            if (diskIndex == COULD_NOT_FIND_SLOT) {
                localBatchSizeInPages = i;
                localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;

                EnterCriticalSection(lockModifiedList);
                InsertTailList(&headModifiedList, &page->entry);
                LeaveCriticalSection(lockModifiedList);

                if (i == 0) {
                    doubleBreak = TRUE;
                }
                LeaveCriticalSection(writingPageTableLock);

                break;
            }                 


            if (diskActiveVa[diskIndex] != NULL) {DebugBreak();}
            diskActiveVa[diskIndex] = va;



            // modified writing
             diskByteAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;
            diskAddressArray[i] = diskByteAddress;


            //nptodo ask how I should do this ordering
            page->isBeingWritten = TRUE;
            page->diskIndex = diskIndex;
            page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;
      //      page->pte->transitionFormat.frameNumber = frameNumber;


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


        // Here I acquire the standBy lock, so it cannot be rescued and mess up the standby list data.
        ////then I iterate throught the pages see if they have been rescued, copy their contents to disk if they haven't
        ///been rescued, add them to the standByList, and I set their status to no longer being written
        EnterCriticalSection(lockStandByList);
        for (int i = 0; i < localBatchSizeInPages; ++i) {
            if (pfnArray[i]->isBeingWritten == FALSE) {
                continue;
            }
            // copy from transfer to disk
            memcpy(diskAddressArray[i], (PVOID) ((ULONG64) transferVaWriting + i * PAGE_SIZE) , PAGE_SIZE);
            InsertTailList(&headStandByList, &pfnArray[i]->entry);
            pfnArray[i]->isBeingWritten = FALSE;
        }
        LeaveCriticalSection(lockStandByList);


        // unmap transfer and set things to zero
        memset(transferVaWriting, 0, localBatchSizeInBytes);

        if (MapUserPhysicalPages(transferVaWriting, localBatchSizeInPages, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaWriting);
            return 1;
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

VOID
checkVa(PULONG64 va) {
    va = (PULONG64) ((ULONG64)va & ~(PAGE_SIZE - 1));
    for (int i = 0; i < PAGE_SIZE / 8; ++i) {
        if (!(*va == 0 || *va == (ULONG64) va)) {
            DebugBreak();
        }
        va += 1;
    }
}