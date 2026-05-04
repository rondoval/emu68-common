// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _BYTEORDER_H
#define _BYTEORDER_H

#include <types.h>

#define le64(value) ((u64)__builtin_bswap64((u64)(value)))
#define le32(value) ((u32)__builtin_bswap32((u32)(value)))
#define le16(value) ((u16)__builtin_bswap16((u16)(value)))

#endif