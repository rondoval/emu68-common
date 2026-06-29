// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _TYPES_H
#define _TYPES_H

#include <exec/types.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef uint64_t __le64;
typedef uint32_t __le32;
typedef uint16_t __le16;
typedef uint8_t __le8;

typedef ULONG dma_addr_t;

#define UINT_MAX 0xffffffffu
#define USHORT_MAX 0xffffu

#ifndef likely
#if defined(__GNUC__) || defined(__clang__)
#define likely(x) __builtin_expect(!!(x), 1)
#else
#define likely(x) (x)
#endif
#endif

#ifndef unlikely
#if defined(__GNUC__) || defined(__clang__)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define unlikely(x) (x)
#endif
#endif

#define DMA_ALIGN_MIN 64
#define DMA_ALIGN_MIN_MASK (DMA_ALIGN_MIN - 1)

#endif