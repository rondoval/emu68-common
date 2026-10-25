// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _HW_CLAIM_H
#define _HW_CLAIM_H

/*
 * hw_claim — one driver at a time on hardware that has no owner field of its
 * own (a PCI board has one: PRM_BoardOwner). While a driver holds the claim, a
 * public semaphore named after the hardware is on the system list. It is never
 * obtained: being on the list is the claim, so it has no owning task (the unit
 * may be closed from another task than opened it) and nothing to signal. Drivers
 * for the same hardware - different binaries, from ROM or from disk - need agree
 * on nothing but the name; a devicetree node name serves.
 *
 * ROM-able: all state lives in the caller-provided struct (typically in the
 * unit), which must stay allocated while the claim is held.
 */

#include <exec/types.h>
#include <exec/semaphores.h>

#define HW_CLAIM_NAME_MAX 32

struct hw_claim
{
	/* All fields are private to hw_claim.c. */
	struct SignalSemaphore hc_Sem;     /* on the public list while held */
	char hc_Name[HW_CLAIM_NAME_MAX];   /* empty while not held */
};

BOOL hw_claim_take(struct ExecBase *SysBase, struct hw_claim *claim, CONST_STRPTR name);
void hw_claim_release(struct ExecBase *SysBase, struct hw_claim *claim);

#endif /* _HW_CLAIM_H */
