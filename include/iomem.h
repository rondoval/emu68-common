// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _IOMEM_H
#define _IOMEM_H

#include <byteorder.h>

static inline ULONG mmio_read32_impl(const volatile void *addr)
{
	const volatile ULONG *ptr = (const volatile ULONG *)addr;
	ULONG val = le32(*ptr);
	asm volatile("nop");
	return val;
}

static inline void mmio_write32_impl(ULONG value, volatile void *addr)
{
	volatile ULONG *ptr = (volatile ULONG *)addr;
	*ptr = le32(value);
	asm volatile("nop");
}

static inline UWORD mmio_read16_impl(const volatile void *addr)
{
	const volatile UWORD *ptr = (const volatile UWORD *)addr;
	UWORD val = le16(*ptr);
	asm volatile("nop");
	return val;
}

static inline void mmio_write16_impl(UWORD value, volatile void *addr)
{
	volatile UWORD *ptr = (volatile UWORD *)addr;
	*ptr = le16(value);
	asm volatile("nop");
}

static inline UBYTE mmio_read8_impl(const volatile void *addr)
{
	return *(const volatile UBYTE *)addr;
}

static inline void mmio_write8_impl(UBYTE value, volatile void *addr)
{
	*(volatile UBYTE *)addr = value;
	asm volatile("nop");
}

#define mmio_read32(addr) mmio_read32_impl((const volatile void *)(addr))
#define mmio_write32(value, addr) mmio_write32_impl((value), (volatile void *)(addr))
#define mmio_read16(addr) mmio_read16_impl((const volatile void *)(addr))
#define mmio_write16(value, addr) mmio_write16_impl((value), (volatile void *)(addr))
#define mmio_read8(addr) mmio_read8_impl((const volatile void *)(addr))
#define mmio_write8(value, addr) mmio_write8_impl((value), (volatile void *)(addr))

static inline void mmio_update32(volatile void *addr, ULONG clear_mask, ULONG set_mask)
{
	mmio_write32((mmio_read32(addr) & ~clear_mask) | set_mask, addr);
}

static inline void mmio_clear32(volatile void *addr, ULONG clear_mask)
{
	mmio_update32(addr, clear_mask, 0);
}

static inline void mmio_set32(volatile void *addr, ULONG set_mask)
{
	mmio_update32(addr, 0, set_mask);
}

static inline void mmio_update16(volatile void *addr, UWORD clear_mask, UWORD set_mask)
{
	mmio_write16((mmio_read16(addr) & ~clear_mask) | set_mask, addr);
}

static inline void mmio_clear16(volatile void *addr, UWORD clear_mask)
{
	mmio_update16(addr, clear_mask, 0);
}

static inline void mmio_set16(volatile void *addr, UWORD set_mask)
{
	mmio_update16(addr, 0, set_mask);
}

#endif