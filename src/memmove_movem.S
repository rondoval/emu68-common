/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * memmove_movem.S — the stack memmove(): a longword loop for short moves,
 * movem.l blocks for long ones, upwards as memcpy goes unless dst lies inside
 * the source, then from the end, so that every source byte is read before it
 * is overwritten; any alignment.
 *
 *   void *memmove(void *dst, const void *src, __SIZE_TYPE__ n);
 *
 * Which way round. Moving data up by copying from its start destroys what the
 * copy still needs; starting from the end does not:
 *
 *   before           A B C D E F . .     src = 0, dst = 2, n = 6
 *   from the start   A B A B A B A B     C was overwritten before it was read
 *   from the end     A B A B C D E F     each store lands on a byte already read
 *
 * Moving down is the mirror image, so it has to go from the start, and regions
 * apart work either way. Only dst inside the source needs the end first. The
 * forward path takes everything else and comes first, because the JIT assumes
 * a forward branch is not taken.
 *
 * Same shape and constraints as memcpy_movem.S (see there for why), but for
 * the end. memcpy finishes by copying its last bytes again from the source,
 * which a move has overwritten by then, so here the long that a move ends on
 * is read before anything moves and stored last:
 *   n < 4              bytes, one at a time;
 *   4 <= n < 64        longwords up to that long;
 *   n >= 64            64-byte blocks, then longwords up to that long.
 * The saved long also saves a byte tail: the loops stop up to 4 bytes short of
 * the end, and that store covers whatever is left in one go, rewriting any
 * bytes already copied with the values they already have.
 *
 * A long or a block may overlap its own destination: all of it is read before
 * any of it is stored. Neither path aligns its pointers, since longword and
 * movem.l accesses may be misaligned from the 020 up and cost nothing extra
 * on Emu68. The result is returned in d0 and a0.
 *
 * Registers: the caller gives up d0, d1, a0 and a1. The block paths need more
 * and save them on the stack first, which moves the arguments, so they reload
 * n and src from the stack only once those are restored.
 */
/* below this, the long loop; from here on, blocks (measured: no higher value
 * wins, despite the second loop a block path has here) */
#define MEMMOVE_SMALL 64

	.text
	.even
	.globl _memmove
	.globl memmove

_memmove:
memmove:
	move.l 4(%sp), %a0          | dst
	move.l 8(%sp), %a1          | src
	move.l 12(%sp), %d0         | n
	move.l %a0, %d1
	sub.l %a1, %d1              | d1 = dst - src
	beq.s .Ldone                | the same place: nothing to do

	/* Is dst inside the source? d1 counts as unsigned here: with dst below src
	 * it has wrapped to a huge number, so d1 < n only when src < dst < src + n. */
	cmp.l %d0, %d1
	bcs.s .Ldown                | dst lies inside the source

	cmp.l #MEMMOVE_SMALL, %d0
	bcc.s .Lbig

	/* Forward, 4 to 63 bytes, using only the scratch registers: a0 and a1 walk
	 * up, d1 is where a1 stops, d0 holds the last long. */
	move.l %d0, %d1
	subq.l #4, %d1              | d1 = n - 4: offset of the last long
	bcs.s .Ltiny                | n < 4: there is no last long
	move.l (%a1,%d1.l), %d0     | before anything moves
	add.l %a1, %d1              | d1 = src + n - 4: address of the last long
.Llong:
	move.l (%a1)+, (%a0)+
	cmp.l %a1, %d1              | cmp, not subq: it leaves the X flag alone
	bhi.s .Llong                | while below the last long

	/* a1 has stopped on the last long or a few bytes past it, and a0 is the same
	 * distance past dst's last long, so d1 - a1 steps a0 back onto it. */
.Llast:
	sub.l %a1, %d1              | how far a0 is past the last long
	move.l %d0, (%a0,%d1.l)     | the end of the move, as it was at the start
.Ldone:
	move.l 4(%sp), %d0          | return dst
	movea.l %d0, %a0
	rts

	/* Too short for a last long, and the only path n = 0 can reach. */
.Ltiny:                             | n = 0..3
	tst.l %d0
	beq.s .Ldone
.Lbyte:
	move.b (%a1)+, (%a0)+
	subq.l #1, %d0
	bne.s .Lbyte
	bra.s .Ldone

	/* Forward, 64 bytes or more: whole 64-byte blocks, each two movem.l of 8
	 * registers = 32 bytes, then .Llong for the up to 63 bytes left. d0 is not
	 * in the movem.l list, so the last long stays in it all through the loop. */
