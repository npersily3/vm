//
// Created by nrper on 7/30/2025.
//

#ifndef USER_FREE_H
#define USER_FREE_H
#include "variables/structures.h"


BOOL freeVA(ULONG64 va, PTHREAD_INFO thread_info);

BOOL freePTE(pte *currentPTE, PTHREAD_INFO thread_info);

BOOL freeActivePage(pte pteContents, PTHREAD_INFO thread_info);

BOOL freeTransitionPage(pte pteContents, PTHREAD_INFO thread_info);

BOOL freeDiskPage(pte pteContents, PTHREAD_INFO thread_info);


#endif //USER_FREE_H
