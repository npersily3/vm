#include "disk/disk.h"
//
// Created by nrper on 9/21/2025.
//
#include "variables/structures.h"
DWORD scheduler_thread(LPVOID info) {
    PTHREAD_INFO threadInfo;

    threadInfo = (PTHREAD_INFO) info;

    while (TRUE) {

        if (WaitForSingleObject(vm.events.systemShutdown, 0) == WAIT_OBJECT_0) {
            return 0;
        }



        _sleep(100);
    }
}