# Ladybird Windows Platform Layer (Experimental)

This project provides a native Windows backend for the Ladybird browser.

## Features
- CMake-based build using clang-cl and Ninja
- Basic window creation with RGBA buffer blitting via GDI
- Minimal platform vtable interface (`LB_PlatformV1`)

## Build
```bash
cmake --preset win-clangcl-debug
cmake --build --preset win-clangcl-debug
./out/build/win-clangcl-debug/bin/lbw_bootstrap.exe
```
