# emu68-common Agent Notes

## Role

- `emu68-common` is the shared support library for the stack: the DMA-memory facility (reachability predicate + region pools) and slab allocator, byteorder / MMIO accessors, timing helpers, the reset guard, debug output, GPIO / minlist / string helpers, and devicetree wrappers.
- Changes here can affect multiple downstream repos, so prefer validating through `emu68-driver-stack` if public headers or shared helper semantics change.

## Build

- Preferred commands:
  - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_PREFIX_PATH=/path/to/emu68-driver-stack -DCMAKE_INSTALL_PREFIX=/path/to/emu68-driver-stack`
  - `cmake --build build`
  - `cmake --install build`
- `devicetree.resource` must be installed first.

## Code Handling

- Prefer adding generic helpers here instead of duplicating utility code in drivers.
- When a shared helper can replace an AmigaOS library dependency, prefer the local helper if it is genuinely reused by more than one stack component.
- Preserve public header stability where possible; downstream repos include these headers directly.
- `emu68-common` now presents as `MPL-2.0 OR GPL-2.0+` at the repository level; preserve SPDX headers on source files when editing.

## Validation

- After editing public headers, rebuild at least one downstream consumer, preferably through `emu68-driver-stack`.
- After editing source-only helpers, checking Problems plus a local build is usually enough unless signatures changed.

