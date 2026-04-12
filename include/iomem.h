// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _IOMEM_H
#define _IOMEM_H

#include <types.h>
#include <byteorder.h>

static inline u32 mmio_read32_impl(const volatile __le32 *addr)
{
	u32 val = le32(*addr);
	asm volatile("nop");
	return val;
}

static inline void mmio_write32_impl(u32 value, volatile __le32 *addr)
{
	*addr = le32(value);
	asm volatile("nop");
}

static inline u16 mmio_read16_impl(const volatile __le16 *addr)
{
	asm volatile("nop");
	u16 val = le16(*addr);
	return val;
}

static inline void mmio_write16_impl(u16 value, volatile __le16 *addr)
{
	*addr = le16(value);
	asm volatile("nop");
}

static inline u8 mmio_read8_impl(const volatile __le8 *addr)
{
	asm volatile("nop");
	return *addr;
}

static inline void mmio_write8_impl(u8 value, volatile __le8 *addr)
{
	*addr = value;
	asm volatile("nop");
}

#define mmio_read32(addr) mmio_read32_impl((const volatile __le32 *)(addr))
#define mmio_write32(value, addr) mmio_write32_impl((value), (volatile __le32 *)(addr))
#define mmio_read16(addr) mmio_read16_impl((const volatile __le16 *)(addr))
#define mmio_write16(value, addr) mmio_write16_impl((value), (volatile __le16 *)(addr))
#define mmio_read8(addr) mmio_read8_impl((const volatile __le8 *)(addr))
#define mmio_write8(value, addr) mmio_write8_impl((value), (volatile __le8 *)(addr))

static inline void mmio_update32(volatile __le32 *addr, u32 clear_mask, u32 set_mask)
{
	mmio_write32((mmio_read32(addr) & ~clear_mask) | set_mask, addr);
}

static inline void mmio_clear32(volatile __le32 *addr, u32 clear_mask)
{
	mmio_update32(addr, clear_mask, 0);
}

static inline void mmio_set32(volatile __le32 *addr, u32 set_mask)
{
	mmio_update32(addr, 0, set_mask);
}

static inline void mmio_update16(volatile __le16 *addr, u16 clear_mask, u16 set_mask)
{
	mmio_write16((u16)((mmio_read16(addr) & ~(u32)clear_mask) | (u32)set_mask), addr);
}

static inline void mmio_clear16(volatile __le16 *addr, u16 clear_mask)
{
	mmio_update16(addr, clear_mask, 0);
}

static inline void mmio_set16(volatile __le16 *addr, u16 set_mask)
{
	mmio_update16(addr, 0, set_mask);
}

#endif