# emu68-common — Agent Notes

## Role

- `emu68-common` is the shared support library for the stack: debug output, GPIO helpers, the DMA-memory facility, a reset guard, byteorder/MMIO accessors, timing, devicetree wrappers, minlist/string/format helpers.
- Also builds the hosted `emu68check` CLI tool (`tools/`), installed to `C/` — run by the stack's Install script from the archive, not a user-facing command.
- `tools/memcheck.c` proves `memmove` on m68k, byte for byte, and is not installed. Run it after any change to the memory primitives, on an Amiga to include the JIT: a host test only exercises the host's own `memmove`.
- `tools/prefscheck.c` does the same for `prefs.h` and `strfit()`, also not installed. Run it after changing them; vamos is enough, as nothing there depends on the JIT.
- `testing/` is the stack's common test harness (`testing/README.md`): the `emu68test` Python package, the `Emu68Common::kv` and `Emu68Common::logic` libraries and `emu68_logic_program()`. The components' suites build on it through the install tree. Its own tests: `tests/run.sh host` (no Amiga; needs the stack built). Run them after any change under `testing/`.
- Changes here can affect multiple downstream repos, so prefer validating through `emu68-driver-stack` if public headers or shared helper semantics change.

## Build

Build through the superbuild's container wrapper — never host `cmake` (build
trees are configured at `/work` inside the toolchain container), from the
`emu68-driver-stack` superbuild root:

```sh
./scripts/docker-build.sh --target emu68-common
```

The `devicetree.resource` client headers (`<proto/devicetree.h>`) come with the toolchain
image (amiga-build-container); there is no package to install first.

### Debug output backend (owned by this component)

`EMU68_DEBUG_BACKEND` (default `pistorm` | `serial` | `off`) selects the debug
sink and is exported to every consumer via the installed
`cmake/Emu68CommonDebug.cmake` module:

```sh
EMU68_CONFIGURE_ARGS="-DEMU68_DEBUG_BACKEND=serial" ./scripts/docker-build.sh
```

- `pistorm` — `RawDoFmt` + `putch` → magic `0xdeadbeef` (Emu68 trap). ROM-able.
- `serial`  — `RawDoFmt` + `putch` → `debug.lib` `KPutChar` (AmigaOS serial @ 9600).
  Links `debug.lib` (a 4-byte `_SysBase` `.bss`) → **not** ROM-able.
- `off`     — logging compiled out.

On top of the sink, `EMU68_TIER` (`off`|`profile`|`debug`|`trace`, default
`debug`) selects what is emitted — a cumulative ladder (`PROFILE` ⊂ `DEBUG` ⊂
`TRACE`), one printer each (`KprintfP`/`Kprintf`/`KprintfT`). `DEBUG_SINK`
marks "a sink exists" and is NOT a tier: it gates the formatter, the tier
macros gate the printers — which is what keeps `perf.c`'s reporter linkable
from a higher-tier consumer.

The module exports the functions every debug-emitting component calls
instead of hardcoding `-DDEBUG`: `emu68_debug_definitions()` (sets
`DEBUG_SINK`/`DEBUG_SERIAL` + tier macros) and `emu68_tier_at_least(<out>
<rung>)` (wires third-party log switches, e.g. nvme.device's mounter
submodule).  ROM-ability is not part of this module: it is an `ASSERT` in
`ldscripts/module.lds`, applied by `emu68_module_layout()`.

SFD header generation is `emu68_sfd_headers()` (`cmake/Emu68CommonSfd.cmake`,
with the sfdc runner `cmake/Emu68CommonSfdScript.cmake`); components call it
instead of carrying their own copy of the script.  `FD_DIR` adds the FD file.

Note: `debug.h` includes `<proto/exec.h>` only under `DEBUG_SINK`; sources
that call exec functions must include it themselves (the `#define
__NOLIBBASE__` / `EXEC_BASE_NAME (*(struct ExecBase**)4UL)` idiom).

## Key Headers

The headers are in `include/`; each says in its comments what it holds. What they leave out:

- `memcpy` (`memory.h`) is not overlap-safe in either direction.
- New settings readers use `prefs.h`, not their own loop.
- `scripts/perf-report.py` parses `perf.h`'s timing lines only and ignores the `hist` lines.

## Code Handling

- Prefer adding generic helpers here instead of duplicating utility code in drivers.
- When a shared helper can replace an AmigaOS library dependency, prefer the local helper if it is genuinely reused by more than one stack component.
- Preserve public header stability where possible; downstream repos include these headers directly.
- `emu68-common` presents as `MPL-2.0 OR GPL-2.0+` at the repository level; preserve SPDX headers on source files when editing.

## Validation

- After editing public headers, rebuild at least one downstream consumer, preferably through `emu68-driver-stack`.
- After editing source-only helpers, checking Problems plus a local build is usually enough unless signatures changed.
