//
// Created by nrper on 9/20/2025.
//

#ifndef PTE_REGIONS_UTILS_H
#define PTE_REGIONS_UTILS_H
#include "variables/structures.h"

PTE_REGION* RemoveFromHeadofRegionList(pListHead head, PTHREAD_INFO threadInfo);
VOID removeFromMiddleOfPageTableRegionList(pListHead head,PTE_REGION* region, PTHREAD_INFO threadInfo);
VOID addRegionToTail(pListHead head, PTE_REGION* region, PTHREAD_INFO threadInfo);

#endif //PTE_REGIONS_UTILS_H
