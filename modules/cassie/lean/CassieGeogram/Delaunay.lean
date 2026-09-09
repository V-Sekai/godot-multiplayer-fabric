/-!
# `CassieGeogram.Delaunay` — geogram BDEL Delaunay FFI

Lean-side handle for the geogram 2D constrained Delaunay used by
`cassie_triangulator`. The minimum surface we need from the geogram
side for the cycle-detect → triangulate → fair pipeline:

  - construct a 2D boundary polyline as a constrained delaunay
  - refine to a target edge length
  - extract vertices + triangles

`cassie_triangulator::triangulate(boundary, edge_length)` already
implements this in C++ — the FFI is a thin re-exposure of that entry
plus the geogram bindings the wrapper relies on.
-/

namespace CassieGeogram

/-- Opaque handle for a heap-allocated geogram Delaunay context. -/
abbrev DelaunayHandle := USize

/-- Build a constrained Delaunay from a flat boundary polyline
    (xyz xyz ...). The input must be a closed loop — the last vertex
    implicitly connects back to the first. `target_edge_length` sets
    the BDEL refinement budget. -/
@[extern "cassie_geogram_delaunay_from_boundary"]
opaque delaunayFromBoundary (n_pts : USize) (positions : @& FloatArray)
    (targetEdgeLength : Float) : IO DelaunayHandle

/-- Release a Delaunay handle. -/
@[extern "cassie_geogram_delaunay_free"]
opaque delaunayFree (d : DelaunayHandle) : IO Unit

/-- Number of vertices in the produced triangulation. -/
@[extern "cassie_geogram_delaunay_n_vertices"]
opaque nVertices (d : DelaunayHandle) : IO USize

/-- Number of triangles in the produced triangulation. -/
@[extern "cassie_geogram_delaunay_n_triangles"]
opaque nTriangles (d : DelaunayHandle) : IO USize

/-- Copy positions back into a flat float array (xyz xyz ...). The
    `out` array must be at least `n_vertices d * 3` floats long. -/
@[extern "cassie_geogram_delaunay_get_positions"]
opaque getPositions (d : DelaunayHandle) (out : FloatArray) : IO FloatArray

/-- Copy triangle vertex indices back. The `out` ByteArray must be at
    least `n_triangles d * 3 * 4` bytes long. -/
@[extern "cassie_geogram_delaunay_get_triangles"]
opaque getTriangles (d : DelaunayHandle) (out : ByteArray) : IO ByteArray

end CassieGeogram
