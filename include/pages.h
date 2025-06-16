#ifndef PAGES_H
#define PAGES_H

#include "util.h"

//
// Page management functions
//
VOID modified_read(ULONG64 arbitrary_va, pte* currentPTE, pfn* freePage);
VOID page_trimmer(VOID);
VOID checkVa(PULONG64 va);

#endif // PAGES_H