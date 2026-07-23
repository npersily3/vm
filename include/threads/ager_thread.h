//
// Created by nrper on 9/21/2025.
//

#ifndef AGER_THREAD_H
#define AGER_THREAD_H


ULONG64 getRegionAge(PTE_REGION* region);
ULONG64 ageLeafPTE(pte* pteAddress, PTE_REGION* region);
ULONG64 ageLeafRegion(pte* parentPTE, PTHREAD_INFO threadInfo);
boolean ageUnnaccessedPTE(pte* pteAddress, pte pteContents, ULONG64* newAge);
VOID shiftAgeList(pte* parentPTE, ULONG64 oldAge, ULONG64 newAge, PTHREAD_INFO threadInfo);
DWORD ager_thread(LPVOID info);
#endif //AGER_THREAD_H
