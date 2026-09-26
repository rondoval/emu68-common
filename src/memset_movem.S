/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * memset_movem.S — the stack memset(): a longword loop for short fills,
 * movem.l blocks for long ones; any alignment.
 *
 *   void *memset(void *dst, int c, __SIZE_TYPE__ n);
 *
 * Same shape and constraints as memcpy_movem.S (see there for why): no Exec
 * call, no writable data (interrupt-callable, ROM-safe), 68020 or later
 * (misaligned longword stores; mulu.l), no forward branch taken on the common
 * paths, one loop, an overlapping final store instead of a byte tail:
 *   n < 4              three byte stores at 0, n-1 and n/2 (n == 0 returns);
 *   4 <= n < 64        longwords while below the last long, then the long
 *                      ending at n;
 *   n >= 64            64-byte blocks as two movem.l stores of 8 registers,
 *                      then the 64-byte block ending at n.
 * The fill byte is broadcast to a longword once. The result is returned in d0
 * and a0.
 */
/* below this, the long loop; from here on, blocks (measured: the block path
 * wins from its minimum, one whole block) */
#define MEMSET_SMALL 64

	.text
	.even
	.globl _memset
	.globl memset

_memset:
memset:
	move.l 4(%sp), %a0          | dst
	moveq #0, %d1
	move.b 11(%sp), %d1         | c: the low byte of the int argument
	mulu.l #0x01010101, %d1     | broadcast to all four byte lanes
	move.l 12(%sp), %d0         | n
	cmp.l #MEMSET_SMALL, %d0
	bcc.s .Lbig
	subq.l #4, %d0              | d0 = n - 4: offset of the last long
	bcs.s .Ltiny
	lea (%a0,%d0.l), %a1        | a1 = dst + n - 4
.Llong:
	move.l %d1, (%a0)+
	cmpa.l %a0, %a1
	bhi.s .Llong                | while below the last long
	move.l %d1, (%a1)           | the long ending at n
.Ldone:
	move.l 4(%sp), %d0          | return dst
	movea.l %d0, %a0
	rts

.Ltiny:                             | n = 0..3
	addq.l #4, %d0
	beq.s .Ldone
	move.b %d1, (%a0)
	move.b %d1, -1(%a0,%d0.l)
	lsr.l #1, %d0
	move.b %d1, (%a0,%d0.l)
	bra.s .Ldone

.Lbig:                              | n >= MEMSET_SMALL
	movem.l %d2-%d7/%a2-%a3, -(%sp)
	move.l %d1, %d2             | the pattern in all 8 store registers
	move.l %d1, %d3
	move.l %d1, %d4
	move.l %d1, %d5
	move.l %d1, %d6
	move.l %d1, %d7
	movea.l %d1, %a2
	moveq #-64, %d2
	and.l %d0, %d2
	lea (%a0,%d2.l), %a3        | a3 = end of the whole blocks
	move.l %d1, %d2
.Lblock:
	movem.l %d1-%d7/%a2, (%a0)
	movem.l %d1-%d7/%a2, 32(%a0)
	lea 64(%a0), %a0
	cmpa.l %a3, %a0
	bne.s .Lblock
	and.l #63, %d0
	sub.l #64, %d0              | back to the start of the block ending at n
	adda.l %d0, %a0
	movem.l %d1-%d7/%a2, (%a0)
	movem.l %d1-%d7/%a2, 32(%a0)
	movem.l (%sp)+, %d2-%d7/%a2-%a3
	bra.s .Ldone
