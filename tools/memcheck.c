// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * memcheck — proves the m68k memmove() byte for byte against a plain byte copy,
 * since a test on the host only exercises the host's own.  Every length up to
 * LENGTH_MAX with every gap up to GAP_MAX either way, at each source alignment,
 * then long moves around the block sizes.  Bytes outside the destination must
 * stay as they were, and so must the registers the caller keeps.
 *
 * Not installed.  Runs on an Amiga, to include the JIT, or on the host:
 *     vamos -C 68040 -m 4096 memcheck
 * RC 0 when everything held, else RC 10 and the first failure on stdout.
 */
#ifdef __INTELLISENSE__
#include <clib/dos_protos.h>
#else
#include <proto/dos.h>
#endif

#include <exec/types.h>
#include <dos/dos.h>

#include <memory.h>

static const char verstag[] __attribute__((used)) = VERSTAG;

#define LENGTH_MAX 300
#define GAP_MAX 140
#define LONG_MOVE_MAX 100000u
#define GUARD 16
#define ROOM (LONG_MOVE_MAX * 2u + 4096u)
#define ALIGNMENTS 4

/* src starts here, whatever the gap: both regions and the guards stay in the buffer */
#define SRC_OFFSET (ROOM / 2u & ~63u)

#define KEPT_REGISTERS 11

static UBYTE pattern[ROOM], work[ROOM];
static ULONG moves;

/* the call the checks make, through a pointer: the compiler must not fold it */
static void *(*const volatile move)(void *, const void *, __SIZE_TYPE__) = memmove;

/**
 * regs_call() - Call memmove() with every register it must keep set.
 * @in:  the values to set, d2-d7 then a2-a6
 * @out: where they go afterwards
 * @dst: its destination
 * @src: its source
 * @n:   its length
 */
void regs_call(const ULONG *in, ULONG *out, void *dst, const void *src, ULONG n);
asm(".text\n"
    "	.even\n"
    "_regs_call:\n"
    "	movem.l %d2-%d7/%a2-%a6, -(%sp)\n"
    "	movea.l 48(%sp), %a0\n"
    "	move.l 64(%sp), -(%sp)\n" /* n, src, dst: each push moves the next one to 64 */
    "	move.l 64(%sp), -(%sp)\n"
    "	move.l 64(%sp), -(%sp)\n"
    "	movem.l (%a0), %d2-%d7/%a2-%a6\n"
    "	jsr _memmove\n"
    "	lea 12(%sp), %sp\n"
    "	movea.l 52(%sp), %a0\n"
    "	movem.l %d2-%d7/%a2-%a6, (%a0)\n"
    "	movem.l (%sp)+, %d2-%d7/%a2-%a6\n"
    "	rts\n");

/**
 * pattern_fill() - Make bytes with no run or period a wrong copy could hide in.
 */
static void pattern_fill(void)
{
	ULONG x = 0x12345678u;
	for (ULONG i = 0; i < ROOM; i++)
	{
		x = x * 1664525u + 1013904223u;
		pattern[i] = (UBYTE)(x >> 24);
	}
}

/**
 * check_move() - Do one move and compare every byte around it.
 * @n:     its length
 * @gap:   how far src lies above dst, below when negative
 * @align: src's offset from a 64-byte boundary
 *
 * Return: TRUE when it held, FALSE with the failure said.
 */
static BOOL check_move(ULONG n, LONG gap, ULONG align)
{
	ULONG src = SRC_OFFSET + align;
	ULONG dst = (ULONG)((LONG)src - gap);
	ULONG low = (dst < src ? dst : src) - GUARD;
	ULONG high = (dst > src ? dst : src) + n + GUARD;

	for (ULONG i = low; i < high; i++)
		work[i] = pattern[i];

	void *result = move(work + dst, work + src, n);
	moves++;

	if (result != work + dst)
	{
		Printf((CONST_STRPTR) "%lu bytes, gap %ld, align %lu: returned the wrong address\n", n, gap, align);
		return FALSE;
	}
	for (ULONG i = low; i < high; i++)
	{
		BOOL moved = i >= dst && i < dst + n;
		UBYTE want = moved ? pattern[(ULONG)((LONG)i + gap)] : pattern[i];
		if (work[i] != want)
		{
			Printf((CONST_STRPTR) "%lu bytes, gap %ld, align %lu: byte %ld of dst is %lx, not %lx\n", n, gap, align,
			       (LONG)i - (LONG)dst, (ULONG)work[i], (ULONG)want);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * check_short() - Check every short length and gap at every alignment.
 *
 * Return: TRUE when all held.
 */
static BOOL check_short(void)
{
	for (ULONG align = 0; align < ALIGNMENTS; align++)
		for (ULONG n = 0; n <= LENGTH_MAX; n++)
			for (LONG gap = -GAP_MAX; gap <= GAP_MAX; gap++)
				if (!check_move(n, gap, align))
					return FALSE;
	return TRUE;
}

/**
 * check_long() - Check lengths around the block sizes, at gaps that matter.
 *
 * Return: TRUE when all held.
 */
static BOOL check_long(void)
{
	static const ULONG lengths[] = {1023, 1024, 1025, 4092, 4095, 4096, 4097, 65535, 65536, 65537, LONG_MOVE_MAX};
	static const LONG gaps[] = {1, 2, 3, 4, 5, 31, 32, 33, 52, 53, 54, 55, 63, 64, 65, 4096};

	for (ULONG i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
		for (ULONG align = 0; align < ALIGNMENTS; align++)
		{
			LONG n = (LONG)lengths[i];
			if (!check_move(lengths[i], n - 1, align) || !check_move(lengths[i], n, align) ||
			    !check_move(lengths[i], 1 - n, align) || !check_move(lengths[i], -n, align))
				return FALSE;
			for (ULONG g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++)
				if (!check_move(lengths[i], gaps[g], align) || !check_move(lengths[i], -gaps[g], align))
					return FALSE;
		}
	return TRUE;
}

/**
 * check_registers() - Check each way through memmove() keeps what it must.
 *
 * Return: TRUE when all held.
 */
static BOOL check_registers(void)
{
	static const struct
	{
		ULONG n;
		LONG gap;
	} ways[] = {{0, 0}, {3, 1}, {3, -1}, {40, 8}, {40, -8}, {200, 9}, {200, -9}, {200, 300}, {40, 300}};
	static const ULONG in[KEPT_REGISTERS] = {0x20202020u, 0x31313131u, 0x42424242u, 0x53535353u, 0x64646464u, 0x75757575u,
	                                         0x86868686u, 0x97979797u, 0xA8A8A8A8u, 0xB9B9B9B9u, 0xCACACACAu};

	for (ULONG w = 0; w < sizeof(ways) / sizeof(ways[0]); w++)
	{
		ULONG out[KEPT_REGISTERS];
		ULONG src = SRC_OFFSET;
		regs_call(in, out, work + (ULONG)((LONG)src - ways[w].gap), work + src, ways[w].n);
		for (ULONG r = 0; r < KEPT_REGISTERS; r++)
			if (out[r] != in[r])
			{
				Printf((CONST_STRPTR) "%lu bytes, gap %ld: register %lu of d2-d7/a2-a6 not kept\n", ways[w].n, ways[w].gap, r);
				return FALSE;
			}
	}
	return TRUE;
}

int main(void)
{
	pattern_fill();
	if (!check_registers() || !check_short() || !check_long())
		return RETURN_ERROR;
	Printf((CONST_STRPTR) "%lu moves held\n", moves);
	return RETURN_OK;
}
