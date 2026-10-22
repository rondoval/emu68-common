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

/*
 * DT_OpenKey() of the node a label names in /__symbols__ (the labels overlays
 * refer to, e.g. "vchiq" for &vchiq; the Pi firmware's trees carry them); NULL
 * if the tree has no symbols or no such label.  For nodes without an alias.
 */
APTR DT_OpenSymbol(APTR DeviceTreeBase, CONST_STRPTR label);

/* The node at or below @key whose "phandle" is @phandle; NULL if none. */
APTR DT_FindByPHandle(APTR DeviceTreeBase, APTR key, u32 phandle);

/*
 * The address the 68k side reaches a node's registers at: the address of the
 * @index-th "reg" record, translated through the "ranges" of the node's parent
 * bus.  NULL if there is no such record or the bus does not map it.
 */
APTR DT_GetBaseAddressVirtual(APTR DeviceTreeBase, APTR key, u32 index);

/*
 * The address a device (node @key) uses to reach the CPU memory [phys, phys +
 * len) by DMA: translated through the "dma-ranges" of the node's parent bus
 * (on the Pi 4, /soc: the lowest GiB at 0xC0000000).  FALSE if the bus has no
 * "dma-ranges" or no record of it covers the whole span.
 */
BOOL DT_GetDmaAddress(APTR DeviceTreeBase, APTR key, u32 phys, u32 len, u32 *dma);

/*
 * As DT_GetDmaAddress(), plus the window the translation came from.
 *
 * A "dma-ranges" record maps one block of CPU memory to one block of bus
 * addresses with a single constant offset.  *cpu_start and *cpu_end are the
 * CPU physical bounds of the record that covered [phys, phys + len), start
 * inclusive, end exclusive: every CPU address in that block is reachable by
 * the device, at its CPU address + (*dma - phys).  Translate one buffer, then
 * test any other buffer against the window instead of looking each one up:
 *
 *     reachable = addr >= cpu_start && addr < cpu_end && size <= cpu_end - addr;
 *     bus       = addr + (dma - phys);
 *
 * Pi 4, /soc <0xC0000000 0x0 0x00000000 0x40000000>: the lowest GiB, so
 * *cpu_start = 0, *cpu_end = 0x40000000, *dma = phys + 0xC0000000; a buffer
 * above 1 GiB is out of the window.
 *
 * *cpu_end is 0xFFFFFFFF when the record reaches past 4 GiB, and an empty
 * "dma-ranges" (the bus maps everything one to one) gives [0, 0xFFFFFFFF).
 * Both outputs are untouched when FALSE is returned.
 */
BOOL DT_GetDmaWindow(APTR DeviceTreeBase, APTR key, u32 phys, u32 len, u32 *dma, u32 *cpu_start, u32 *cpu_end);

/*
 * One of Emu68's memory carve-outs: /emu68/@name, two cells <base size> (e.g.
 * "unicam-mem", "vc4-mem").  FALSE if Emu68 does not set it.
 */
BOOL DT_GetEmu68Range(APTR DeviceTreeBase, CONST_STRPTR name, u32 *base, u32 *size);

/*
 * The absolute GIC interrupt number (SPI + 32, PPI + 16) of the @index-th entry
 * of the node's "interrupts"; -1 if the entry or the interrupt parent is missing.
 */
s32 DT_GetInterrupt(APTR DeviceTreeBase, APTR key, u32 index);

#endif // DEV_TREE_H
