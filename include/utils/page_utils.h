//
// Created by nrper on 7/17/2025.
//

#ifndef PAGE_UTILS_H
#define PAGE_UTILS_H

#include "../variables/structures.h"

VOID pfnInbounds(pfn* trimmed);
ULONG64 getFrameNumber(pfn* pfn);
pfn* getPFNfromFrameNumber(ULONG64 frameNumber);
VOID removeFromMiddleOfList(pListHead head,pfn* page, PTHREAD_INFO threadInfo);
pfn* RemoveFromHeadofPageList(pListHead head, PTHREAD_INFO threadInfo);
VOID addPageToTail(pListHead head, pfn* page, PTHREAD_INFO threadInfo);
pfn* removeBatchFromList(pListHead headToRemove, pListHead headToAdd, PTHREAD_INFO threadInfo);
pfn* RemoveFromHeadWithListLockHeld(pListHead head, PTHREAD_INFO threadInfo) ;


#endif //PAGE_UTILS_H
