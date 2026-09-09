-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Root of `CassiePmp` — Lean FFI bindings into the vendored
-- thirdparty/pmp library, scoped to the operations the CASSIE
-- surface-fairing pipeline needs: `uniform_remeshing` and
-- `implicit_smoothing` on a `pmp::SurfaceMesh` with `e:feature`
-- boundary preservation.
--
-- Sibling to `CassieAvbd` (the proof + Slang library) and
-- `CassieGeogram` (Delaunay / BDEL triangulator bindings, follow-up).
--
-- The native side that resolves the `@[extern]` symbols ships as a
-- C wrapper at `modules/cassie/src/lean_ffi/cassie_pmp_ffi.cpp`
-- (follow-up commit); until then the symbols panic at call time
-- and the Lean tree still type-checks.

import CassiePmp.Mesh
