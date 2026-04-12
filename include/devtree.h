/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
#ifndef DEV_TREE_H
#define DEV_TREE_H

#ifdef __INTELLISENSE__
#include <clib/devicetree_protos.h>
#else
#include <proto/devicetree.h>
#endif

#include <types.h>

APTR DT_FindByPHandle(APTR key, u32 phandle);
CONST_STRPTR DT_GetAlias(CONST_STRPTR alias);

APTR DT_GetBaseAddress(CONST_STRPTR alias);
APTR DT_GetBaseAddressVirtual(CONST_STRPTR alias);
u32 DT_GetPropertyValueULONG(APTR key, const char *propname, u32 def_val, BOOL check_parent);
u64 DT_GetNumber(const u32 *ptr, u32 cells);
s32 DT_TranslateAddress(APTR *address, APTR node);
s32 DT_GetInterrupt(APTR key, u32 index);

#endif // DEV_TREE_H