# emu68-common

> **Releases:** this component ships as part of the
> [emu68-driver-stack](https://github.com/rondoval/emu68-driver-stack) — the downloadable
> `.lha` and bundled documentation are published there. This repository is source-only
> and versioned via git tags.

Common support library used by Emu68 AmigaOS drivers.

This package provides shared helper code such as debug output, formatted printing, GPIO helpers, device-tree wrapper utilities, a DMA memory facility (`dma_mem.h`), and a reset guard (`reset_guard.h`) that lets DMA-capable drivers quiesce their hardware before the Amiga resets.

## DMA memory (`dma_mem.h`)

On PiStorm/Emu68 only Emu68 Fast RAM — the Pi's own DRAM, advertised in the device-tree `/memory` node and added to Exec as high-priority "expansion memory" — is reachable by the Pi's PCIe / on-SoC DMA engines. Chip RAM and Zorro III / accelerator Fast RAM are not. `AllocMem(MEMF_FAST)` usually returns Emu68 RAM, but not once it is exhausted, so DMA-capable drivers cannot rely on it.

`dma_mem.h` discovers the Emu68 RAM regions once (from `/memory`) into a caller-owned `struct dma_mem_ctx` (embed it in the device base / controller struct) and offers:

- `dma_mem_init(ctx)` — discover the regions; call once early in driver init.
- `dma_addr_reachable(ctx, addr, len)` — transport-agnostic predicate (PCIe and on-SoC genet alike) for bounce-buffer decisions. Returns `TRUE` only when `[addr, addr+len)` lies entirely within Emu68 RAM; fails safe (caller bounces) when `ctx` is `NULL` or no regions were found.
- `dma_pool_create(ctx)` / `dma_pool_delete(pool)` — a region-restricted `struct dma_pool` that *always* allocates from Emu68 RAM, so persistent DMA structures and bounce buffers stay reachable even under Emu68-RAM pressure. `ctx` must outlive the pool.
- `dma_alloc(pool, align, size)` / `dma_zalloc(...)` / `dma_free(pool, ptr)` — DMA-buffer allocation from a region pool. Cache-line-aligned (or coarser) requests are rounded up so the buffer owns whole cache lines at both ends.

A `struct dma_pool *` handle is valid only for the `dma_alloc`/`dma_zalloc`/`dma_free` family. CPU-only metadata should use an ordinary Exec pool (`pool_alloc`/`pool_free` from `memory.h`).

## Reset guard (`reset_guard.h`)

`reset_guard_install(rg, prepare, user, name)` runs a driver "prepare for reset" callback before the Amiga resets, covering the Ctrl-Amiga-Amiga keyboard reset-warning protocol and `ColdReboot()` (C:Reboot, Installer, ...). The `prepare()` callback must be interrupt-safe and is invoked at most once per session. The module is ROM-able and task-less; all mutable state lives in the caller-provided `struct reset_guard`. `reset_guard_remove(rg)` is for expunge only and fails if the `ColdReboot` vector was re-patched by someone else.

## Utility headers

The remaining headers are small, mostly inline helpers shared by the drivers. Each is independent — include only what you need.

| Header | Provides |
|---|---|
| `types.h` | Fixed-width integer types (`u8`–`u64`, `s8`–`s64`), little-endian MMIO types (`__le8`–`__le64`), `dma_addr_t`, and `likely()`/`unlikely()` branch hints. |
| `bits.h` | Bit and alignment helpers: `ALIGN_UP`, `DIV_CEIL`, `BIT()`, mask extract/insert/update, `log2_floor_u32/u64`, `round_up_pow2_u32/u64`, and `u64` hi/lo splits. |
| `byteorder.h` | Endianness conversion macros (`le16`/`le32`/`le64`) for byte-swapping device data on the big-endian m68k. |
| `iomem.h` | MMIO accessors — `mmio_read{8,16,32}` / `mmio_write{8,16,32}` plus read-modify-write helpers (`mmio_update/clear/set`). |
| `devtree.h` | Device-tree lookup wrappers over `devicetree.resource`: base-address resolution (`DT_GetBaseAddress[Virtual]`), property/number reads, `DT_TranslateAddress`, and `DT_GetInterrupt`. |
| `bcm_gpio.h` | BCM2711 GPIO helpers — set pull, alternate function, and output level. |
| `timing.h` | Busy-wait timing: `get_time()`, `delay_us()` / `delay_ms()`, and `time_deadline_passed()`. |
| `memory.h` | Exec pool helpers (`pool_alloc` / `pool_zalloc` / `pool_free`) and fast `movem`-based block zeroing. |
| `slab.h` | Fixed-size object slab allocator (`slab_cache_init` / alloc / free), optionally backed by a `dma_mem` pool for DMA-reachable objects. |
| `strutil.h` | Case- and length-bounded string compares: `_Stricmp`, `_Strnicmp`, `_Strncmp`. |
| `format.h` | Bounded formatted printing: `_SNPrintf` / `_VSNPrintf`. |
| `debug.h` | Debug logging (`Kprintf`, `KprintfH`, `KASSERT`, `PrintPistorm`). Output sink set by the `EMU68_DEBUG_BACKEND` backend (`pistorm` → `0xdeadbeef` Emu68 trap; `serial` → `debug.lib` serial); compiled out for `off`. See *Debug output backend*. |
| `errors.h` | `errno`-style codes (`EINVAL`, `EIO`, `ETIMEDOUT`, `ENOMEM`, …) used by the ported hardware code. |
| `minlist.h` | `_NewMinList()` — initialise a `struct MinList` without the Kickstart V45 `NewMinList()` dependency. |

## Dependencies

- `devicetree.resource`

## Building

Use Bebbo's GCC cross compiler and CMake.

Recommended workflow: install dependencies and this package into one shared prefix.

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake \
  -DCMAKE_PREFIX_PATH=/path/to/emu68-driver-stack \
  -DCMAKE_INSTALL_PREFIX=/path/to/emu68-driver-stack
cmake --build build
cmake --install build
```

If you keep dependencies in separate install trees, point `CMAKE_PREFIX_PATH` at the `devicetree.resource` install prefix instead.

### Debug output backend

This package owns the stack-wide debug backend, selected with the
`EMU68_DEBUG_BACKEND` cache variable (default `pistorm`) and exported to all
consumers via the installed `cmake/Emu68CommonDebugBackend.cmake` module:

```sh
cmake -S . -B build ... -DEMU68_DEBUG_BACKEND=serial   # pistorm | serial | off
```

| Value     | Output                                                       | ROM-able |
|-----------|--------------------------------------------------------------|----------|
| `pistorm` | `RawDoFmt` → magic `0xdeadbeef` (Emu68/PiStorm trap)          | yes      |
| `serial`  | `debug.lib` `KPutChar` → AmigaOS serial console @ 9600 baud   | no       |
| `off`     | debug output compiled out                                    | yes      |

The module exports `emu68_debug_backend_definitions()` and
`emu68_debug_backend_finalize(<target> [ROMABLE])`, which downstream components
call instead of hardcoding `-DDEBUG` / `emu68_rom_check`.