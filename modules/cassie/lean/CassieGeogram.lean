-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Root of `CassieGeogram` — Lean FFI bindings into the vendored
-- thirdparty/geogram library. Scope:
--   - constrained Delaunay (BDEL) construction from a 2D boundary
--   - refinement to a target edge length
-- Matches the surface area cassie_triangulator already implements in
-- C++; Lean side is a thin re-exposure for the cycle-detect pipeline
-- (boundary loop → Delaunay → PMP smoothing).
--
-- Sibling to `CassieAvbd` (proofs + Slang) and `CassiePmp` (PMP
-- surface mesh remeshing/smoothing).
--
-- C wrapper at modules/cassie/src/lean_ffi/cassie_geogram_ffi.cpp
-- (follow-up commit) resolves the @[extern] symbols.

import CassieGeogram.Delaunay
