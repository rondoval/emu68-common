// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * perf — per-stage timing samples for throughput work.
 *
 * A component declares an instance: a slot array, a matching name table, and
 * a `struct perf` binding them under a report prefix. Probes bracket a code
 * stage with PERF_T0/PERF_ADD, accumulating {count, µs sum, µs max} per slot
 * from the BCM 1 MHz system timer (timing.h get_time()). perf_report()
 * prints every active slot as a delta since the previous report and rezeroes
 * it — call it from the component's own periodic context (~2 s works well).
 *
 * Probes compile to nothing without DEBUG.
 *
 * The framework is instance-based so ROM-able drivers can use it: embed the
 * counters and the instance in the unit/device context — no writable
 * globals. The name table and prefix are rodata.
 *
 *   struct MyUnit {
 *       ...
 *       struct perf_counter mu_PerfSlots[MYP_SLOT_COUNT]; // zeroed at init
 *       struct perf mu_Perf;                              // bound at init
 *   };
 *   unit->mu_Perf = (struct perf){ "myprof", my_slot_names,
 *                                  unit->mu_PerfSlots, MYP_SLOT_COUNT };
 *   ...
 *   PERF_T0(t0);
 *   do_stage();
 *   PERF_ADD(&unit->mu_Perf, MYP_STAGE, t0);
 *
 * Counters are plain read-modify-write: each hot slot is expected to have a
 * single writer task, on the 68k each update is one instruction, and a race
 * would only lose a sample — no locking is worth it here.
 *
 * Report line format (parsed by scripts/perf-report.py — keep it stable):
 *   [<prefix>] <name>: n=<count> sum=<us>us avg=<x>.<y>us max=<us>us
 */

#ifndef _PERF_H
#define _PERF_H

#include <types.h>

struct perf_counter {
	u32 pc_count;
	u32 pc_sum_us;
	u32 pc_max_us;
};

struct perf {
	const char *pf_prefix;         /* report tag, e.g. "nsprof" */
	const char *const *pf_names;   /* pf_nslots entries, rodata */
	struct perf_counter *pf_slots; /* caller-owned, zero-initialized */
	u32 pf_nslots;
};

#ifdef DEBUG

#include <timing.h>

static inline void perf_add(struct perf *pf, u32 slot, u32 t0)
{
	u32 dt = get_time() - t0;
	struct perf_counter *c = &pf->pf_slots[slot];
	c->pc_count++;
	c->pc_sum_us += dt;
	if (dt > c->pc_max_us)
		c->pc_max_us = dt;
}

#define PERF_T0(var)            u32 var = get_time()
#define PERF_ADD(pf, slot, var) perf_add((pf), (u32)(slot), (var))

/* Print + zero every active slot (delta reporting). Cold path (perf.c). */
void perf_report(struct perf *pf);

#else /* !DEBUG */

#define PERF_T0(var)            do {} while (0)
#define PERF_ADD(pf, slot, var) do {} while (0)
#define perf_report(pf)         do { (void)(pf); } while (0)

#endif /* DEBUG */

#endif /* _PERF_H */
