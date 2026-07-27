// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * emu68_features.h — runtime detection of Emu68 firmware capabilities.
 *
 * emu68_probe_dcache_range_ops(): the raw truth, always asked of the device
 * tree.  Emu68 advertises support for the private LINE-F data-cache range
 * opcodes (the ones cache_ops.h emits inline) as the /emu68 node's
 * "dcache-range-ops" property: one u32 capability revision.  Revision 1 is
 * the contract this header understands; unknown revisions are treated as
 * unsupported.  The three-state result also distinguishes "not running under
 * Emu68 at all" (no devicetree.resource).
 *
 * emu68_has_dcache_range_ops(): the driver init gate.  Rangeops driver
 * builds MUST call this once at device init and refuse to load when it
 * returns FALSE — on any other Emu68 the opcode Line-F traps.  In LVO
 * builds (EMU68_FORCE_LVO_CACHE_OPS) it folds to TRUE without touching the
 * device tree.  Because of that fold, ONLY components that receive the
 * EMU68_FORCE_LVO_CACHE_OPS definition (the cache_ops.h consumers —
 * EMU68_CACHE_OPS_COMPONENTS in the stack CMakeLists) may call this
 * wrapper; elsewhere the gate cannot fold and would demand the firmware
 * capability from builds that never emit the opcode.  The raw probe carries
 * no such restriction.
 *
 * Callers must include <proto/exec.h> (their own __NOLIBBASE__ convention)
 * BEFORE this header — OpenResource().
 */
#ifndef _EMU68_FEATURES_H
#define _EMU68_FEATURES_H

#ifdef __INTELLISENSE__
#include <clib/devicetree_protos.h>
#else
#include <proto/devicetree.h>
#endif

#include <exec/types.h>

typedef enum emu68_probe_result
{
	EMU68_PROBE_NO_DEVICETREE = -1, /* no devicetree.resource: not running under Emu68 */
	EMU68_PROBE_ABSENT = 0,         /* DT present; property missing, wrong size, or unknown revision */
	EMU68_PROBE_PRESENT = 1,        /* /emu68 "dcache-range-ops" revision 1 */
} emu68_probe_result;

static inline emu68_probe_result emu68_probe_dcache_range_ops(void)
{
	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	if (DeviceTreeBase == NULL)
		return EMU68_PROBE_NO_DEVICETREE;

	APTR key = DT_OpenKey((CONST_STRPTR) "/emu68");
	if (key == NULL)
		return EMU68_PROBE_ABSENT;

	emu68_probe_result res = EMU68_PROBE_ABSENT;
	APTR prop = DT_FindProperty(key, (CONST_STRPTR) "dcache-range-ops");
	if (prop != NULL && DT_GetPropLen(prop) == sizeof(ULONG))
	{
		/* one big-endian cell, like phandle/msi-parent (Emu68 writes BE32) */
		const ULONG *v = DT_GetPropValue(prop);
		if (*v == 1) /* unknown revisions = incompatible */
			res = EMU68_PROBE_PRESENT;
	}
	DT_CloseKey(key);
	return res;
}

static inline BOOL emu68_has_dcache_range_ops(void)
{
#ifdef EMU68_FORCE_LVO_CACHE_OPS
	return TRUE; /* the LVO flavor never emits the opcode */
#else
	return emu68_probe_dcache_range_ops() == EMU68_PROBE_PRESENT;
#endif
}

#endif /* _EMU68_FEATURES_H */
