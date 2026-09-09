#pragma once

// Thin shim that produces the triangular-face list of the 3D Delaunay
// tetrahedralization of an unconstrained point cloud.
//
// Backed by Godot's built-in Delaunay3D (R128 precision, no external dep).

namespace cassie {

// Computes the unique triangular faces of the 3D Delaunay
// tetrahedralization. The resulting `trifacelist` holds
// `numberoftrifaces * 3` vertex indices, each triple a triangle.
// Each unique triangle is emitted exactly once (no duplicates from
// shared internal faces) with vertex indices sorted ascending.
struct DelaunayFaces {
    DelaunayFaces();
    ~DelaunayFaces();
    DelaunayFaces(const DelaunayFaces&)            = delete;
    DelaunayFaces& operator=(const DelaunayFaces&) = delete;

    int  numberoftrifaces = 0;
    int* trifacelist      = nullptr;

    // Returns true on success. On failure (empty point set, Geogram
    // exception, etc.) leaves numberoftrifaces = 0 and trifacelist null.
    bool compute(const double* points, int npoints);

    // Releases trifacelist storage. Called by the destructor.
    void clear();
};

}  // namespace cassie
