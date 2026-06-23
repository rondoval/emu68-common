# Release notes — emu68-common 1.6.0

Changes since 1.5.0.

---

## Breaking changes

### `mem_zero()` removed — use standard `memset()`

The entire hand-rolled `mem_zero*` family is gone from `memory.h` (`mem_zero`, the
`mem_zero_asm_clr1`/`clr4`/`movem` size buckets, `mem_zero_align_long`,
`mem_zero_tail`, `mem_zero_asm_movem_impl`, and the `MEM_ZERO_*` thresholds).
Callers must switch to the standard `memset(dst, 0, n)`.  The asm helper file was
renamed `memzero_movem.S` → `memset_movem.S` and now takes a broadcast fill value.  In-tree consumers (`dma_zalloc`, `slab_zalloc`, `pool_zalloc`,
`reset_guard_install`) were updated.

### emu68-common no longer hard-codes `-DDEBUG`

The common library used to compile with `-DDEBUG` unconditionally.  Debug output is
now selected through the new stack-wide backend (see below); components opt in via
`emu68_debug_backend_definitions()` / `emu68_debug_backend_finalize()` (or the
stack top-level build) instead of defining `DEBUG` themselves.

---

## New features (new APIs / build)

### Freestanding C runtime memory primitives (`memset`/`memcpy`/`memmove`/`memcmp`)

`memory.h` now declares — and the new `src/memory.c` defines — the four memory
primitives the compiler may synthesise out of ordinary loops and aggregate
init/assignment.  These must exist as real symbols because the drivers are built
`-ffreestanding -nostdlib` with no libc:

```c
void *memset(void *dst, int c, __SIZE_TYPE__ n);
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
void *memmove(void *dst, const void *src, __SIZE_TYPE__ n);
int   memcmp(const void *s1, const void *s2, __SIZE_TYPE__ n);
```

