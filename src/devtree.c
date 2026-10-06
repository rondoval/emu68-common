// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/devicetree_protos.h>
#else
#define __NOLIBBASE__ /* devicetree.resource is the DeviceTreeBase parameter of every helper */
#include <proto/devicetree.h>
#endif

#include <exec/types.h>

#include <debug.h>
#include <devtree.h>

u64 DT_GetNumber(const u32 *ptr, u32 cells)
{
	u64 value = 0;

	while (cells--)
	{
		value = (value << 32) | *ptr++;
	}
	return value;
}

u32 DT_GetPropertyValueULONG(APTR DeviceTreeBase, APTR key, const char *propname, u32 def_val)
{
	APTR p = DT_FindProperty(key, (CONST_STRPTR)propname);

	return DT_GetPropLen(p) >= sizeof(u32) ? *(const u32 *)DT_GetPropValue(p) : def_val;
}

/*
 * dt_find_up - the nearest node, starting at @key and going up, that has the
 * property; NULL if none has.
 *
 * devicetree.resource has DT_FindPropertyRecursive for this walk.  It is not
 * used until the stack may depend on that call.
 */
static APTR dt_find_up(APTR DeviceTreeBase, APTR key, CONST_STRPTR propname)
{
	/* The NULL test ends the walk above the root: DT_GetParent(NULL) is the root again */
	while (key != NULL && DT_FindProperty(key, propname) == NULL)
		key = DT_GetParent(key);
	return key;
}

/*
 * dt_translate - an address on the bus @bus, as the bus's parent sees it.
 *
 * The bus node's "ranges" lists <child address, parent address, size> records:
 * the child address and the size in the bus's own #address-cells and #size-cells,
 * the parent address in the #address-cells of the bus's parent.  An empty "ranges"
 * means both sides use the same addresses.  One level only: on Emu68 the parent of
 * /soc and /scb is the root, and its addresses are the ones the 68k side uses.
 *
 * Returns NULL if the bus has no "ranges", no record covers @addr, or the result
 * does not fit a 32-bit pointer.
 */
static APTR dt_translate(APTR DeviceTreeBase, APTR bus, u64 addr)
{
	APTR prop = DT_FindProperty(bus, (CONST_STRPTR) "ranges");
	if (prop == NULL)
	{
		Kprintf("[devtree] %s: No ranges to translate address 0x%lx%08lx with\n", __func__, (ULONG)(addr >> 32), (ULONG)addr);
		return NULL;
	}

	const u32 child_cells = DT_GetPropertyValueULONG(DeviceTreeBase, bus, "#address-cells", 2);
	const u32 parent_cells = DT_GetPropertyValueULONG(DeviceTreeBase, DT_GetParent(bus), "#address-cells", 2);
	const u32 size_cells = DT_GetPropertyValueULONG(DeviceTreeBase, bus, "#size-cells", 1);
	const u32 record_cells = child_cells + parent_cells + size_cells;

	const u32 *rec = DT_GetPropValue(prop);
	u32 cells_left = DT_GetPropLen(prop) / sizeof(u32);

	u64 translated = addr; /* an empty "ranges": the same address on both sides */
	BOOL covered = (cells_left == 0);
	for (; !covered && record_cells != 0 && cells_left >= record_cells; rec += record_cells, cells_left -= record_cells)
	{
		const u64 child = DT_GetNumber(rec, child_cells);
		const u64 parent = DT_GetNumber(rec + child_cells, parent_cells);
		const u64 size = DT_GetNumber(rec + child_cells + parent_cells, size_cells);
		KprintfT("[devtree] %s: child=0x%lx%08lx parent=0x%lx%08lx size=0x%lx%08lx\n", __func__,
				 (ULONG)(child >> 32), (ULONG)child, (ULONG)(parent >> 32), (ULONG)parent, (ULONG)(size >> 32), (ULONG)size);

		/* Does this record cover addr, i.e. child <= addr < child + size?  The upper
		 * bound is tested as an offset: child + size wraps to 0 for a record that
		 * ends at the top of the address space, addr - child cannot wrap once
		 * addr >= child.  The same offset then applies on the parent side. */
		if (addr >= child && addr - child < size)
		{
			translated = parent + (addr - child);
			covered = TRUE;
		}
	}

	if (!covered || translated > 0xFFFFFFFFULL)
	{
		Kprintf("[devtree] %s: No translation found for address 0x%lx%08lx\n", __func__, (ULONG)(addr >> 32), (ULONG)addr);
		return NULL;
	}

	KprintfT("[devtree] %s: Virtual address=0x%08lx\n", __func__, (ULONG)translated);
	return (APTR)(ULONG)translated;
}

