/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
#ifndef _MINLIST_H
#define _MINLIST_H

#include <exec/lists.h>

/* This is to avoid KS v45 requirement*/
inline static void _NewMinList(struct MinList *list)
{
    list->mlh_Head = (struct MinNode *)&list->mlh_Tail;
    list->mlh_Tail = NULL;
    list->mlh_TailPred = (struct MinNode *)&list->mlh_Head;
}

/* NDK 3.2 added type-safe MinList wrappers (AddHeadMinList,
 * AddTailMinList, RemHeadMinList).  They are nothing more than AddHead/AddTail/
 * RemHead operating on the (struct List/Node)-cast pointers, which exist on every
 * Kickstart.  Provide them here when the NDK predates 3.2 so the drivers build
 * against any NDK while staying identical on 3.2 (where these are already defined).
 * AddHead/AddTail/RemHead resolve at the call site via the includer's proto/exec.h. */
#ifndef AddTailMinList
#define AddTailMinList(ml, mn) AddTail((struct List *)(ml), (struct Node *)(mn))
#endif
#ifndef AddHeadMinList
#define AddHeadMinList(ml, mn) AddHead((struct List *)(ml), (struct Node *)(mn))
#endif
#ifndef RemHeadMinList
#define RemHeadMinList(ml) ((struct MinNode *)RemHead((struct List *)(ml)))
#endif
#ifndef RemoveMinNode
#define RemoveMinNode(mn) Remove((struct Node *)(mn))
#endif

#endif