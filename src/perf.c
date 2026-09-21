// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * perf — delta reporting for the per-stage timing samples (see perf.h).
 *
 * Gated on DEBUG_SINK, not on the PROFILE tier: this cold reporter ships in the
 * shared libcommon.a, and the component that calls it decides its own tier. If
 * this followed emu68-common's tier, TIER=off PROFILE=lwip-amiga would compile
 * it away and leave lwip-amiga unlinkable. Callers below the PROFILE tier drop
 * the call entirely (perf.h stubs perf_report), so nothing reaches this.
 */

#ifdef DEBUG_SINK

#define PERF_IMPL /* declare perf_report rather than stubbing it out */
#include <perf.h>
#include <debug.h>

/* lock_prof's two slot names (see perf.h). Rodata; lives beside perf_report
 * because it is only referenced when the reporter exists — the profile tier
 * always carries a sink, the same reason perf_report itself is DEBUG_SINK-gated. */
const char *const lock_prof_names[LOCKPROF_NSLOTS] = { "lockwait", "lockhold" };

void perf_report(struct perf *pf)
{
	for (u32 i = 0; i < pf->pf_nslots; i++) {
		struct perf_counter *c = &pf->pf_slots[i];
		if (c->pc_count == 0)
			continue;
		PrintPistorm("[%s] %s: n=%lu sum=%luus avg=%lu.%luus max=%luus\n",
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

void perf_hist_report(struct perf_hist *ph)
{
	u32 samples = 0;
	for (u32 i = 0; i <= ph->ph_nbounds; i++)
		samples += ph->ph_buckets[i];
	if (samples == 0)
		return;

	PrintPistorm("[%s] hist %s: n=%lu", (ULONG)ph->ph_prefix, (ULONG)ph->ph_name, (ULONG)samples);
	for (u32 i = 0; i <= ph->ph_nbounds; i++) {
		if (ph->ph_buckets[i] == 0)
			continue;
		if (i < ph->ph_nbounds)
			PrintPistorm(" <=%lu:%lu", (ULONG)ph->ph_bounds[i], (ULONG)ph->ph_buckets[i]);
		else
			PrintPistorm(" >%lu:%lu", (ULONG)ph->ph_bounds[i - 1], (ULONG)ph->ph_buckets[i]);
		ph->ph_buckets[i] = 0;
	}
	PrintPistorm("\n");
}

#endif /* DEBUG_SINK */
