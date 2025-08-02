#ifndef DISK_H
#define DISK_H

#include "../variables/structures.h"

//

//
// Disk operation functions
//
ULONG64 most_free_disk_portion(VOID);
ULONG64 get_free_disk_index(VOID);
VOID wipePage (ULONG64 diskIndex);
VOID set_disk_space_free(ULONG64 diskIndex);
pfn* standByToDisk();
ULONG64 get_free_disk_bit(PULONG64 diskSlot);
ULONG64 getMultipleDiskIndices(PULONG64 diskIndices);
#endif // DISK_H