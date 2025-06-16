#ifndef DISK_H
#define DISK_H

#include "util.h"

//
// Disk-related global variables (declared here, defined in init.c)
//
extern PVOID diskStart;
extern ULONG64 diskEnd;
extern boolean* diskActive;
extern ULONG64* number_of_open_slots;

//
// Disk operation functions
//
ULONG64 most_free_disk_portion(VOID);
ULONG64 get_free_disk_index(VOID);
VOID set_disk_space_free(ULONG64 diskIndex);

#endif // DISK_H