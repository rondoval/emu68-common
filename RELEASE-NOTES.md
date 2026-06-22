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
