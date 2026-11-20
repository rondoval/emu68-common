// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _BYTEORDER_H
#define _BYTEORDER_H

#include <types.h>

/*
 * Conversion between the CPU's byte order and a fixed one, in either direction:
 * leNN() for little-endian data, beNN() for big-endian data. Whichever matches the
 * CPU costs nothing, so code written with them is right on the big-endian m68k and
 * in a build for a little-endian host.
 */
#define BYTEORDER_SWAP64(value) ((u64)__builtin_bswap64((u64)(value)))
#define BYTEORDER_SWAP32(value) ((u32)__builtin_bswap32((u32)(value)))
#define BYTEORDER_SWAP16(value) ((u16)__builtin_bswap16((u16)(value)))
#define BYTEORDER_KEEP64(value) ((u64)(value))
#define BYTEORDER_KEEP32(value) ((u32)(value))
#define BYTEORDER_KEEP16(value) ((u16)(value))

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define le64(value) BYTEORDER_SWAP64(value)
#define le32(value) BYTEORDER_SWAP32(value)
#define le16(value) BYTEORDER_SWAP16(value)
#define be64(value) BYTEORDER_KEEP64(value)
#define be32(value) BYTEORDER_KEEP32(value)
#define be16(value) BYTEORDER_KEEP16(value)
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define le64(value) BYTEORDER_KEEP64(value)
#define le32(value) BYTEORDER_KEEP32(value)
#define le16(value) BYTEORDER_KEEP16(value)
#define be64(value) BYTEORDER_SWAP64(value)
#define be32(value) BYTEORDER_SWAP32(value)
#define be16(value) BYTEORDER_SWAP16(value)
#else
#error "byteorder.h: unsupported byte order"
#endif

#endif
