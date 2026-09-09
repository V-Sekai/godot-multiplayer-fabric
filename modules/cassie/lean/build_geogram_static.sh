#!/usr/bin/env bash
# Build a MinGW-g++ static library of the geogram subset CASSIE uses,
# plus the geogram-specific Lean FFI wrapper. Output:
#   $BUILD/libcassie_geogram.a  — links into lake exes (see lakefile).
#
# Subset mirrors modules/cassie/SCsub's geogram_sources list (basic_skip,
# delaunay_skip, mesh_keep) so we don't drag in the GPL/non-commercial
# pieces upstream cassie-triangulation also excludes.

set -euo pipefail

REPO=E:/multiplayer-fabric-godot
LEAN_INC=/c/Users/ernest.lee/.elan/toolchains/leanprover--lean4---v4.29.1/include
GEO_ROOT=$REPO/thirdparty/geogram/src/lib
BUILD=${REPO}/modules/cassie/lean/.lake/build/geogram_static
mkdir -p "$BUILD"

CXXFLAGS=(
  -c
  -std=c++17
  -O2
  -fPIC
  -fno-exceptions
  -DGEOGRAM_WITH_BUILTIN_DEPS
  -DGEOGRAM_USE_BUILTIN_DEPS
  -DGEO_OS_WINDOWS
  -D_USE_MATH_DEFINES
  -DGEO_DYNAMIC_LIBS=
  -DGEOGRAM_PSM=
  -I "$LEAN_INC"
  -I "$GEO_ROOT"
  -I "$GEO_ROOT/geogram/third_party"
  -I "$GEO_ROOT/geogram/third_party/numerics/include"
  -I "$GEO_ROOT/geogram/third_party/OpenNL"
)

CFLAGS=(
  -c
  -O2
  -fPIC
  -DGEOGRAM_PSM=
  -I "$LEAN_INC"
  -I "$GEO_ROOT"
  -I "$GEO_ROOT/geogram/third_party"
  -I "$GEO_ROOT/geogram/third_party/numerics/include"
  -I "$GEO_ROOT/geogram/third_party/OpenNL"
)

# basic_skip
basic_skip=(geofile.cpp android_utils.cpp line_stream.cpp progress.cpp)
# delaunay_skip
delaunay_skip=(delaunay_2d.cpp delaunay_tetgen.cpp delaunay_triangle.cpp parallel_delaunay_3d.cpp)
# mesh_keep
mesh_keep=(mesh.cpp mesh_reorder.cpp)
# points_skip
points_skip=(co3ne.cpp)

skip_contains() {
    local needle="$1"; shift
    for s in "$@"; do
        if [ "$s" = "$needle" ]; then return 0; fi
    done
    return 1
}

objs=()
compile_dir() {
    local sub="$1"; shift
    local skip=("$@")
    for src in "$GEO_ROOT/geogram/$sub"/*.cpp; do
        [ -f "$src" ] || continue
        local base=$(basename "$src")
        if skip_contains "$base" "${skip[@]}"; then
            continue
        fi
        local obj="$BUILD/geo_${sub//\//_}_${base%.cpp}.o"
        if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
            echo "  cc  $sub/$base"
            g++ "${CXXFLAGS[@]}" "$src" -o "$obj"
        fi
        objs+=("$obj")
    done
}

compile_dir_keeplist() {
    local sub="$1"; shift
    local keep=("$@")
    for src in "$GEO_ROOT/geogram/$sub"/*.cpp; do
        [ -f "$src" ] || continue
        local base=$(basename "$src")
        if ! skip_contains "$base" "${keep[@]}"; then
            continue
        fi
        local obj="$BUILD/geo_${sub//\//_}_${base%.cpp}.o"
        if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
            echo "  cc  $sub/$base"
            g++ "${CXXFLAGS[@]}" "$src" -o "$obj"
        fi
        objs+=("$obj")
    done
}

compile_dir "basic" "${basic_skip[@]}"
compile_dir "numerics"
compile_dir_keeplist "mesh" "${mesh_keep[@]}"
compile_dir "delaunay" "${delaunay_skip[@]}"
compile_dir "points" "${points_skip[@]}"
compile_dir "api"
compile_dir "bibliography"
compile_dir "third_party/predicate_generator"
compile_dir "third_party/numerics"

# OpenNL is C, not C++.
for src in "$GEO_ROOT/geogram/third_party/OpenNL"/*.c; do
    [ -f "$src" ] || continue
    base=$(basename "$src")
    obj="$BUILD/geo_OpenNL_${base%.c}.o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  cc  third_party/OpenNL/$base"
        gcc "${CFLAGS[@]}" "$src" -o "$obj"
    fi
    objs+=("$obj")
done

# Lean FFI wrapper.
src=$REPO/modules/cassie/src/lean_ffi/cassie_geogram_ffi.cpp
obj=$BUILD/cassie_geogram_ffi.o
if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    echo "  cc  cassie_geogram_ffi"
    g++ "${CXXFLAGS[@]}" "$src" -o "$obj"
fi
objs+=("$obj")

# Archive members go in alphabetical (glob) order — this lines up with the
# static-init order lld walks at startup. Building from objs[] in compile
# order put one TU's ctor ahead of its required initializer and segfaulted
# at startup. Sorted order is the known-good sequence; see commit
# 575787071af for the bisection. Output is the "_half.a" name only because
# the bisection started from a no-ctor subset; it's actually the full
# 58-object archive.
ar rcs "$BUILD/libcassie_geogram_half.a" $(printf '%s\n' "${objs[@]}" | sort)
echo "OK  $BUILD/libcassie_geogram_half.a  ($(du -h "$BUILD/libcassie_geogram_half.a" | cut -f1))  $(echo "${#objs[@]}") objects"

# Also produce the wrapper-only archive so lake's moreLinkArgs can pull
# the FFI symbols without needing -Wl,--whole-archive.
ar rcs "$BUILD/libcassie_geogram_ffi.a" "$BUILD/cassie_geogram_ffi.o"
echo "OK  $BUILD/libcassie_geogram_ffi.a  (wrapper-only)"