APTR DT_GetBaseAddressVirtual(APTR DeviceTreeBase, APTR key, u32 index)
{
	/* "reg" lists <address, size> records in the cells of the node's parent bus */
	const APTR bus = DT_GetParent(key);
	const u32 addr_cells = DT_GetPropertyValueULONG(DeviceTreeBase, bus, "#address-cells", 2);
	const u32 size_cells = DT_GetPropertyValueULONG(DeviceTreeBase, bus, "#size-cells", 1);
	const u32 record_cells = addr_cells + size_cells;

	APTR reg = DT_FindProperty(key, (CONST_STRPTR) "reg");
	if (addr_cells == 0 || DT_GetPropLen(reg) / sizeof(u32) < (index + 1) * record_cells)
	{
		Kprintf("[devtree] %s: %s has no reg record %ld\n", __func__, DT_GetKeyName(key), index);
		return NULL;
	}

	const u32 *rec = (const u32 *)DT_GetPropValue(reg) + index * record_cells;
	return dt_translate(DeviceTreeBase, bus, DT_GetNumber(rec, addr_cells));
}

CONST_STRPTR DT_GetAlias(APTR DeviceTreeBase, CONST_STRPTR alias)
{
	APTR key = DT_OpenKey((CONST_STRPTR) "/aliases");
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key /aliases\n", __func__);
		return NULL;
	}

	CONST_STRPTR value = DT_GetPropValue(DT_FindProperty(key, alias));
	DT_CloseKey(key);

	if (value == NULL)
		Kprintf("[devtree] %s: Failed to find alias %s\n", __func__, alias);
	return value;
}

APTR DT_FindByPHandle(APTR DeviceTreeBase, APTR key, u32 phandle)
{
	if (key == NULL) /* DT_GetChild(NULL) would start at the root's children */
		return NULL;

	APTR p = DT_FindProperty(key, (CONST_STRPTR) "phandle");
	if (DT_GetPropLen(p) >= sizeof(u32) && *(const u32 *)DT_GetPropValue(p) == phandle)
		return key;

	for (APTR c = DT_GetChild(key, NULL); c; c = DT_GetChild(key, c))
	{
		APTR found = DT_FindByPHandle(DeviceTreeBase, c, phandle);
		if (found)
			return found;
	}
	return NULL;
}

s32 DT_GetInterrupt(APTR DeviceTreeBase, APTR key, u32 index)
{
	/* The interrupt parent is the node named by the nearest "interrupt-parent" at
	 * or above this one.  Its #interrupt-cells is the length of one entry of
	 * "interrupts".  The parent is the GIC, whose entry is <type number flags>. */
	APTR named_at = dt_find_up(DeviceTreeBase, key, (CONST_STRPTR) "interrupt-parent");
	const u32 phandle = DT_GetPropertyValueULONG(DeviceTreeBase, named_at, "interrupt-parent", 0);

	APTR root = DT_OpenKey((CONST_STRPTR) "/");
	APTR interrupt_parent = DT_FindByPHandle(DeviceTreeBase, root, phandle);
	const u32 interrupt_cells = DT_GetPropertyValueULONG(DeviceTreeBase, interrupt_parent, "#interrupt-cells", 0);
	DT_CloseKey(root);

	if (interrupt_parent == NULL || interrupt_cells < 2)
	{
		Kprintf("[devtree] %s: Failed to find a usable interrupt-parent\n", __func__);
		return -1;
	}

	APTR prop = DT_FindProperty(key, (CONST_STRPTR) "interrupts");
	if (prop == NULL)
	{
		Kprintf("[devtree] %s: Failed to find interrupts property\n", __func__);
		return -1;
	}

	const u32 *interrupts = DT_GetPropValue(prop);
	const u32 len = DT_GetPropLen(prop);

	if (len / sizeof(u32) < (index + 1) * interrupt_cells)
	{
		Kprintf("[devtree] %s: Interrupt index out of range\n", __func__);
		return -1;
	}

	const u32 *ptr = interrupts + index * interrupt_cells;

	const u32 interrupt_type = ptr[0];
	u32 interrupt_number = ptr[1];

	if (interrupt_type == 0)
		interrupt_number += 32u; // SPI
	else if (interrupt_type == 1)
		interrupt_number += 16u; // PPI

#ifdef TRACE
	const u32 interrupt_flags = interrupt_cells >= 3 ? ptr[2] : 0;
	char *trigger;
	switch (interrupt_flags & 0xf)
	{
	case 1:
		trigger = "edge rising";
		break;
	case 2:
		trigger = "edge falling";
		break;
	case 4:
		trigger = "level high";
		break;
	case 8:
		trigger = "level low";
		break;
	default:
		trigger = "unknown";
		break;
	}

	KprintfT("[devtree] %s: Found interrupt: irq=%lu trigger=%s\n", __func__, (ULONG)interrupt_number, trigger);
#endif

	return (s32)interrupt_number;
}
