// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _BITS_H
#define _BITS_H

#include <types.h>

#define ALIGN_MASK(value, mask) (((value) + (mask)) & ~(mask))
#define ALIGN_UP(value, alignment) ALIGN_MASK((value), (typeof(value))(alignment) - 1)

#define DIV_CEIL(numerator, denominator) (((numerator) + (denominator) - 1) / (denominator))

#define BIT(bit_index) (1UL << (bit_index))

#define SZ_1M 0x00100000
#define SZ_64M 0x04000000
#define SZ_4G 0x100000000ULL

static inline u32 mask_shift(u32 mask)
{
	u32 shift = 0;

	while (mask != 0 && (mask & 1UL) == 0)
	{
		mask >>= 1;
		shift++;
	}

	return shift;
}

static inline u32 mask_extract(u32 value, u32 mask)
{
	if (mask == 0)
		return 0;

	return (value & mask) >> mask_shift(mask);
}

static inline u32 mask_insert(u32 value, u32 mask)
{
	if (mask == 0)
		return 0;

	return (value << mask_shift(mask)) & mask;
}

static inline void u32_update_mask(u32 *word, u32 value, u32 mask)
{
	*word = (*word & ~mask) | mask_insert(value, mask);
}

static inline u32 log2_floor_u64(u64 value)
{
	u32 log2 = 0;

	while (value > 1)
	{
		value >>= 1;
		log2++;
	}

	return log2;
}

static inline u64 round_up_pow2_u64(u64 value)
{
	if (value <= 1)
		return 1;

	return 1ULL << (log2_floor_u64(value - 1) + 1);
}

static inline u32 u64_lo32(u64 value)
{
	return (u32)value;
}

static inline u32 u64_hi32(u64 value)
{
	return (u32)(value >> 32);
}

static inline s32 min(s32 a, s32 b)
{
	return (a < b) ? a : b;
}

static inline s32 max(s32 a, s32 b)
{
	return (a > b) ? a : b;
}

static inline u32 clamp_val(u32 val, u32 min, u32 max)
{
	if (val < min)
		return min;
	if (val > max)
		return max;
	return val;
}

inline static u32 roundup(u32 x, u32 y)
{
	return ((x + y - 1) / y) * y;
}

inline static u32 rounddown(u32 x, u32 y)
{
	return x - (x % y);
}

#endif