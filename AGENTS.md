# emu68-common Agent Notes

## Role

- `emu68-common` is the shared support library for the stack: compat helpers, debug output, GPIO helpers, minlist helpers, and devicetree wrapper utilities.
- Changes here can affect multiple downstream repos, so prefer validating through `emu68-driver-stack` if public headers or shared helper semantics change.

## Build

- Preferred commands:
  - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_PREFIX_PATH=/path/to/emu68-driver-stack -DCMAKE_INSTALL_PREFIX=/path/to/emu68-driver-stack`
  - `cmake --build build`
  - `cmake --install build`
- `devicetree.resource` must be installed first.

## Code Handling

- Prefer adding generic helpers here instead of duplicating utility code in drivers.
- Keep `compat.h` and `compat.c` conservative: do not add libc-like helpers unless they are needed by more than one stack component.
- When a compat helper can replace an AmigaOS library dependency, prefer the compat helper to reduce library opens in drivers.
- Preserve public header stability where possible; downstream repos include these headers directly.

## Validation

- After editing public headers, rebuild at least one downstream consumer, preferably through `emu68-driver-stack`.
- After editing source-only helpers, checking Problems plus a local build is usually enough unless signatures changed.

