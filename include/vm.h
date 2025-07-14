//
// Created by nrper on 6/17/2025.
//

#ifndef VM_H
#define VM_H

#define REDO_FAULT TRUE

DWORD testVM(LPVOID lpParam);
VOID full_virtual_memory_test(VOID);
BOOL mapPage(ULONG64 arbitrary_va, pte* currentPTE, LPVOID threadContext, PCRITICAL_SECTION currentPageTableLock);
BOOL rescue_page(ULONG64 arbitrary_va, pte* currentPTE);
BOOL pageFault(PULONG_PTR arbitrary_va,  LPVOID lpParam);
BOOL zeroOnePage (pfn* page, ULONG64 theadNumber);
BOOL zeroMultiplePages (PULONG64 frameNumbers, ULONG64 batchSize);


extern HANDLE GlobalStartEvent;;

#endif //VM_H
