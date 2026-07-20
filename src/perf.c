// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * perf — delta reporting for the per-stage timing samples (see perf.h).
 * DEBUG builds only; the probes and this reporter compile out otherwise.
 */

#ifdef DEBUG

#include <perf.h>
#include <debug.h>

void perf_report(struct perf *pf)
{
	for (u32 i = 0; i < pf->pf_nslots; i++) {
		struct perf_counter *c = &pf->pf_slots[i];
		if (c->pc_count == 0)
			continue;
		Kprintf("[%s] %s: n=%lu sum=%luus avg=%lu.%luus max=%luus\n",
			(ULONG)pf->pf_prefix, (ULONG)pf->pf_names[i],
			(ULONG)c->pc_count, (ULONG)c->pc_sum_us,
			(ULONG)(c->pc_sum_us / c->pc_count),
			(ULONG)(c->pc_sum_us * 10 / c->pc_count % 10),
			(ULONG)c->pc_max_us);
		c->pc_count = 0;
		c->pc_sum_us = 0;
		c->pc_max_us = 0;
	}
}

#endif /* DEBUG */
