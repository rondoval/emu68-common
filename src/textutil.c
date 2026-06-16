/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
#include <exec/types.h>
#include <strutil.h>

LONG _Stricmp(CONST_STRPTR s1, CONST_STRPTR s2)
{
	UBYTE c1;
	UBYTE c2;

	do
	{
		c1 = (UBYTE)(*s1 ? *s1++ : 0);
		c2 = (UBYTE)(*s2 ? *s2++ : 0);
		if (c1 >= 'a' && c1 <= 'z') c1 = (UBYTE)(c1 - 32);
		if (c2 >= 'a' && c2 <= 'z') c2 = (UBYTE)(c2 - 32);
		if (c1 != c2)
			return (LONG)c1 - (LONG)c2;
	} while (c1 != 0);

	return 0;
}

LONG _Strncmp(CONST_STRPTR s1, CONST_STRPTR s2, LONG len)
{
	if (len <= 0)
		return 0;

	while (len-- > 0)
	{
		UBYTE c1 = (UBYTE)(*s1 ? *s1++ : 0);
		UBYTE c2 = (UBYTE)(*s2 ? *s2++ : 0);
		if (c1 != c2)
			return (LONG)c1 - (LONG)c2;
		if (c1 == 0)
			break;
	}
	return 0;
}

LONG _Strnicmp(CONST_STRPTR s1, CONST_STRPTR s2, LONG len)
{
	if (len <= 0)
		return 0;

	while (len-- > 0)
	{
		UBYTE c1 = (UBYTE)(*s1 ? *s1++ : 0);
		UBYTE c2 = (UBYTE)(*s2 ? *s2++ : 0);
		if (c1 >= 'a' && c1 <= 'z') c1 = (UBYTE)(c1 - 32);
		if (c2 >= 'a' && c2 <= 'z') c2 = (UBYTE)(c2 - 32);
		if (c1 != c2)
			return (LONG)c1 - (LONG)c2;
		if (c1 == 0)
			break;
	}
	return 0;
}