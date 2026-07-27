// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * emu68check — query-only capability probe for the Install script.
 *
 * All answers are return codes (aligned with dos RETURN_*); output only on
 * usage error, so `(run "C/emu68check <query>" (safe))` in the Installer
 * consumes the RC directly.
 *
 *   emu68check RANGEOPS
 *     0   firmware advertises the dcache-range-ops capability (revision 1)
 *     5   running under Emu68, but the capability is absent — the rangeops
 *         drivers would refuse to load
 *     10  no devicetree.resource: not running under Emu68 at all
 *
 *   anything else: usage on stdout, RC 20
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include <exec/types.h>
#include <dos/dos.h>

#include <emu68_features.h>
#include <strutil.h>

struct ExecBase *SysBase;

static const char verstag[] __attribute__((used)) = VERSTAG;

static int check_rangeops(void)
{
    switch (emu68_probe_dcache_range_ops())
    {
    case EMU68_PROBE_PRESENT:
        return RETURN_OK;
    case EMU68_PROBE_ABSENT:
        return RETURN_WARN;
    default: /* EMU68_PROBE_NO_DEVICETREE */
        return RETURN_ERROR;
    }
}

int main(int argc, char **argv)
{
    SysBase = *(struct ExecBase **)4UL;

    if (argc == 2)
    {
        if (_Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR) "RANGEOPS") == 0)
            return check_rangeops();
    }

    PutStr((CONST_STRPTR)
               "Usage: emu68check RANGEOPS\n"
               "  RANGEOPS: RC 0 firmware has the dcache-range-ops capability,\n"
               "            5 absent, 10 not running under Emu68\n");
    return RETURN_FAIL;
}
