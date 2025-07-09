#ifndef DISK_H
#define DISK_H

#include "util.h"

//
// Disk-related global variables (declared here, defined in init.c)
//
extern PVOID diskStart;
extern ULONG64 diskEnd;
extern PULONG64 diskActive;
extern ULONG64* number_of_open_slots;

//
// Disk operation functions
//
ULONG64 most_free_disk_portion(VOID);
ULONG64 get_free_disk_index(VOID);
VOID wipePage (ULONG64 diskIndex);
VOID set_disk_space_free(ULONG64 diskIndex);
pfn* standByToDisk();
ULONG64 get_free_disk_bit(PULONG64 diskSlot);

#endif // DISK_H