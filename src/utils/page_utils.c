//
// Created by nrper on 7/17/2025.
//

#include "../../include/utils/page_utils.h"
#include "../../include/variables/globals.h"

VOID
pfnInbounds(pfn* trimmed) {
    if (trimmed < pfnStart || trimmed >= endPFN) {
        DebugBreak();
    }
}
ULONG64 getFrameNumber(pfn* pfn) {
    return (ULONG64)(pfn - pfnStart);
}

pfn* getPFNfromFrameNumber(ULONG64 frameNumber) {
    return pfnStart + frameNumber;
}

VOID removeFromMiddleOfList(pListHead head,LIST_ENTRY* entry) {
    LIST_ENTRY* prevEntry = entry->Blink;
    LIST_ENTRY* nextEntry = entry->Flink;
    prevEntry->Flink = nextEntry;
    nextEntry->Blink = prevEntry;
    head->length--;
}