# emu68-common — Agent Notes

## Role

- `emu68-common` is the shared support library for the stack: debug output, GPIO helpers, the DMA-memory facility, a reset guard, byteorder/MMIO accessors, timing, devicetree wrappers, minlist/string/format helpers.
- Also builds the hosted `emu68check` CLI tool (`tools/`), installed to `C/` — run by the stack's Install script from the archive, not a user-facing command.
- Changes here can affect multiple downstream repos, so prefer validating through `emu68-driver-stack` if public headers or shared helper semantics change.

## Build

Build through the superbuild's container wrapper — never host `cmake` (build
trees are configured at `/work` inside the toolchain container), from the
`emu68-driver-stack` superbuild root:

```sh
./scripts/docker-build.sh --target emu68-common
```

`devicetree.resource` must be installed first (the superbuild orders this).

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
instead of hardcoding `-DDEBUG`/`emu68_rom_check`: `emu68_debug_definitions()`
(sets `DEBUG_SINK`/`DEBUG_SERIAL` + tier macros), `emu68_tier_at_least(<out>
<rung>)` (wires third-party log switches, e.g. nvme.device's mounter
submodule), and `emu68_debug_backend_finalize(<tgt> [ROMABLE])` (serial: link
`-ldebug` + the weak `__divsi3` glue; else: run the ROM check for `ROMABLE`
targets).

Note: `debug.h` includes `<proto/exec.h>` only under `DEBUG_SINK`; sources
that call exec functions must include it themselves (the `#define
__NOLIBBASE__` / `EXEC_BASE_NAME (*(struct ExecBase**)4UL)` idiom).

## Key Headers

| Header | Purpose |
|---|---|
| `debug.h` | `KprintfP`/`Kprintf`/`KprintfT`/`KASSERT`/`PrintPistorm` — one printer per tier over a `RawDoFmt` whose per-byte `putch` sink is set by the backend (`pistorm`: magic `0xdeadbeef` PiStorm hook; `serial`: `debug.lib` `KPutChar`); all compiled out in the `off` backend. Sink from `EMU68_DEBUG_BACKEND`, tier from `EMU68_TIER` (see Build) |
| `emu68_features.h` | Runtime firmware-capability detection: `emu68_probe_dcache_range_ops()` (raw three-state probe of the `/emu68` `dcache-range-ops` device-tree property) and `emu68_has_dcache_range_ops()` (the driver init gate; folds to `TRUE` under `EMU68_FORCE_LVO_CACHE_OPS`) |
| `dma_mem.h` | DMA-memory facility: `dma_mem_init` / `dma_addr_reachable` (bounce-buffer predicate) and a region-restricted `dma_pool` (`dma_pool_create`/`dma_alloc`/`dma_zalloc`/`dma_free`) that always allocates from DMA-reachable Emu68 RAM |
| `slab.h` | Fixed-size object slab allocator (`slab_cache_init`/alloc/free), optionally backed by a `dma_mem` pool for DMA-reachable objects |
| `memory.h` | Exec pool helpers (`pool_alloc`/`pool_zalloc`/`pool_free`) and the freestanding `memset`/`memcpy`/`memmove`/`memcmp` the compiler may synthesise at `-O3` in this `-nostdlib` tree (implemented in `memory.c`; `memset` is asm-optimised, `memcpy`/`memmove` route through Exec `CopyMem`) |
| `perf.h` | Instance-based per-stage timing (`PERF_T0`/`PERF_ADD` probes, `perf_report()` delta lines); embed the counters in the caller's context (ROM-able, no globals); compiles out below the `PROFILE` tier. Reduce captures with `scripts/perf-report.py` |
| `reset_guard.h` | `reset_guard_install`/`reset_guard_remove` — run a driver "prepare for reset" callback before the Amiga resets (Ctrl-A-A warning and `ColdReboot()`) |
| `iomem.h` | Volatile little-endian MMIO accessors (`mmio_read/write{8,16,32}`, `mmio_update/clear/set{16,32}`) |
| `devtree.h` | Thin wrappers around `devicetree.resource` API |
| `bcm_gpio.h` | BCM2711 GPIO helpers (set pull, alternate function, output level) |
| `types.h`, `bits.h`, `byteorder.h`, `timing.h` | Fixed-width/`__le` types + branch hints, bit/alignment ops, endian conversion, busy-wait delay primitives |
| `errors.h`, `minlist.h`, `strutil.h`, `format.h` | `errno`-style codes, `_NewMinList`, bounded string compares plus standard `strncmp`/`strlen`/`strlcpy`, `_SNPrintf`/`_VSNPrintf` |

## Code Handling

- Prefer adding generic helpers here instead of duplicating utility code in drivers.
- When a shared helper can replace an AmigaOS library dependency, prefer the local helper if it is genuinely reused by more than one stack component.
- Preserve public header stability where possible; downstream repos include these headers directly.
- `emu68-common` presents as `MPL-2.0 OR GPL-2.0+` at the repository level; preserve SPDX headers on source files when editing.

## Validation

- After editing public headers, rebuild at least one downstream consumer, preferably through `emu68-driver-stack`.
- After editing source-only helpers, checking Problems plus a local build is usually enough unless signatures changed.
