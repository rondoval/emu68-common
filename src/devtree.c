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
 * dt_map - map [addr, addr + len) across the bus @bus through its "ranges" or
 * "dma-ranges".
 *
 * Both list <child address, parent address, size> records: the child address
 * and the size in the bus's own #address-cells and #size-cells, the parent
 * address in the #address-cells of the bus's parent.  An empty property means
 * both sides use the same addresses.  @to_parent: @addr is a child address and
 * the result the parent's ("ranges": registers as the CPU sees them); else the
 * other way round ("dma-ranges": CPU memory as the bus's DMA masters see it).
 * One level only: on Emu68 the parent of /soc and /scb is the root.
 *
 * Returns FALSE if the bus has no such property, no record covers the whole
 * span, or the result does not fit 32 bits.  @from_range, if not NULL, gets
 * the covering record's extent on @addr's side, [start, end), at most 32 bits
 * (both 0 for an empty property, which maps everything).
 */
static BOOL dt_map(APTR DeviceTreeBase, APTR bus, CONST_STRPTR propname, BOOL to_parent, u64 addr, u64 len, u32 *out,
                   u32 *from_range)
{
	APTR prop = DT_FindProperty(bus, propname);
	if (prop == NULL)
	{
		Kprintf("[devtree] %s: %s has no %s to map 0x%lx%08lx with\n", __func__, DT_GetKeyName(bus), propname,
				(ULONG)(addr >> 32), (ULONG)addr);
		return FALSE;
	}

	const u32 child_cells = DT_GetPropertyValueULONG(DeviceTreeBase, bus, "#address-cells", 2);
	const u32 parent_cells = DT_GetPropertyValueULONG(DeviceTreeBase, DT_GetParent(bus), "#address-cells", 2);
	const u32 size_cells = DT_GetPropertyValueULONG(DeviceTreeBase, bus, "#size-cells", 1);
	const u32 record_cells = child_cells + parent_cells + size_cells;

	const u32 *rec = DT_GetPropValue(prop);
	u32 cells_left = DT_GetPropLen(prop) / sizeof(u32);

	u64 mapped = addr; /* an empty property: the same address on both sides */
	u64 range_start = 0, range_end = 0;
	BOOL covered = (cells_left == 0);
	for (; !covered && record_cells != 0 && cells_left >= record_cells; rec += record_cells, cells_left -= record_cells)
	{
		const u64 child = DT_GetNumber(rec, child_cells);
		const u64 parent = DT_GetNumber(rec + child_cells, parent_cells);
		const u64 size = DT_GetNumber(rec + child_cells + parent_cells, size_cells);
		KprintfT("[devtree] %s: %s child=0x%lx%08lx parent=0x%lx%08lx size=0x%lx%08lx\n", __func__, propname,
				 (ULONG)(child >> 32), (ULONG)child, (ULONG)(parent >> 32), (ULONG)parent, (ULONG)(size >> 32), (ULONG)size);

		/* Does this record cover the span, i.e. from <= addr and addr + len <= from + size?
		 * Tested as offsets: from + size wraps to 0 for a record that ends at the top
		 * of the address space, addr - from cannot wrap once addr >= from.  The same
		 * offset then applies on the other side. */
		const u64 from = to_parent ? child : parent;
		const u64 to = to_parent ? parent : child;
		if (addr >= from && addr - from < size && len <= size - (addr - from))
		{
			mapped = to + (addr - from);
			range_start = from;
			range_end = from + size;
			covered = TRUE;
		}
	}

	if (!covered || mapped > 0xFFFFFFFFULL || len - 1u > 0xFFFFFFFFULL - mapped)
	{
		Kprintf("[devtree] %s: %s of %s does not map 0x%lx%08lx\n", __func__, propname, DT_GetKeyName(bus),
				(ULONG)(addr >> 32), (ULONG)addr);
		return FALSE;
	}

	KprintfT("[devtree] %s: %s: 0x%08lx\n", __func__, propname, (ULONG)mapped);
	*out = (u32)mapped;
	if (from_range != NULL)
	{
		from_range[0] = range_start > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (u32)range_start;
		from_range[1] = range_end > 0xFFFFFFFFULL || range_end == 0 ? 0xFFFFFFFFu : (u32)range_end;
	}
	return TRUE;
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
	u32 virt;
	if (!dt_map(DeviceTreeBase, bus, (CONST_STRPTR) "ranges", TRUE, DT_GetNumber(rec, addr_cells), 1, &virt, NULL))
		return NULL;
	return (APTR)virt;
}

BOOL DT_GetDmaAddress(APTR DeviceTreeBase, APTR key, u32 phys, u32 len, u32 *dma)
{
	return len != 0 && dt_map(DeviceTreeBase, DT_GetParent(key), (CONST_STRPTR) "dma-ranges", FALSE, phys, len, dma, NULL);
}

BOOL DT_GetDmaWindow(APTR DeviceTreeBase, APTR key, u32 phys, u32 len, u32 *dma, u32 *cpu_start, u32 *cpu_end)
{
	u32 range[2];
	if (len == 0 || !dt_map(DeviceTreeBase, DT_GetParent(key), (CONST_STRPTR) "dma-ranges", FALSE, phys, len, dma, range))
		return FALSE;
	*cpu_start = range[0];
	*cpu_end = range[1];
	return TRUE;
}

BOOL DT_GetEmu68Range(APTR DeviceTreeBase, CONST_STRPTR name, u32 *base, u32 *size)
{
	APTR key = DT_OpenKey((CONST_STRPTR) "/emu68");
	if (key == NULL)
		return FALSE;

	APTR prop = DT_FindProperty(key, name);
	BOOL found = DT_GetPropLen(prop) >= 2u * sizeof(u32);
	if (found)
	{
		const u32 *v = DT_GetPropValue(prop); /* big-endian cells, as Emu68 writes them: native here */
		*base = v[0];
		*size = v[1];
	}
	DT_CloseKey(key);
	return found;
}

/* The path the property @name of the node @table (/aliases, /__symbols__) holds; NULL if none. */
static CONST_STRPTR dt_path_of(APTR DeviceTreeBase, CONST_STRPTR table, CONST_STRPTR name)
{
	APTR key = DT_OpenKey(table);
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key %s\n", __func__, table);
		return NULL;
	}

	CONST_STRPTR value = DT_GetPropValue(DT_FindProperty(key, name));
	DT_CloseKey(key);

	if (value == NULL)
		Kprintf("[devtree] %s: %s has no %s\n", __func__, table, name);
	return value;
}

CONST_STRPTR DT_GetAlias(APTR DeviceTreeBase, CONST_STRPTR alias)
{
	return dt_path_of(DeviceTreeBase, (CONST_STRPTR) "/aliases", alias);
}

APTR DT_OpenSymbol(APTR DeviceTreeBase, CONST_STRPTR label)
{
	CONST_STRPTR path = dt_path_of(DeviceTreeBase, (CONST_STRPTR) "/__symbols__", label);
	return path != NULL ? DT_OpenKey(path) : NULL;
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
