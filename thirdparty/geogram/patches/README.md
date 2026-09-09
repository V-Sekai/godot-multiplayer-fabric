# Geogram patchset for the multiplayer-fabric-godot fork

Geogram (BSD-3, upstream `BrunoLevy/geogram` at commit `c40b4653`,
release 1.9.9) was vendored via `git subtree add` to restore the
real-time 3D Delaunay path that upstream `cassie-triangulation` uses.
See ENG-88 in Linear for the full perf rationale.

Godot is built with `-fno-exceptions`. Geogram's source contains a few
hundred `throw` sites that block compilation under that flag, even
though in our usage **none of them fire** (they're guards against
invalid-dimension Delaunay calls, parse errors in code paths we don't
hit, etc.).

This directory holds the small patchset that resolves the constraint
without diverging from upstream's algorithm:

1. **Compilation-blocker patches** (headers + .cpp files we *must*
   compile): replace `throw X(args)` with `geo_runtime_abort_impl(...)`
   — a static function that logs the would-have-thrown class name and
   calls `std::abort()`. Behaviour-equivalent for non-error paths;
   crashes loudly instead of unwinding on actual bad input. Matches
   the "fail fast" philosophy of an in-engine triangulator.

   A related **web-build patch** lives in `basic/common.cpp`: upstream's
   `GeogramLibSingleton` constructor mounts the node.js filesystem via
   `EM_ASM(... FS.mount(NODEFS, ...) ...)` under `GEO_OS_EMSCRIPTEN`.
   Godot's web export targets the browser, never node, so the mount is
   dead code — and the injected `NODEFS` global is undeclared to the
   closure compiler, failing `use_closure_compiler=yes` builds. The
   `EM_ASM` block is dropped.

2. **Excluded-from-build .cpp files** (paths we never reach): listed
   in `modules/cassie/SCsub`'s `_without(...)` calls. Not patched —
   simpler to drop than to maintain a patch against unused code. The
   exclude list is:

   - `basic/geofile.cpp` — GeoFile binary serialization (also has a
     zlib macro collision under MinGW, separate from the throw issue)
   - `basic/android_utils.cpp` — Android JNI
   - `basic/boolean_expression.cpp` — CSG expression parser
   - `basic/line_stream.cpp` — file-format line reader
   - `basic/progress.cpp` — progress UI callbacks
   - `delaunay/delaunay_2d.cpp` — we use 3D only
   - `delaunay/delaunay_tetgen.cpp` — TetGen creator (AGPL, excluded)
   - `delaunay/delaunay_triangle.cpp` — Triangle creator (excluded)
   - `delaunay/parallel_delaunay_3d.cpp` — PDEL needs OpenMP we don't
     enable; serial BDEL is sufficient for nB ≤ a few hundred
   - `voronoi/RVD.cpp` — Voronoi diagram, not needed for Delaunay
   - `mesh/mesh_CSG_compiler.cpp` — CSG
   - `mesh/mesh_io.cpp` — file-format readers/writers

## Applying the patches

Patches are pre-applied to the vendored source tree. The `.patch`
files here are kept as documentation of what changed relative to
upstream `c40b4653` so the diffs survive future `git subtree pull`
operations (which will reject conflicts on the patched lines).

To re-apply after a fresh subtree pull:

```
git apply --directory=thirdparty/geogram thirdparty/geogram/patches/*.patch
```

To regenerate after editing the patched files:

```
cd thirdparty/geogram
git diff <upstream-sha> -- src/lib/geogram > patches/0001-...-NAME.patch
```

## Status

* `0001-no-exceptions-string-h.patch` — string.h `to_int`/`to_double`/`to_bool`
  ConversionError sites. Replaced with `geo_runtime_abort_impl`. **Pending — written but not yet applied.**
* `0002-android-new-handler-include.patch` — `memory.h` adds `#include <new>`
  so `std::new_handler` / `std::get_new_handler` resolve on Android's libc++
  (glibc++/MSVC pull `<new>` in transitively; the Android NDK toolchain does
  not, breaking the `android.editor.arm64` build). **Applied.**

Next-session inventory of remaining throws to convert:

