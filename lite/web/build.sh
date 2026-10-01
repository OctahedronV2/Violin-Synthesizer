#!/bin/sh
# Builds lite.wasm from the shared engine. Needs only clang and wasm-ld.
set -e
cd "$(dirname "$0")"
clang --target=wasm32 -O3 -std=c++17 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
  -mbulk-memory -Wl,--no-entry -Wl,-z,stack-size=1048576 -Wl,--strip-all -Wl,--export=__wasm_call_ctors \
  -o lite.wasm wasm.cpp
ls -l lite.wasm
