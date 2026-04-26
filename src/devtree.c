// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/devicetree_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
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

u32 DT_GetPropertyValueULONG(APTR key, const char *propname, u32 def_val, BOOL check_parent)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	u32 ret = def_val;

	while (key != NULL)
	{
		APTR p = DT_FindProperty(key, (CONST_STRPTR)propname);

		if (p != NULL || check_parent == FALSE)
		{
			if (p != NULL || DT_GetPropLen(p) >= 4)
			{
				ret = *(u32 *)DT_GetPropValue(p);
			}

			return ret;
		}
		key = DT_GetParent(key);
	}
	return ret;
}

s32 DT_TranslateAddress(APTR *address, APTR node)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	const u32 *ranges = DT_GetPropValue(DT_FindProperty(node, (CONST_STRPTR) "ranges"));
	const u32 len = DT_GetPropLen(DT_FindProperty(node, (CONST_STRPTR) "ranges"));

	const u32 address_cells_parent = DT_GetPropertyValueULONG(DT_GetParent(node), "#address-cells", 2, FALSE);
	const u32 address_cells_child = DT_GetPropertyValueULONG(node, "#address-cells", 2, FALSE);
	const u32 size_cells = DT_GetPropertyValueULONG(node, "#size-cells", 2, FALSE);
	const u32 cells_per_record = address_cells_parent + address_cells_child + size_cells;

	for (const u32 *i = ranges; i < ranges + len / sizeof(u32); i += cells_per_record)
	{
		u32 phys_vc4 = (u32)DT_GetNumber(i, address_cells_child);
		u32 phys_cpu = (u32)DT_GetNumber(i + address_cells_child, address_cells_parent);
		u32 size = (u32)DT_GetNumber(i + address_cells_child + address_cells_parent, size_cells);
		KprintfH("[devtree] %s: phys_vc4=0x%08lx phys_cpu=0x%08lx size=0x%08lx\n", __func__, (ULONG)phys_vc4, (ULONG)phys_cpu, (ULONG)size);

		if ((u32)*address >= phys_vc4 && (u32)*address < phys_vc4 + size)
		{
			u32 offset = phys_cpu - phys_vc4;
			*address += offset;
			KprintfH("[devtree] %s: Virtual address=0x%08lx\n", __func__, *address);
			return 0;
		}
	}
	Kprintf("[devtree] %s: No translation found for address 0x%08lx\n", __func__, address);
	return -1;
}

APTR DT_GetBaseAddressVirtual(CONST_STRPTR alias)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	APTR key = DT_OpenKey(alias);
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key %s\n", __func__, alias);
		return NULL;
	}

	const APTR parent = DT_GetParent(key);
	const u32 address_cells_parent = DT_GetPropertyValueULONG(parent, "#address-cells", 2, FALSE);
	APTR address = (APTR)(ULONG)DT_GetNumber(DT_GetPropValue(DT_FindProperty(key, (CONST_STRPTR) "reg")), address_cells_parent);
	DT_TranslateAddress(&address, parent);
	DT_CloseKey(key);

	return address;
}

APTR DT_GetBaseAddress(CONST_STRPTR alias)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	APTR key = DT_OpenKey(alias);
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key %s\n", __func__, alias);
		return NULL;
	}

	u32 address_cells = DT_GetPropertyValueULONG(DT_GetParent(key), "#address-cells", 2, FALSE);

	const u32 *reg = DT_GetPropValue(DT_FindProperty(key, (CONST_STRPTR) "reg"));
	if (reg != NULL)
	{
		DT_CloseKey(key);
		return (APTR)reg[address_cells - 1];
	}
	Kprintf("[devtree] %s: Failed to find reg property in key %s\n", __func__, alias);
	DT_CloseKey(key);
	return NULL;
}

CONST_STRPTR DT_GetAlias(CONST_STRPTR alias)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	APTR key = DT_OpenKey((CONST_STRPTR) "/aliases");
	if (key == NULL)
	{
		Kprintf("[devtree] %s: Failed to open key /aliases\n", __func__);
		return NULL;
	}

	APTR prop = DT_FindProperty(key, (CONST_STRPTR)alias);
	if (prop != NULL)
	{
		CONST_STRPTR value = DT_GetPropValue(prop);
		DT_CloseKey(key);
		return value;
	}
	Kprintf("[devtree] %s: Failed to find alias %s\n", __func__, alias);
	DT_CloseKey(key);
	return NULL;
}

APTR DT_FindByPHandle(APTR key, u32 phandle)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	APTR p = DT_FindProperty(key, (CONST_STRPTR) "phandle");

	if (p && *(u32 *)DT_GetPropValue(p) == phandle)
	{
		return key;
	}
	else
	{
		for (APTR c = DT_GetChild(key, NULL); c; c = DT_GetChild(key, c))
		{
			APTR found = DT_FindByPHandle(c, phandle);
			if (found)
				return found;
		}
	}
	return NULL;
}

s32 DT_GetInterrupt(APTR key, u32 index)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	/* Get interrupt information
	 * We need to find the interrupt-parent's #interrupt-cells to parse the interrupts property correctly.
	 * We're looking for two interrupts: one for TX/RX events, one for link changes.
	 */
	APTR root = DT_OpenKey((CONST_STRPTR) "/");
	APTR interrupt_parent = DT_FindByPHandle(root, DT_GetPropertyValueULONG(root, "interrupt-parent", 0, TRUE));
	if (interrupt_parent == NULL)
	{
		Kprintf("[devtree] %s: Failed to find interrupt-parent\n", __func__);
		DT_CloseKey(root);
		return -1;
	}
	const u32 interrupt_cells = DT_GetPropertyValueULONG(interrupt_parent, "#interrupt-cells", 1, FALSE);

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

	const u32 interrupt_type = (u32)DT_GetNumber(ptr, 1);
	u32 interrupt_number = (u32)DT_GetNumber(ptr + 1, 1);

	if (interrupt_type == 0)
		interrupt_number += 32u; // SPI
	else if (interrupt_type == 1)
		interrupt_number += 16u; // PPI

#ifdef DEBUG_HIGH
	const u32 interrupt_flags = (u32)DT_GetNumber(ptr + 2, 1);
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

	KprintfH("[devtree] %s: Found interrupt: irq=%lu trigger=%s\n", __func__, (ULONG)interrupt_number, trigger);
#endif

	DT_CloseKey(root);

	return (s32)interrupt_number;
}
