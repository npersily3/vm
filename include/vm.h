//
// Created by nrper on 6/17/2025.
//

#ifndef VM_H
#define VM_H

DWORD testVM(LPVOID lpParam);
VOID full_virtual_memory_test(VOID);
VOID mapPage(ULONG64 arbitrary_va, pte* currentPTE);
VOID rescue_page(ULONG64 arbitrary_va, pte* currentPTE);
VOID startTrimmer(VOID);


extern HANDLE GlobalStartEvent;;

#endif //VM_H
