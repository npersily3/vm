/* This file contains macros for manipulating doubly linked lists.
 * The code for these macros was written by Microsoft.
 * Edited By Noah Persily to accomodate his structs */
#ifndef VM_MACROS_H
#define VM_MACROS_H

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "structures.h"

FORCEINLINE
        VOID
InitializeListHead(
        __out PLIST_ENTRY ListHead
)
{
ListHead->Flink = ListHead;
ListHead->Blink = ListHead;

}

__checkReturn
        BOOLEAN
FORCEINLINE
IsListEmpty(
        __in const pListHead ListHead
)
{
return (BOOLEAN)(ListHead->entry.Flink == &ListHead->entry);
}

FORCEINLINE
        BOOLEAN
RemoveEntryList(
        __inout pListHead ListHead,
        __in PLIST_ENTRY Entry
)
{
PLIST_ENTRY Blink;
PLIST_ENTRY Flink;

Flink = Entry->Flink;
Blink = Entry->Blink;
Blink->Flink = Flink;
Flink->Blink = Blink;
ListHead->length--;
return (BOOLEAN)(Flink == Blink);
}

FORCEINLINE
        PLIST_ENTRY
RemoveHeadList(
        __inout pListHead ListHead
)
{
PLIST_ENTRY Flink;
PLIST_ENTRY Entry;



Entry = ListHead->entry.Flink;
Flink = Entry->Flink;



        if (Entry == &ListHead->entry) { return LIST_IS_EMPTY;}


ListHead->entry.Flink = Flink;
Flink->Blink = &ListHead->entry;
ListHead->length--;


return Entry;
}

FORCEINLINE
        PLIST_ENTRY
RemoveTailList(
        __inout pListHead ListHead
)
{
PLIST_ENTRY Blink;
PLIST_ENTRY Entry;

Entry = ListHead->entry.Blink;
Blink = Entry->Blink;
ListHead->entry.Blink = Blink;
Blink->Flink = &ListHead->entry;
ListHead->length--;
return Entry;
}

FORCEINLINE
        VOID
// changed to keep track of length
InsertTailList(
        __inout pListHead ListHead,
        __inout __drv_aliasesMem PLIST_ENTRY Entry
)
{

        if (Entry == &vm.lists.modified.entry || Entry == &vm.lists.active.entry
               || Entry == &vm.lists.standby.entry) {DebugBreak(); return;}
PLIST_ENTRY Blink;


Blink = ListHead->entry.Blink;
Entry->Flink = &ListHead->entry;
Entry->Blink = Blink;
Blink->Flink = Entry;
ListHead->entry.Blink = Entry;
        ListHead->length++;

}


FORCEINLINE
        VOID
InsertHeadList(
        __inout pListHead ListHead,
        __inout __drv_aliasesMem PLIST_ENTRY Entry
)
{


PLIST_ENTRY Flink;

Flink = ListHead->entry.Flink;
Entry->Flink = Flink;
Entry->Blink = &ListHead->entry;
Flink->Blink = Entry;
ListHead->entry.Flink = Entry;
ListHead->length++;
}

FORCEINLINE
        VOID
AppendTailList(
        __inout pListHead ListHead,
        __inout PLIST_ENTRY ListToAppend
)
{
PLIST_ENTRY ListEnd = ListHead->entry.Blink;

ListHead->entry.Blink->Flink = ListToAppend;
ListHead->entry.Blink = ListToAppend->Blink;
ListToAppend->Blink->Flink = &ListHead->entry;
ListToAppend->Blink = ListEnd;
// Note: This function appends an entire list, so length update would need to count the appended list
// For now, we'll leave this as a TODO since it requires counting the appended list
}

FORCEINLINE
        PSINGLE_LIST_ENTRY
PopEntryList(
        __inout PSINGLE_LIST_ENTRY ListHead
)
{
PSINGLE_LIST_ENTRY FirstEntry;
FirstEntry = ListHead->Next;
if (FirstEntry != NULL) {
ListHead->Next = FirstEntry->Next;
}

return FirstEntry;
}


FORCEINLINE
        VOID
PushEntryList(
        __inout PSINGLE_LIST_ENTRY ListHead,
        __inout __drv_aliasesMem PSINGLE_LIST_ENTRY Entry
)
{
Entry->Next = ListHead->Next;
ListHead->Next = Entry;
}
#endif //VM_MACROS_H
