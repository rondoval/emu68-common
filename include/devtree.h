#ifndef DEV_TREE_H
#define DEV_TREE_H

#ifdef __INTELLISENSE__
#include <clib/devicetree_protos.h>
#else
#include <proto/devicetree.h>
#endif

APTR DT_FindByPHandle(APTR key, ULONG phandle);
CONST_STRPTR DT_GetAlias(CONST_STRPTR alias);

APTR DT_GetBaseAddress(CONST_STRPTR alias);
APTR DT_GetBaseAddressVirtual(CONST_STRPTR alias);
ULONG DT_GetPropertyValueULONG(APTR key, const char *propname, ULONG def_val, BOOL check_parent);
uint64_t DT_GetNumber(const ULONG *ptr, ULONG cells);
WORD DT_TranslateAddress(APTR *address, APTR node);
int DT_GetInterrupt(APTR key, ULONG index);

#endif // DEV_TREE_H