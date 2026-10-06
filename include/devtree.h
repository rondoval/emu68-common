/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
#ifndef DEV_TREE_H
#define DEV_TREE_H

#ifdef __INTELLISENSE__
#include <clib/devicetree_protos.h>
#else
#include <proto/devicetree.h>
#endif

#include <types.h>

/*
 * Helpers on top of devicetree.resource.
 *
 * The resource: the caller opens it once, checks it, and passes it in.  With
 * __NOLIBBASE__ that is the local its own DT_ calls already go through:
 *
 *     APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
 *
 * A NULL key is accepted by every helper and reads as "nothing there": the
 * default, NULL or -1 comes back.
 */

/* The cells at @ptr as one number, most significant cell first.  Holds two cells. */
u64 DT_GetNumber(const u32 *ptr, u32 cells);

/* The first cell of a property of @key; @def_val if it is missing or shorter than a cell. */
u32 DT_GetPropertyValueULONG(APTR DeviceTreeBase, APTR key, const char *propname, u32 def_val);

/* The path an entry of /aliases names; NULL if there is no such alias. */
CONST_STRPTR DT_GetAlias(APTR DeviceTreeBase, CONST_STRPTR alias);

/* The node at or below @key whose "phandle" is @phandle; NULL if none. */
APTR DT_FindByPHandle(APTR DeviceTreeBase, APTR key, u32 phandle);

/*
 * The address the 68k side reaches a node's registers at: the address of the
 * @index-th "reg" record, translated through the "ranges" of the node's parent
 * bus.  NULL if there is no such record or the bus does not map it.
 */
APTR DT_GetBaseAddressVirtual(APTR DeviceTreeBase, APTR key, u32 index);

/*
 * The absolute GIC interrupt number (SPI + 32, PPI + 16) of the @index-th entry
 * of the node's "interrupts"; -1 if the entry or the interrupt parent is missing.
 */
s32 DT_GetInterrupt(APTR DeviceTreeBase, APTR key, u32 index);

#endif // DEV_TREE_H
