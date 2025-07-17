//
// Created by nrper on 7/16/2025.
//

#ifndef INIT_THREADS_H
#define INIT_THREADS_H

#include "../variables/structures.h"

VOID initThreads(VOID);
VOID createThreads(VOID);

HANDLE createNewThread(LPTHREAD_START_ROUTINE ThreadFunction, PTHREAD_INFO ThreadContext);
VOID initializePageTableLocks(VOID);
VOID initCriticalSections(VOID);
VOID createEvents(VOID);





#endif //INIT_THREADS_H
