/-!
# `CassiePmp.Mesh` — PMP SurfaceMesh FFI

Lean-side handle for `pmp::SurfaceMesh` plus `@[extern]` declarations
for the subset of the PMP API we need for surface fairing on
CassieSketchGraph cycles:

  - construct from a flat (positions, triangles) pair
  - feature-flag the boundary so split_long_edges keeps the polyline
    geometry intact
  - `pmp::uniform_remeshing` (target_edge_length, iters, use_projection)
  - `pmp::implicit_smoothing` (timestep, hold_boundary)
  - extract back to (positions, triangles)

The C wrappers live in `cassie_pmp_ffi.cpp` (see SCsub follow-up); they
allocate a `pmp::SurfaceMesh` on the heap, return an opaque handle as
a `USize`, and expose each PMP operation as a flat extern "C" function.

This file is the Lean interface — the codegen wiring + native facet
support lands in a separate commit, alongside `cassie_pmp_ffi.cpp`.
Until then the symbols resolve to lean_panic stubs.
-/

namespace CassiePmp

/-- Opaque handle for a heap-allocated `pmp::SurfaceMesh`. The C side
    owns the object; `meshFree` releases it. -/
abbrev MeshHandle := USize

/-- Build a `pmp::SurfaceMesh` from flat (positions, triangles). The
    `positions` array holds `n_verts * 3` floats (xyz xyz ...). The
    `tris` array holds `n_tris * 3` uints (a b c | a b c | ...). -/
@[extern "cassie_pmp_mesh_new"]
opaque meshNew (n_verts : USize) (positions : @& FloatArray)
    (n_tris : USize) (tris : @& ByteArray) : IO MeshHandle

/-- Release a mesh allocated by `meshNew`. -/
@[extern "cassie_pmp_mesh_free"]
opaque meshFree (m : MeshHandle) : IO Unit

/-- Mark every boundary edge as `e:feature` so `split_long_edges`
    keeps the polyline curve intact during remeshing. -/
@[extern "cassie_pmp_mark_boundary_feature"]
opaque markBoundaryFeature (m : MeshHandle) : IO Unit

/-- `pmp::uniform_remeshing(mesh, target_edge_length, iterations,
    use_projection)`. Re-tessellates to roughly uniform edge length;
    when `use_projection = true`, refined vertices are projected back
    onto the input surface. -/
@[extern "cassie_pmp_uniform_remeshing"]
opaque uniformRemeshing (m : MeshHandle) (targetEdgeLength : Float)
    (iters : USize) (useProjection : Bool) : IO Unit

/-- `pmp::implicit_smoothing(mesh, timestep, hold_boundary)`.
    Backward-Euler Laplacian smoothing — pulls each interior vertex
    toward the average of its neighbors by a fraction set by
    `timestep`. Boundary feature edges stay pinned when
    `hold_boundary = true`. This is the "give the patch some volume"
    operator for the cycle-detect → triangulate → fair pipeline. -/
@[extern "cassie_pmp_implicit_smoothing"]
opaque implicitSmoothing (m : MeshHandle) (timestep : Float)
    (holdBoundary : Bool) : IO Unit

/-- Number of vertices currently in the mesh. -/
@[extern "cassie_pmp_n_vertices"]
opaque nVertices (m : MeshHandle) : IO USize

/-- Number of faces currently in the mesh. -/
@[extern "cassie_pmp_n_faces"]
opaque nFaces (m : MeshHandle) : IO USize

/-- Copy positions back into a flat float array (xyz xyz ...). The
    `out` array must be at least `n_vertices m * 3` floats long. -/
@[extern "cassie_pmp_get_positions"]
opaque getPositions (m : MeshHandle) (out : FloatArray) : IO FloatArray

/-- Copy triangle indices back into a flat uint array (a b c | ...).
    The `out` ByteArray must be at least `n_faces m * 3 * 4` bytes
    long. Returns the populated ByteArray. -/
@[extern "cassie_pmp_get_triangles"]
opaque getTriangles (m : MeshHandle) (out : ByteArray) : IO ByteArray

end CassiePmp
