//
// Created by nrper on 6/17/2025.
//

#ifndef VM_H
#define VM_H

DWORD testVM(LPVOID lpParam);
VOID testVMSingleThreaded(VOID);
VOID full_virtual_memory_test(VOID);
extern CRITICAL_SECTION GlobalCriticalSection;
extern HANDLE GlobalStartEvent;

#endif //VM_H
