// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _TIMING_H
#define _TIMING_H

#include <byteorder.h>
#include <errors.h>

#define get_time() (le32(*(volatile ULONG *)0xf2003004))

inline void delay_us(ULONG us)
{
	ULONG timer = get_time();
	ULONG end = timer + us;

	if (end < timer)
	{
		while (end < get_time())
			asm volatile("nop");
	}
	while (end > get_time())
		asm volatile("nop");
}

static inline BOOL time_deadline_passed(ULONG now, ULONG deadline)
{
	return ((LONG)(deadline - now)) < 0;
}

#endif