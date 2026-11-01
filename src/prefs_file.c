// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * prefs_file.c — an ENV:*.prefs file, read a line at a time; see prefs.h. Apart
 * from prefs.c as this is what needs dos.library. It opens that itself, for the
 * drivers that have no reason to hold it.
 */
#ifdef __INTELLISENSE__
#include <clib/dos_protos.h>
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#include <proto/dos.h>
#define EXEC_BASE_NAME SysBase /* a local in every function */
#include <proto/exec.h>
#endif

#include <exec/execbase.h>

#include <prefs.h>

#define DOS_VERSION 36 /* FGets() */

/**
 * prefs_open() - Open a prefs file for prefs_next().
 * @f:       the file, the caller's
 * @SysBase: exec
 * @path:    its name
 *
 * Return: TRUE, FALSE with nothing to close.
 */
BOOL prefs_open(struct prefs_file *f, struct ExecBase *SysBase, const char *path)
{
	/* dos packet I/O needs a Process: a bare Task's pr_MsgPort would be garbage beyond the Task structure */
	if (FindTask(NULL)->tc_Node.ln_Type != NT_PROCESS)
		return FALSE;

	struct DosLibrary *DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR) "dos.library", DOS_VERSION);
	if (DOSBase == NULL)
		return FALSE;

	f->fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
	if (f->fh == 0)
	{
		CloseLibrary((struct Library *)DOSBase);
		return FALSE;
	}
	f->SysBase = SysBase;
	f->DOSBase = DOSBase;
	return TRUE;
}

/**
 * prefs_next() - Take the next line that holds a setting.
 * @f:   the file
 * @key: where its key goes
 * @val: where its value goes
 *
 * Return: TRUE, FALSE at the end of the file.
 */
BOOL prefs_next(struct prefs_file *f, char **key, char **val)
{
	struct DosLibrary *DOSBase = f->DOSBase;

	while (FGets(f->fh, (STRPTR)f->line, sizeof(f->line)) != NULL)
		if (prefs_split(f->line, key, val))
			return TRUE;
	return FALSE;
}

/**
 * prefs_close() - Let go of a file prefs_open() opened.
 * @f: the file
 */
void prefs_close(struct prefs_file *f)
{
	struct ExecBase *SysBase = f->SysBase;
	struct DosLibrary *DOSBase = f->DOSBase;

	Close(f->fh);
	CloseLibrary((struct Library *)DOSBase);
}
