// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _TIMING_H
#define _TIMING_H

#include <types.h>
#include <byteorder.h>
#include <errors.h>

static inline u32 get_time(void)
{
	return le32(*(volatile __le32 *)0xf2003004);
}

static inline void delay_us(u32 us)
{
	u32 timer = get_time();
	u32 end = timer + us;

	if (end < timer)
	{
		while (end < get_time())
			asm volatile("nop");
	}
	while (end > get_time())
		asm volatile("nop");
}

static inline void delay_ms(u32 ms)
{
	delay_us(ms * 1000U);
}

static inline BOOL time_deadline_passed(u32 now, u32 deadline)
{
	return ((s32)(deadline - now)) < 0;
}

#endif