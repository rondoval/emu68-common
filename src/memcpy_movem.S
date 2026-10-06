/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * memcpy_movem.S — the stack memcpy(): a longword loop for short copies,
 * movem.l blocks for long ones; any alignment.
 *
 *   void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
 *
 * Non-overlapping regions only (memmove guards the overlapping case). Makes
 * no Exec call and owns no writable data, so it is callable from interrupts
 * and from SANA-II copy hooks, and ROM-safe. 68020 or later: longword and
 * movem.l accesses may be misaligned from the 020 up.
 *
 * Shaped for the Emu68 JIT: a translated block
 * assumes forward branches not taken and short backward branches taken, and
 * every branch that goes the other way, loop exits included, leaves the block
 * for a dispatcher lookup that costs more than the bytes moved. So the common
 * paths take no forward branch, run exactly one loop, and end with an
 * overlapping store instead of a byte tail (rewriting bytes already copied is
 * harmless when the regions do not overlap):
 *   n < 4              three byte moves at 0, n-1 and n/2 (n == 0 returns);
 *   4 <= n < 64        longwords while below the last long, then the long
 *                      ending at n;
 *   n >= 64            64-byte blocks as two movem.l pairs (8 registers = 32
 *                      bytes each), then the 64-byte block ending at n.
 * Loop tests compare pointers (cmp leaves the X flag alone). The result is
 * returned in d0 and a0 (both pointer-return conventions).
 */
/* below this, the long loop; from here on, blocks (measured: the block path
 * wins from its minimum, one whole block) */
#define MEMCPY_SMALL 64

	.text
	.even
	.globl _memcpy
	.globl memcpy

_memcpy:
memcpy:
	move.l 4(%sp), %a0          | dst
	move.l 8(%sp), %a1          | src
	move.l 12(%sp), %d0         | n
	cmp.l #MEMCPY_SMALL, %d0
	bcc.s .Lbig
	subq.l #4, %d0              | d0 = n - 4: offset of the last long
	bcs.s .Ltiny
	move.l %a1, %d1
	add.l %d0, %d1              | d1 = src + n - 4
.Llong:
	move.l (%a1)+, (%a0)+
	cmp.l %a1, %d1
	bhi.s .Llong                | while below the last long
	movea.l 4(%sp), %a0
	movea.l 8(%sp), %a1
	move.l (%a1,%d0.l), (%a0,%d0.l) | the long ending at n
.Ldone:
	move.l 4(%sp), %d0          | return dst
	movea.l %d0, %a0
	rts

.Ltiny:                             | n = 0..3
	addq.l #4, %d0
	beq.s .Ldone
	move.b (%a1), (%a0)
	move.b -1(%a1,%d0.l), -1(%a0,%d0.l)
	lsr.l #1, %d0
	move.b (%a1,%d0.l), (%a0,%d0.l)
	bra.s .Ldone

.Lbig:                              | n >= MEMCPY_SMALL
	movem.l %d2-%d7/%a2-%a3, -(%sp)
	moveq #-64, %d1
	and.l %d0, %d1
	lea (%a1,%d1.l), %a3        | a3 = src end of the whole blocks
.Lblock:
	movem.l (%a1)+, %d1-%d7/%a2 | 32 bytes in
	movem.l %d1-%d7/%a2, (%a0)  | movem-to-memory has no postincrement:
	movem.l (%a1)+, %d1-%d7/%a2 | fixed offsets, then one lea
	movem.l %d1-%d7/%a2, 32(%a0)
	lea 64(%a0), %a0
	cmpa.l %a3, %a1
	bne.s .Lblock
	and.l #63, %d0
	sub.l #64, %d0              | back to the start of the block ending at n
	adda.l %d0, %a1
	adda.l %d0, %a0
	movem.l (%a1)+, %d1-%d7/%a2
	movem.l %d1-%d7/%a2, (%a0)
	movem.l (%a1)+, %d1-%d7/%a2
	movem.l %d1-%d7/%a2, 32(%a0)
	movem.l (%sp)+, %d2-%d7/%a2-%a3
	bra.s .Ldone
