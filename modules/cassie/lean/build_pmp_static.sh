#!/usr/bin/env bash
# Build a MinGW-g++ static library of the PMP subset CASSIE uses plus the
# Lean FFI wrapper. Output: $BUILD/libcassie_pmp.a — links into lake exes
# via moreLinkArgs in the lakefile.
#
# Run from anywhere; paths are absolute.

set -euo pipefail

REPO=E:/multiplayer-fabric-godot
LEAN_INC=/c/Users/ernest.lee/.elan/toolchains/leanprover--lean4---v4.29.1/include
BUILD=${REPO}/modules/cassie/lean/.lake/build/pmp_static
mkdir -p "$BUILD"

CXXFLAGS=(
  -c
  -std=c++17
  -O2
  -D_USE_MATH_DEFINES
  -DPMP_SCALAR_TYPE_64
  -I "$LEAN_INC"
  -I "$REPO/thirdparty/pmp/src"
  -I "$REPO/thirdparty/eigen"
)

# PMP source subset — matches SCsub's pmp_keep_algos.
PMP_SOURCES=(
  surface_mesh
  algorithms/remeshing
  algorithms/decimation
  algorithms/differential_geometry
  algorithms/normals
  algorithms/features
  algorithms/smoothing
  algorithms/utilities
  algorithms/distance_point_triangle
  algorithms/triangulation
  algorithms/curvature
  algorithms/laplace
  algorithms/numerics
)

objs=()
for s in "${PMP_SOURCES[@]}"; do
  src=$REPO/thirdparty/pmp/src/pmp/$s.cpp
  obj=$BUILD/pmp_$(echo "$s" | tr '/' '_').o
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    echo "  cc  $s"
    g++ "${CXXFLAGS[@]}" "$src" -o "$obj"
  fi
  objs+=("$obj")
done

# Lean FFI wrapper.
src=$REPO/modules/cassie/src/lean_ffi/cassie_pmp_ffi.cpp
obj=$BUILD/cassie_pmp_ffi.o
if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
  echo "  cc  cassie_pmp_ffi"
  g++ "${CXXFLAGS[@]}" "$src" -o "$obj"
fi
objs+=("$obj")

# Archive.
ar rcs "$BUILD/libcassie_pmp.a" "${objs[@]}"
echo "OK  $BUILD/libcassie_pmp.a  ($(du -h "$BUILD/libcassie_pmp.a" | cut -f1))"
