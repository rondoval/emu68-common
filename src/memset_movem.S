/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * memset_movem.S — bulk byte-fill via movem.l (the > MEM_FILL_4_MAX path of
 * memset()).  Stores 16 longs (one 64-byte "block") per inner iteration by
 * pre-decrementing from the end of the region.  The 8 movem source registers
 * (d0-d4/a2-a4) are pre-loaded with the broadcast fill value, so value 0 costs
 * exactly what the old zero-only routine did and any other byte comes for free.
 *
 *   void mem_fill_asm_movem_impl(ULONG *end, ULONG blocks, ULONG value);
 *     end    = one-past-the-last long of the block region (fills downward)
 *     blocks = number of 64-byte (16-long) blocks
 *     value  = fill pattern, byte already broadcast to all four byte lanes
 */
	.text
	.even
	.globl _mem_fill_asm_movem_impl
	.globl mem_fill_asm_movem_impl

_mem_fill_asm_movem_impl:
mem_fill_asm_movem_impl:
	move.l 4(%sp), %a0          | end pointer
	move.l 8(%sp), %d0          | blocks
	beq.s .Ldone
	move.l 12(%sp), %d1         | value (read before pushing callee-saved regs)

	movem.l %d2-%d5/%d7/%a2-%a4, -(%sp)
	move.l %d0, %d7             | d7 = block counter (d0 reused as fill reg below)

	move.l %d1, %d0             | broadcast value into all 8 movem registers
	move.l %d1, %d2             | (d1 is itself part of the d0-d4 set)
	move.l %d1, %d3
	move.l %d1, %d4
	movea.l %d1, %a2
	movea.l %d1, %a3
	movea.l %d1, %a4

	swap %d7
	tst.w %d7
	beq.s .Lsetup_low

.Louter_loop:
	moveq #-1, %d5

.Linner_full:
	movem.l %d0-%d4/%a2-%a4, -(%a0)
	movem.l %d0-%d4/%a2-%a4, -(%a0)
	dbra %d5, .Linner_full
	subq.w #1, %d7
	bne.s .Louter_loop

.Lsetup_low:
	swap %d7
	subq.w #1, %d7
	bmi.s .Lrestore

.Lloop:
	movem.l %d0-%d4/%a2-%a4, -(%a0)
	movem.l %d0-%d4/%a2-%a4, -(%a0)
	dbra %d7, .Lloop

.Lrestore:
	movem.l (%sp)+, %d2-%d5/%d7/%a2-%a4

.Ldone:
	rts
