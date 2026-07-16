// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * barrier.h — the Emu68 NOP-becomes-dsb-sy trick.
 *
 * Emu68 JIT-translates the m68k NOP into a bare ARM `dsb sy`.  On real
 * silicon a NOP is just a NOP — only ever rely on this on the Emu68
 * platform (everything in this stack is).
 *
 * Shared by cache_ops.h (DMA batch termination) and iomem.h (ordering
 * between adjacent MMIO accesses).
 */
#ifndef _BARRIER_H
#define _BARRIER_H

static inline void emu68_barrier(void)
{
	asm volatile("nop" ::: "memory");
}

#endif /* _BARRIER_H */