`memset` is the asm-optimised byte fill (the generalised former `mem_zero`);
`memcpy`/`memmove` route through Exec `CopyMem` (with an in-house descending copy
for the rare overlapping `dst > src` case, since `CopyMem` is not overlap-safe);
`memcmp` is a long-at-a-time compare with a byte tail.  The length type is the
compiler's `__SIZE_TYPE__` (the builtin `size_t`) named directly rather than via
`<stddef.h>`, so the header stays compatible with TUs that redefine `size_t`
(e.g. `emu68-pcie-library`'s `u64 size_t`).  `memory.c` is built
`-fno-builtin -fno-tree-loop-distribute-patterns` so its own copy/fill/compare
loops are not rewritten into self-referential calls.

### Stack-wide debug output backend (`EMU68_DEBUG_BACKEND`)

A new installed CMake module (`Emu68CommonDebugBackend.cmake`, auto-included by
`find_package(Emu68Common)`) turns one cache variable into the right compile
definitions and link steps for the whole stack — a single `libcommon.a` is shared
by every component, so the backend is a stack-wide property:

```sh
cmake -S . -B build ... -DEMU68_DEBUG_BACKEND=serial   # pistorm | serial | off
```

| Value     | Output                                                     | ROM-able |
|-----------|------------------------------------------------------------|----------|
| `pistorm` | `RawDoFmt` → magic `0xdeadbeef` Emu68/PiStorm trap (default)| yes      |
| `serial`  | `debug.lib` `KPutChar` → AmigaOS serial console @ 9600 baud | no       |
| `off`     | debug output compiled out                                  | yes      |

The module exports `emu68_debug_backend_definitions()` (apply the backend's
compile definitions before a directory's targets) and
`emu68_debug_backend_finalize(<target> [ROMABLE])` (link `libdebug.a` plus a weak
`__divsi3` glue for the serial backend, or run the ROM check for `ROMABLE`
targets) so downstream components stop hardcoding `-DDEBUG` / `emu68_rom_check`.
`debug.h` gained the matching `DEBUG_SERIAL` code path; with the backend `off` the
logging macros expand to `((void)0)` (a statement, so `if (x) Kprintf(...);`
keeps a body and doesn't trip `-Wempty-body`).

### Verbose logging opt-in (`EMU68_DEBUG_HIGH`)

A per-component CMake option (`EMU68_DEBUG_HIGH`, default `OFF`) enables the
`DEBUG_HIGH` / `KprintfH` verbose tier.  It layers on top of `DEBUG`, so it only
emits when the backend is `pistorm` or `serial` and is a no-op for `off`.

### NDK 3.9 (and pre-3.2) compatibility

The stack now builds against newer and older NDKs as well as the target NDK 3.2:

- **const-correctness probe** — a `check_c_source_compiles` test detects a
  non-const-correct (pre-3.2) NDK whose `CopyMem()` takes a non-const source and,
  when found, suppresses just `-Wdiscarded-qualifiers` across the stack.  The
  result (`EMU68_NDK_NONCONST`) is baked into the exported package config so every
  `find_package(Emu68Common)` consumer inherits the same decision; on NDK 3.2 the
  probe passes and the warning stays fully enabled.
- **`minlist.h`** — fallback macros for the NDK 3.2 type-safe wrappers
  (`AddHeadMinList`, `AddTailMinList`, `RemHeadMinList`, `RemoveMinNode`) so code
  using them builds on any NDK while staying identical on 3.2.
- **`types.h`** — pulls in `<stdint.h>` for the fixed-width types older NDKs don't
  expose via `exec/types.h`.
- **`dma_mem.c`** — device-tree cells are read as `u32` (DT cells are 32-bit,
  matching `DT_GetNumber` on any NDK).

### Versioning workflow

A `.github/workflows/versioning.yml` that gates every PR on a version bump + a
`RELEASE-NOTES.md` update + a clean build inside the stack, and auto-tags
`v<version>` on merge to `main`, via the stack's reusable
`component-versioning.yml` so all components stay in lock-step.

---

# Release notes — emu68-common 1.5.0

Changes since v1.3.

---

## Breaking changes

### `dma_alloc` / `dma_zalloc` / `dma_free` moved to `dma_mem.h` and now take a `struct dma_pool *`

The DMA-buffer allocator family has moved out of `memory.h` into the new `dma_mem.h`,
and its first argument changed from a raw Exec pool header (`APTR poolHeader`) to a
region pool handle (`struct dma_pool *`).  Consumers must create a pool with
`dma_pool_create()` and pass that handle instead of an Exec pool.  Code that included
only `memory.h` for `dma_alloc`/`dma_zalloc`/`dma_free` must now include `dma_mem.h`.
The plain Exec-pool helpers (`pool_alloc`/`pool_free`/`pool_zalloc`) are unchanged and
stay in `memory.h`.

### `slab_cache_init()` signature changed

`slab_cache_init()` gained a `struct dma_pool *dma_pool` parameter, split from the
single former `pool` argument:

```c
void slab_cache_init(struct slab_cache *cache, APTR meta_pool, struct dma_pool *dma_pool,
                     ULONG obj_size, ULONG obj_align, ULONG slab_capacity);
```

`meta_pool` is an ordinary Exec pool used for slab nodes (and for the object data when
`dma_pool` is `NULL`).  Passing a non-`NULL` `dma_pool` makes the object data
DMA-reachable (Emu68 RAM); passing `NULL` yields a CPU-only slab whose data comes from
`meta_pool` and is only pointer-aligned (CPU-only objects no longer force
`DMA_ALIGN_MIN` alignment).  All callers must update to the new argument list.

---

## New features (new APIs)

### DMA memory facility (`dma_mem.h`)

A new facility for DMA-safe memory on PiStorm/Emu68.  Only Emu68 Fast RAM — the Pi's own
DRAM, advertised in the device-tree `/memory` node and added to Exec as priority-40
"expansion memory" — is reachable by the Pi's PCIe / on-SoC DMA engines; Chip RAM and
Zorro III / accelerator Fast RAM are not.  The facility discovers the Emu68 RAM regions
once into a caller-owned `struct dma_mem_ctx` (embed it in the device base / controller
struct) and exposes:

- `dma_mem_init(ctx)` — discover the Emu68 RAM regions from `/memory`; call once early in
  driver init, before the predicate or pool calls.
- `dma_addr_reachable(ctx, addr, len)` — transport-agnostic reachability predicate (PCIe
  and on-SoC genet alike) for bounce-buffer decisions.  Returns `TRUE` only when
  `[addr, addr+len)` lies entirely within Emu68 RAM; fails safe (caller bounces) when
  `ctx` is `NULL`, and degrades to the historical "above Chip RAM" heuristic if no
  device-tree regions were found.
- `dma_pool_create(ctx)` / `dma_pool_delete(pool)` — a region-restricted `struct dma_pool`
  that always allocates from Emu68 RAM, so a driver's persistent DMA structures and bounce
  buffers stay reachable even under Emu68-RAM pressure.  `ctx` must outlive the pool.
- `dma_pool_region_alloc()` / `dma_pool_region_free()` — the raw region sub-allocator;
  prefer the `dma_alloc` family, which adds cache-line alignment and size bookkeeping.

### Cache-line-aligned `dma_alloc` (`dma_mem.h`)

`dma_alloc(pool, align, size)` (and `dma_zalloc`/`dma_free`) now allocate from a region
pool.  When the requested alignment is cache-line (`DMA_ALIGN_MIN`) or coarser, the size
is rounded up so the buffer owns whole cache lines at both ends.  This prevents a partial
trailing cache line shared with the next allocation from being discarded by the post-DMA
invalidate, per the `68040.library` CachePreDMA/PostDMA contract.

### Reset guard (`reset_guard.h`)

`reset_guard_install(rg, prepare, user, name)` / `reset_guard_remove(rg)` let a
DMA-capable driver run a "prepare for reset" callback before the Amiga resets, covering
both reset flavors on PiStorm/Emu68: the Ctrl-Amiga-Amiga keyboard reset-warning protocol
and `ColdReboot()` (C:Reboot, Installer, ...).  The `prepare()` callback must be
interrupt-safe (MMIO, `timing.h` busy-waits, `Disable()`/`Enable()`, and
interrupt-callable Exec primitives only — no `Wait()`, allocation, or DOS), bounded (keep
the per-driver worst case ≤ 5 s of the shared ~10 s keyboard grace period), and runs at
most once per session.  The module is ROM-able and task-less; all mutable state lives in
the caller-provided `struct reset_guard`.  `reset_guard_remove()` is for expunge only and
fails (the device must stay resident) if the `ColdReboot` vector was re-patched by someone
else.

### `_Strncmp()` (`strutil.h`)

A case-sensitive, length-bounded string compare to complement the existing `_Stricmp` and
`_Strnicmp`:

```c
LONG _Strncmp(CONST_STRPTR s1, CONST_STRPTR s2, LONG len);
```

### `emu68_rom_check()` CMake helper

A new installed CMake helper (`Emu68CommonRomCheck.cmake`, auto-included by
`find_package(Emu68Common)`).  `emu68_rom_check(<target>)` adds a POST_BUILD step that
fails the build if the linked output contains a non-empty `.data` or `.bss` section,
catching writable statics that would silently break a ROM-resident module.

### Misc helpers

- `round_up_pow2_u32()` / `log2_floor_u32()` (`bits.h`) — 32-bit power-of-two helpers that
  avoid the 64-bit shift (`__ashldi3`) the `u64` variants require, which is unavailable in
  the freestanding m68k toolchain.
- `delay_ms()` (`timing.h`) — millisecond busy-wait wrapper over `delay_us()`.

---

## Bug fixes / Improvements

### `DT_GetPropertyValueULONG` parent-check fix

The length validation used `||` where it should have used `&&`, so the property length
was effectively never checked.  A too-short property could be read as a 4-byte ULONG.
Corrected to `&&` so the value is read only when the property is present and at least 4
bytes.

### Slab allocator: separate metadata and DMA pools

The slab cache now distinguishes its metadata (Exec) pool from an optional DMA region
pool.  CPU-only slabs allocate object data from the metadata pool with pointer alignment
instead of being forced to `DMA_ALIGN_MIN`, while DMA slabs draw object data from the
region pool so it is guaranteed Emu68-reachable.

### ROM-ability of new modules

Both `dma_mem.c` and `reset_guard.c` keep no file-scope mutable state — discovered data
lives in caller-owned structs and heap-allocated puddle/trampoline structs — so they are
safe to link into ROM-resident drivers.


# Release notes — emu68-common 1.3

**Full Changelog**: https://github.com/rondoval/emu68-common/commits/v1.3
