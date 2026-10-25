// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * hw_claim — a claim on hardware without an owner field of its own; see
 * hw_claim.h.
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#include <proto/exec.h>
#endif

#include <exec/execbase.h>

#include <hw_claim.h>
#include <strutil.h>

/**
 * hw_claim_take() - Claim the hardware called @name for the caller.
 * @SysBase: exec
 * @claim:   the claim's storage; must stay allocated while it is held
 * @name:    the hardware's name, at most HW_CLAIM_NAME_MAX - 1 characters
 *
 * Return: TRUE when the claim is now held; FALSE when another driver holds it
 * (or @name does not fit), and nothing is held.
 */
BOOL hw_claim_take(struct ExecBase *SysBase, struct hw_claim *claim, CONST_STRPTR name)
{
	if (strlcpy(claim->hc_Name, (const char *)name, sizeof(claim->hc_Name)) >= sizeof(claim->hc_Name))
	{
		claim->hc_Name[0] = '\0';
		return FALSE;
	}

	claim->hc_Sem.ss_Link.ln_Name = claim->hc_Name;
	claim->hc_Sem.ss_Link.ln_Pri = 0;

	Forbid();
	BOOL unclaimed = FindSemaphore((STRPTR)claim->hc_Name) == NULL;
	if (unclaimed)
		AddSemaphore(&claim->hc_Sem); /* initialises it, too */
	Permit();

	if (!unclaimed)
		claim->hc_Name[0] = '\0';
	return unclaimed;
}

/**
 * hw_claim_release() - Give up a claim taken with hw_claim_take().
 * @SysBase: exec
 * @claim:   the claim; one not held is left alone
 */
void hw_claim_release(struct ExecBase *SysBase, struct hw_claim *claim)
{
	if (claim->hc_Name[0] == '\0')
		return;

	RemSemaphore(&claim->hc_Sem);
	claim->hc_Name[0] = '\0';
}
