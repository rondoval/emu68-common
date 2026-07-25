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
 * Probes compile to nothing below the PROFILE tier. The reporter itself lives
 * in libcommon.a gated on DEBUG_SINK, not on PROFILE, so emu68-common may sit at
 * a lower tier than the component calling perf_report() (e.g. TIER=off
 * PROFILE=lwip-amiga) without leaving an undefined symbol behind.
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

#ifdef PROFILE

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

#else /* !PROFILE */

#define PERF_T0(var)            do {} while (0)
#define PERF_ADD(pf, slot, var) do {} while (0)

#endif /* PROFILE */

/*
 * Print + zero every active slot (delta reporting). Cold path (perf.c).
 * Declared for PROFILE-tier callers and for perf.c itself (PERF_IMPL), which is
 * built whenever a sink exists so the symbol is there for any caller — see the
 * tier note at the top. Below the PROFILE tier the call vanishes at the caller.
 */
#if defined(PROFILE) || defined(PERF_IMPL)
void perf_report(struct perf *pf);
#else
#define perf_report(pf)         do { (void)(pf); } while (0)
#endif

/*
 * lock_prof — a SignalSemaphore wait/hold profiler, built on the perf slots
 * above. A self-contained instance: it owns a two-slot perf report ("lockwait",
 * "lockhold") so a component reports its core lock through the same
 * perf_report()/scripts/perf-report.py path as its stage timings. Only the
 * outermost hold is timed (ss_NestCount), so recursive re-entry does not
 * double-count; the wait/hold counter updates run while the lock is held, so
 * they need no atomicity of their own.
 *
 * ROM-able: embed the struct in the unit/device context (all mutable state is
 * caller-owned); the name table and prefix are rodata.
 *
 *   struct MyUnit { ...; struct lock_prof mu_LockProf; };
 *   lock_prof_init(&unit->mu_LockProf, "mydev");   // once, after InitSemaphore
 *   lock_prof_obtain(&unit->mu_LockProf, &sem);     // instead of ObtainSemaphore
 *   ... work ...
 *   lock_prof_release(&unit->mu_LockProf, &sem);    // instead of ReleaseSemaphore
 *   lock_prof_report(&unit->mu_LockProf);           // from the periodic report
 */
enum { LOCKPROF_WAIT, LOCKPROF_HOLD, LOCKPROF_NSLOTS };
extern const char *const lock_prof_names[LOCKPROF_NSLOTS]; /* rodata, perf.c */

struct lock_prof {
	struct perf         lp_perf;                   /* self-contained instance */
	struct perf_counter lp_slots[LOCKPROF_NSLOTS]; /* caller-owned, zeroed */
	u32                 lp_hold_t0;                /* outermost hold start */
};

#ifdef PROFILE

static inline void lock_prof_init(struct lock_prof *lp, const char *prefix)
{
	lp->lp_perf.pf_prefix = prefix;
	lp->lp_perf.pf_names  = lock_prof_names;
	lp->lp_perf.pf_slots  = lp->lp_slots;
	lp->lp_perf.pf_nslots = LOCKPROF_NSLOTS;
}

/* Obtain @s and time the outermost acquire-wait; on the outermost hold start
 * the hold clock. Macro so the exec call expands at the caller (see above). */
#define lock_prof_obtain(lp, s)                                             \
	do {                                                                    \
		u32 _lp_wait_t0 = get_time();                                       \
		ObtainSemaphore(s);                                                 \
		if ((s)->ss_NestCount == 1) {                                       \
			perf_add(&(lp)->lp_perf, LOCKPROF_WAIT, _lp_wait_t0);           \
			(lp)->lp_hold_t0 = get_time();                                  \
		}                                                                   \
	} while (0)

/* Record the outermost hold, then release @s. */
#define lock_prof_release(lp, s)                                            \
	do {                                                                    \
		if ((s)->ss_NestCount == 1)                                         \
			perf_add(&(lp)->lp_perf, LOCKPROF_HOLD, (lp)->lp_hold_t0);      \
		ReleaseSemaphore(s);                                                \
	} while (0)

static inline void lock_prof_report(struct lock_prof *lp)
{
	perf_report(&lp->lp_perf);
}

#else /* !PROFILE — the macros still perform the real lock */

static inline void lock_prof_init(struct lock_prof *lp, const char *prefix)
{
	(void)lp;
	(void)prefix;
}
#define lock_prof_obtain(lp, s)  do { (void)(lp); ObtainSemaphore(s); } while (0)
#define lock_prof_release(lp, s) do { (void)(lp); ReleaseSemaphore(s); } while (0)
static inline void lock_prof_report(struct lock_prof *lp)
{
	(void)lp;
}

#endif /* PROFILE */

#endif /* _PERF_H */
