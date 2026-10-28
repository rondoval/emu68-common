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
 *   emu68check BSDSOCKET
 *     0   LIBS:bsdsocket.library is lwip-amiga's (the bundled stack)
 *     5   it belongs to another stack (Roadshow, AmiTCP, Miami ...)
 *     10  there is no LIBS:bsdsocket.library
 *
 *   emu68check OPENPCI
 *     0   LIBS:openpci.library is ours (the shim over bcmpcie.library)
 *     5   it is another one, most likely a real PCI board's
 *     10  there is no LIBS:openpci.library
 *
 *   emu68check LSPCI
 *     0   C:lspci is ours (it lists the bus through bcmpcie.library)
 *     5   it is another one, most likely a real PCI board's
 *     10  there is no C:lspci
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

static const char verstag[] __attribute__((used)) = VERSTAG;

static int check_rangeops(void)
{
    switch (emu68_probe_dcache_range_ops(SysBase))
    {
    case EMU68_PROBE_PRESENT:
        return RETURN_OK;
    case EMU68_PROBE_ABSENT:
        return RETURN_WARN;
    default: /* EMU68_PROBE_NO_DEVICETREE */
        return RETURN_ERROR;
    }
}

/**
 * check_ours() - Tell whether an installed file is the stack's own build.
 * @path: file to scan
 * @tag:  string only our build of that file carries; its first character must
 *        not recur in it
 *
 * The file is read with Read(), not loaded, so a library's init never runs.
 *
 * Return: RETURN_OK when @tag is in the file, RETURN_WARN when it is not (or on
 * a read error), RETURN_ERROR when the file cannot be opened.
 */
static int check_ours(CONST_STRPTR path, const char *tag)
{
    static char buf[4096];

    BPTR fh = Open(path, MODE_OLDFILE);
    if (!fh)
        return RETURN_ERROR;

    BOOL found = FALSE;
    ULONG matched = 0; /* leading bytes of tag seen so far */
    LONG got;
    while (!found && (got = Read(fh, buf, sizeof(buf))) > 0)
    {
        for (LONG i = 0; i < got && !found; i++)
        {
            if (buf[i] == tag[matched])
                matched++;
            else /* tag[0] recurs nowhere in tag, so no shorter partial match survives */
                matched = buf[i] == tag[0] ? 1 : 0;
            found = tag[matched] == '\0';
        }
    }
    Close(fh);
    return found ? RETURN_OK : RETURN_WARN;
}

int main(int argc, char **argv)
{
    if (argc == 2)
    {
        if (_Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR) "RANGEOPS") == 0)
            return check_rangeops();
        /* The release string every lwip-amiga build carries (SBTC_RELEASESTRPTR). */
        if (_Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR) "BSDSOCKET") == 0)
            return check_ours((CONST_STRPTR) "LIBS:bsdsocket.library", "lwip-amiga");
        /* Ours open bcmpcie.library; a PCI board's openpci.library and lspci do not. */
        if (_Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR) "OPENPCI") == 0)
            return check_ours((CONST_STRPTR) "LIBS:openpci.library", "bcmpcie");
        if (_Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR) "LSPCI") == 0)
            return check_ours((CONST_STRPTR) "C:lspci", "bcmpcie");
    }

    PutStr((CONST_STRPTR)
               "Usage: emu68check RANGEOPS | BSDSOCKET | OPENPCI | LSPCI\n"
               "  RANGEOPS:  RC 0 firmware has the dcache-range-ops capability,\n"
               "             5 absent, 10 not running under Emu68\n"
               "  BSDSOCKET: RC 0 LIBS:bsdsocket.library is lwip-amiga's,\n"
               "             5 another stack's, 10 none\n"
               "  OPENPCI:   RC 0 LIBS:openpci.library is the bcmpcie shim,\n"
               "             5 another one, 10 none\n"
               "  LSPCI:     RC 0 C:lspci is the bcmpcie one,\n"
               "             5 another one, 10 none\n");
    return RETURN_FAIL;
}
