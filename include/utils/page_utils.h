//
// Created by nrper on 7/17/2025.
//

#ifndef PAGE_UTILS_H
#define PAGE_UTILS_H

#include "../variables/structures.h"

VOID pfnInbounds(pfn* trimmed);
ULONG64 getFrameNumber(pfn* pfn);
pfn* getPFNfromFrameNumber(ULONG64 frameNumber);
VOID removeFromMiddleOfList(pListHead head,pfn* page);
pfn* RemoveFromHeadofPageList(pListHead head);
VOID addPageToTail(pListHead head, pfn* page);


#endif //PAGE_UTILS_H
