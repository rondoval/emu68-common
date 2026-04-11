/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
	.text
	.even
	.globl _mem_zero_asm_movem_impl
	.globl mem_zero_asm_movem_impl

_mem_zero_asm_movem_impl:
mem_zero_asm_movem_impl:
	move.l 4(%sp), %a0
	move.l 8(%sp), %d0
	beq.s .Ldone

	movem.l %d2-%d5/%d7/%a2-%a4, -(%sp)
	move.l %d0, %d7
	moveq #0, %d0
	moveq #0, %d1
	moveq #0, %d2
	moveq #0, %d3
	moveq #0, %d4
	movea.l %d0, %a2
	movea.l %d0, %a3
	movea.l %d0, %a4
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