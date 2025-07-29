//
// Created by nrper on 7/17/2025.
//

#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#include "../variables/structures.h"

void acquireLock(PULONG64 lock);
void releaseLock(PULONG64 lock);
BOOL tryAcquireLock(PULONG64 lock);


VOID validateList(pListHead head);
VOID acquire_srw_exclusive(sharedLock* lock, PTHREAD_INFO info);
VOID release_srw_exclusive(sharedLock* lock);

VOID acquire_srw_shared(sharedLock* lock);
VOID release_srw_shared(sharedLock* lock);



VOID enterPageLock(pfn* page, PTHREAD_INFO info);
VOID leavePageLock(pfn* page, PTHREAD_INFO info);

#if DBG

VOID debug_acquire_srw_shared(sharedLock* lock, const char* fileName, int lineNumber);
VOID debug_release_srw_shared(sharedLock* lock, const char* fileName, int lineNumber);
VOID debug_acquire_srw_exclusive(sharedLock* lock, PTHREAD_INFO info);
VOID debug_release_srw_exclusive(sharedLock* lock);

#endif


#endif //THREAD_UTILS_H