.Lbig:                              | n >= MEMMOVE_SMALL
	movem.l %d2-%d7/%a2-%a3, -(%sp)
	moveq #-64, %d1
	and.l %d0, %d1              | d1 = n rounded down to whole blocks
	lea (%a1,%d1.l), %a3        | a3 = src end of the whole blocks
	move.l -4(%a1,%d0.l), %d0   | the last long, before anything moves

	/* Safe at any gap, even 1: the 32 bytes are all in registers before any is
	 * stored, and with dst below src the stores end below the next read. */
.Lblock:
	movem.l (%a1)+, %d1-%d7/%a2 | 32 bytes in
	movem.l %d1-%d7/%a2, (%a0)  | movem-to-memory has no postincrement:
	movem.l (%a1)+, %d1-%d7/%a2 | fixed offsets, then one lea
	movem.l %d1-%d7/%a2, 32(%a0)
	lea 64(%a0), %a0
	cmpa.l %a3, %a1
	bne.s .Lblock

	/* d1 was lost to the movem.l. With the registers back, sp is where it was
	 * and the arguments are too, so work out where the last long is again. */
	movem.l (%sp)+, %d2-%d7/%a2-%a3
	move.l 8(%sp), %d1
	add.l 12(%sp), %d1
	subq.l #4, %d1              | d1 = src + n - 4
	cmp.l %a1, %d1
	bhi.s .Llong                | more than the last long is left
	bra.s .Llast

	/* From the end: dst lies inside the source. This mirrors the forward path.
	 * a0 and a1 start at the ends and walk down, and the long saved is the first,
	 * because that is the one this direction ends on. n is at least 2 here: dst
	 * is at least 1 above src, and less than n above it. */
.Ldown:
	cmp.l #MEMMOVE_SMALL, %d0
	bcc.s .Ldown_big
	moveq #4, %d1
	cmp.l %d1, %d0
	bcs.s .Ldown_tiny           | n < 4: there is no first long
	move.l (%a1), %d1           | the first long, before anything moves
	adda.l %d0, %a0             | a0, a1 = their ends
	adda.l %d0, %a1
	move.l %d1, %d0             | d0 holds that long from here on
	move.l 8(%sp), %d1
	addq.l #4, %d1              | d1 = src + 4: the end of the first long
.Ldown_long:
	move.l -(%a1), -(%a0)
	cmp.l %a1, %d1
	bcs.s .Ldown_long           | while above the first long
.Ldown_last:
	movea.l 4(%sp), %a0         | return dst
	move.l %d0, (%a0)           | the start of the move, as it was at the start
	move.l %a0, %d0
	rts

	/* Indexed from n down to 1: a0 and a1 never move, so a0 is still dst. */
.Ldown_tiny:                        | n = 2..3
	move.b -1(%a1,%d0.l), -1(%a0,%d0.l)
	subq.l #1, %d0
	bne.s .Ldown_tiny
	move.l %a0, %d0             | return dst
	rts

	/* From the end, 64 bytes or more: whole blocks down from the end, then
	 * .Ldown_long for the up to 63 bytes left at the start. d0 holds the first
	 * long through the loop, as it holds the last one in .Lblock. */
.Ldown_big:                         | n >= MEMMOVE_SMALL
	movem.l %d2-%d7/%a2-%a3, -(%sp)
	moveq #63, %d1
	and.l %d0, %d1              | d1 = what the whole blocks leave over
	lea (%a1,%d1.l), %a3        | a3 = src start of the whole blocks
	lea (%a1,%d0.l), %a2        | a2 = src + n: a1 must stay for one more read
	adda.l %d0, %a0             | a0 = dst + n
	move.l (%a1), %d0           | the first long, before anything moves
	movea.l %a2, %a1

	/* The mirror of .Lblock, with the opposite addressing, as movem.l has
	 * predecrement only for stores: fixed offsets on the reads, then one lea.
	 * movem.l stores to -(%a0) from a2 down to d1, so d1 still ends up lowest
	 * and the bytes keep their order. Safe at any gap, as in .Lblock. */
.Ldown_block:
	movem.l -32(%a1), %d1-%d7/%a2
	movem.l %d1-%d7/%a2, -(%a0)
	movem.l -64(%a1), %d1-%d7/%a2
	movem.l %d1-%d7/%a2, -(%a0)
	lea -64(%a1), %a1
	cmpa.l %a3, %a1
	bne.s .Ldown_block

	/* As after .Lblock: registers back, then where this direction stops. */
	movem.l (%sp)+, %d2-%d7/%a2-%a3
	move.l 8(%sp), %d1
	addq.l #4, %d1              | d1 = src + 4
	cmp.l %a1, %d1
	bcs.s .Ldown_long           | more than the first long is left
	bra.s .Ldown_last
