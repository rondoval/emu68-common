# emu68-common

Common support library used by Emu68 AmigaOS drivers.

This package provides shared helper code such as debug output, formatted printing, GPIO helpers, device-tree wrapper utilities, and a reset guard (`reset_guard.h`) that lets DMA-capable drivers quiesce their hardware before the Amiga resets.

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