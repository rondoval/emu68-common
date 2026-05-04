// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _FORMAT_H
#define _FORMAT_H

#include <stdarg.h>
#include <types.h>

LONG _VSNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, va_list args);
LONG _SNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, ...);

#endif