| File | throws |
| -- | -- |
| `basic/assert.h` | 1 |
| `basic/assert.cpp` | 3 |
| `basic/common.h` | 1 (in `GEO_NOEXCEPT` ladder) |
| `basic/memory.h` | a few |
| `basic/progress.h` | 1 |
| `delaunay/delaunay.h` | 1 (InvalidInput) |
| `delaunay/delaunay_3d.cpp` | 1 (InvalidDimension — never fires for us) |
| `delaunay/delaunay_3d.h` | 1 |

Total compile-blocker patch size: ~50 lines across ~8 files.

## Future re-enables

### Frame field for Blender quad export

`mesh/mesh_frame_field.cpp` — Geogram's 4-symmetric direction field that
underpins tri→quad conversion. **No-exceptions patches are already
applied** (the two `try/catch` sites at the `LineInput` file load and the
`ProgressTask`-wrapped smoothing loop both flattened to direct code per
the no-exceptions convention). The file is *patched but not built* —
flipping it on is one SCsub line:

1. Remove `"mesh_frame_field.cpp"` from `mesh_skip` in `modules/cassie/SCsub`.
2. Likely also re-add `parameterization/mesh_PGP_2d.cpp`,
   `parameterization/mesh_global_param.cpp`, and possibly
   `parameterization/mesh_atlas_maker.cpp` — frame field depends on the
   PGP (Periodic Global Parameterization) solver. Each of those has
   its own throw inventory to convert.
3. Build → iterate on the throw sites the parameterization pull-in
   surfaces — same pattern as ENG-88.

**Architecture — quad-first, per-CASSIE-3D-patch.**

The current `triangulate → refine_patch → done` is tri all the way
through. The right shape is to emit each CASSIE 3D patch *as a quad
mesh* — frame field and quad remesh run on each small per-cycle patch
(~100–200 verts post-DMWT), not on a unified mesh at export time:

1. `CassieSketchGraph::sample_cycle_boundary` → boundary polyline.
2. `CassieTriangulator::triangulate` (Geogram BDEL, ENG-88, shipped)
   → small tri patch.
3. **Frame field on the patch** — 4-symmetric direction field. Cheap
   at this size.
4. **Quad remesh using the field** — replaces tri `refine_patch`.
   Geogram's `mesh_remesh` (currently excluded — needs throw patches)
   or PMP's `isotropic_remeshing` are the candidates.
5. `CassieSurfaceManager::_materialize_patch_sync` returns a
   `CassieSurfacePatch` whose mesh is already quad — ready for
   Blender, subdivision surfaces (Catmull-Clark), or sculpting.

Per-patch scope keeps the cost trivial (no giant frame-field solve)
and matches `CassieSurfaceManager`'s existing one-patch-per-cycle
flow — async via `ENG-86` still applies unchanged.

The frame_field no-exceptions patches in this commit are the first
prerequisite. Next patches:
* `parameterization/mesh_atlas_maker.cpp` — single `catch(...)` site
  (the PGP/global_param solvers it composes have no throws). **Done.**
* `mesh/mesh_remesh.cpp` — for the quad remesh step itself. **Done.**

### How to enable

Flip `BUILD_QUAD_MESHING = True` in `modules/cassie/SCsub`. That:

* defines `CASSIE_QUAD_MESHING` so
  `CassieSurfaceManager::_patch_from_tri_result` calls
  `cassie_quadrangulate_patch` on each per-cycle tri patch before it
  becomes a `CassieSurfacePatch`;
* adds `mesh_frame_field.cpp`, `mesh_remesh.cpp`, and
  `parameterization/*.cpp` to the compiled Geogram set.

`modules/cassie/src/sketch/cassie_quad_mesh.cpp` holds the
implementation stub — `cassie_quadrangulate_patch` is currently a
no-op that returns `false`, so even with the toggle on the tri mesh
ships unchanged. Wire the real Geogram frame field + remesh calls
into that function when the Blender quad export work begins.
* `delaunay/delaunay_2d.cpp` — excluded today. Re-enable if a 2D
  Delaunay user appears.

PDEL (parallel_delaunay_3d.cpp) is **permanently excluded** — we do
not wire in OpenMP. Single-thread BDEL is fast enough at our nB scale.
