#!/usr/bin/env bash
# Build the tiny cassie_obj_ffi static archive (no third-party deps).
# Output: $BUILD/libcassie_obj.a — linked into lake exes that load OBJ
# meshes (e.g. inverse_extract, obj_probe).

set -euo pipefail

REPO=E:/multiplayer-fabric-godot
LEAN_INC=/c/Users/ernest.lee/.elan/toolchains/leanprover--lean4---v4.29.1/include
BUILD=${REPO}/modules/cassie/lean/.lake/build/obj_static
mkdir -p "$BUILD"

CXXFLAGS=(
  -c
  -std=c++17
  -O2
  -I "$LEAN_INC"
)

src=$REPO/modules/cassie/src/lean_ffi/cassie_obj_ffi.cpp
obj=$BUILD/cassie_obj_ffi.o
if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
  echo "  cc  cassie_obj_ffi"
  g++ "${CXXFLAGS[@]}" "$src" -o "$obj"
fi

ar rcs "$BUILD/libcassie_obj.a" "$obj"
echo "OK  $BUILD/libcassie_obj.a"
