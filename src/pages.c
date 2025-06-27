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
    if (MapUserPhysicalPages(transferVaToRead[threadNumber], 1, &frameNumber) == FALSE) {
        printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVaToRead[threadNumber], frameNumber);
        return;
    }

    // memcpy(va,va,size)

    memcpy(transferVaToRead[threadNumber], (PVOID) diskAddress, PAGE_SIZE);

    if (MapUserPhysicalPages(transferVaToRead[threadNumber], 1, NULL) == FALSE) {
        printf("full_virtual_memory_test : could not unmap VA %p\n", transferVaToRead[threadNumber]);
        return;
    }

    set_disk_space_free(diskIndex);
}

DWORD
page_trimmer(LPVOID lpParam) {


    while (TRUE) {


        WaitForSingleObject(trimmingStartEvent, INFINITE);

        pfn* page;
        PULONG64 va;

        EnterCriticalSection(&lockPageTable);

        ULONG64 localBatchSizeInPages;
        ULONG64 localBatchSizeInBytes;
        localBatchSizeInPages = min(BATCH_SIZE,headActiveList.length);
        localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;


        for (int i = 0; i < localBatchSizeInPages; ++i) {




            EnterCriticalSection(&lockActiveList);
            page = container_of(RemoveHeadList(&headActiveList), pfn, entry);
            LeaveCriticalSection(&lockActiveList);


            va = (PULONG64) pte_to_va(page->pte);


            // unmap everpage, ask how I can string these together
            if (MapUserPhysicalPages(va, 1, NULL) == FALSE) {
                DebugBreak();
                printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
                return 1;
            }

            page->pte->transitionFormat.mustBeZero = 0;
            page->pte->transitionFormat.contentsLocation = MODIFIED_LIST;

            EnterCriticalSection(&lockModifiedList);
            InsertTailList(&headModifiedList, &page->entry);
            LeaveCriticalSection(&lockModifiedList);
        }

        LeaveCriticalSection(&lockPageTable);

        SetEvent(writingStartEvent);

    }
}

//modified to standBy
DWORD diskWriter(LPVOID lpParam) {

    while (TRUE) {



        WaitForSingleObject(writingStartEvent, INFINITE);


        //think about case where modified pagfe is rescued and there bsize -1 pgaes on the list
       // ULONG64 localBatchSize


        pfn* page;
        ULONG64 frameNumber;
        ULONG64 diskIndex;
        ULONG64 diskAddressArray[BATCH_SIZE];
        ULONG64 frameNumberArray[BATCH_SIZE];
        pfn* pfnArray[BATCH_SIZE];
        ULONG64 diskByteAddress;
        boolean doubleBreak;






        EnterCriticalSection(&lockPageTable);
        // add a head struct that has a len and a entry
        ULONG64 localBatchSizeInPages;
        ULONG64 localBatchSizeInBytes;
        doubleBreak = FALSE;

        localBatchSizeInPages = min(BATCH_SIZE, headModifiedList.length);

        localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;

        if (localBatchSizeInPages == 0) {
            LeaveCriticalSection(&lockPageTable);
            continue;
        }

        for (int i = 0; i < localBatchSizeInPages; ++i) {


            EnterCriticalSection(&lockModifiedList);
            page = container_of(RemoveHeadList(&headModifiedList), pfn, entry);
            LeaveCriticalSection(&lockModifiedList);

            ULONG64 va = (ULONG64) pte_to_va(page->pte);

            pfnArray[i] = page;

            frameNumber = getFrameNumber(page);
            frameNumberArray[i] = frameNumber;



            diskIndex = get_free_disk_index();
            if (diskIndex == COULD_NOT_FIND_SLOT) {
                localBatchSizeInPages = i;
                localBatchSizeInBytes = localBatchSizeInPages * PAGE_SIZE;
                InsertTailList(&headModifiedList, &page->entry);

                if (i == 0) {
                    doubleBreak = TRUE;
                }

                break;
            }


            if (diskActiveVa[diskIndex] != NULL) {DebugBreak();}
            diskActiveVa[diskIndex] = va;

            //nptodo multiple of each thread therefore I need more locks.start with multiple faulting threads
            // array of pagetable locks similar to disk array

            // modified writing
             diskByteAddress = (ULONG64) diskStart + diskIndex * PAGE_SIZE;
            diskAddressArray[i] = diskByteAddress;


            page->diskIndex = diskIndex;
            page->pte->transitionFormat.contentsLocation = STAND_BY_LIST;
            page->pte->transitionFormat.frameNumber = frameNumber;
        }
        // breaks if i is zero
        if (doubleBreak) {
            LeaveCriticalSection(&lockPageTable);
            continue;
        }

        // map to transfer va

        if (MapUserPhysicalPages(transferVa, localBatchSizeInPages, frameNumberArray) == FALSE) {
            DWORD error = GetLastError();
            DebugBreak();
            printf("full_virtual_memory_test : could not map VA %p to page %llX\n", transferVa, frameNumber);
            return 1;
        }

        for (int i = 0; i < localBatchSizeInPages; ++i) {
            // copy from transfer to disk
            memcpy(diskAddressArray[i], (PVOID) ((ULONG64) transferVa + i * PAGE_SIZE) , PAGE_SIZE);
        }

        // unmap transfer and set things to zero
        memset(transferVa, 0, localBatchSizeInBytes);

        if (MapUserPhysicalPages(transferVa, localBatchSizeInPages, NULL) == FALSE) {
            DebugBreak();
            printf("full_virtual_memory_test : could not unmap VA %p\n", transferVa);
            return 1;
        }


        EnterCriticalSection(&lockStandByList);
        for (int i = 0; i < localBatchSizeInPages; ++i) {


            InsertTailList(&headStandByList, &pfnArray[i]->entry);


        }
        // this needs to be in a lock to avoid
        // the race condition where it is reset right after it is
        SetEvent(writingEndEvent);

        LeaveCriticalSection(&lockStandByList);

        LeaveCriticalSection(&lockPageTable);



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