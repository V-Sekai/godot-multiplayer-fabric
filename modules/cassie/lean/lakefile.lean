-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Lake build config for the CASSIE AVBD Lean proofs + Slang kernels
-- (ENG-50 + ENG-52 phase 4.1b).
--
-- The DiffCloth Cloth + Cloth/SlangCodegen Lean tree (originally at
-- V-Sekai/TOOL_cloth_dynamics/lean) is vendored in this directory —
-- previous attempts to require it as a git subdirectory failed because
-- Lake didn't accept the `/ "lean"` subpath. Vendoring keeps the proofs
-- + Slang kernels co-located with the C++ that consumes the generated
-- header. See ../lean/README.md for the pipeline rationale and the
-- ../memory/reference_lean-shader-slang.md memory.

import Lake
open System Lake DSL

package «cassie-avbd» where

-- LeanSlang — the AST + emitter for Slang shader source.
require LeanSlang from git
  "https://github.com/V-Sekai-fire/lean-slang.git" @ "v0.0.6"

-- DiffCloth's proved AVBD kernel library + host-side AVBD data
-- (Cloth.Avbd.{AdjacencySpring, AdjacencyKwise, Coloring}) — vendored.
lean_lib «Cloth» where
  roots := #[`Cloth]

-- CASSIE-specific composition layer that imports Cloth.* and exports
-- the CASSIE-shaped theorem statements + codegen entry-point.
lean_lib «CassieAvbd» where
  roots := #[`CassieAvbd]

-- PMP FFI bindings: Lean side of `pmp::SurfaceMesh` +
-- `uniform_remeshing` / `implicit_smoothing` for the surface-fairing
-- step of the cycle-detect pipeline. C wrapper resolution lives at
-- modules/cassie/src/lean_ffi/cassie_pmp_ffi.cpp (follow-up).
lean_lib «CassiePmp» where
  roots := #[`CassiePmp]

-- geogram BDEL Delaunay bindings — boundary → Delaunay → vertices /
-- triangles. Thin re-exposure of cassie_triangulator's existing C++
-- entry point. C wrapper at
-- modules/cassie/src/lean_ffi/cassie_geogram_ffi.cpp (follow-up).
lean_lib «CassieGeogram» where
  roots := #[`CassieGeogram]

-- OBJ loader bindings — a heap-allocated handle around vertex
-- positions + triangle indices. Kept available for OBJ-driven probes
-- (obj_probe). C wrapper at
-- modules/cassie/src/lean_ffi/cassie_obj_ffi.cpp; backing archive
-- built by build_obj_static.sh.
lean_lib «CassieObj» where
  roots := #[`CassieObj]

-- Polylines JSON loader (pure Lean.Data.Json) — gives cycle_patch a
-- runtime path (`cycle_patch --input X.json`) so externally-produced
-- stroke sets don't need codegen + Lean recompile to drive the
-- forward pipeline.
lean_lib «CassiePolylinesJson» where
  roots := #[`CassiePolylinesJson]

-- DiffCloth's original shader-emit exe — kept available because its
-- IO.FS.writeFile + slangc-invocation pattern is the reference the
-- CASSIE avbd-codegen mirrors.
lean_exe «emit_shaders» where
  root := `EmitShaders

-- Emits modules/cassie/thirdparty/avbd/avbd_step.h from the composed
-- kernels. Pipeline: Lean source → LeanSlang.Emit → Slang text →
-- slangc -target spirv + -target cpu → embedded byte arrays.
@[default_target]
lean_exe «avbd-codegen» where
  root := `CassieAvbd.Codegen
  supportInterpreter := true

-- CycleDetect parameter sweep — `lake exe cycle_sweep` runs the
-- arrangement build + cycle finder over a small (proximity, mergeEps)
-- grid against the hat fixture and prints exact-match counts. Iterates
-- the algorithm in ~few-seconds without going through the C++ runtime.
lean_exe «cycle_sweep» where
  root := `CycleSweep
  supportInterpreter := true

-- Dumps HatRawData back to JSON for the roundtrip soundness check. The
-- companion Python script reads this output and diffs it against
-- thirdparty/cassie-data/raw_data/hat.json.
lean_exe «hat_dump» where
  root := `HatDump
  supportInterpreter := true

-- Smoke test for Lean.Data.Json availability in our toolchain.
lean_exe «json_test» where
  root := `JsonTest
  supportInterpreter := true

lean_exe «json_float_test» where
  root := `JsonFloatTest
  supportInterpreter := true

lean_exe «stroke_diff» where
  root := `StrokeDiff
  supportInterpreter := true

-- Phase B.0 — Transport.lean smoke tests (Bezier eval / tangent /
-- parallel transport / crossNode). Not the Unity-derived native_decide
-- fixtures yet (those need a Unity dump first); this just confirms the
-- kernel itself doesn't blow up.
lean_exe «transport_smoke» where
  root := `TransportSmoke
  supportInterpreter := true

-- Phase B.0 step 3 — NodeAugment.augment smoke test on hat polylines.
lean_exe «node_augment_smoke» where
  root := `NodeAugmentSmoke
  supportInterpreter := true

-- Phase B.0 step 7 — compare legacy findCycles vs the new Unity-port
-- findCyclesPort on the hat polylines fixture.
lean_exe «walk_probe» where
  root := `WalkProbe
  supportInterpreter := true

-- 65-dataset arrangement parity probe — runs buildArrangementAugmented
-- on a polylines JSON and prints {nodes, edges, sharp, strokes} for diff
-- against cassie-data sketch_graph reference.
lean_exe «arrangement_probe» where
  root := `ArrangementProbe
  supportInterpreter := true

-- Smoke test the CassieObj FFI loader on agenthat2.obj (or any OBJ via
-- CLI). `bash build_obj_static.sh` must have run.
lean_exe «obj_probe» where
  root := `ObjProbe
  supportInterpreter := true
  moreLinkArgs := #[
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/obj_static/libcassie_obj.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libstdc++.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc_eh.a" ]

-- Cycle → patch end-to-end. `lake exe cycle_patch` builds the hat
-- arrangement, picks the longest cycle, walks its boundary as a flat
-- polyline, runs geogram CDT2d, then PMP implicit smoothing. Closes the
-- detect → triangulate → fair loop with real input data. Links against
-- the same PMP + geogram archives as surface_fair.
lean_exe «cycle_patch» where
  root := `CyclePatch
  supportInterpreter := true
  moreLinkArgs := #[
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/pmp_static/libcassie_pmp.a",
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/geogram_static/libcassie_geogram_ffi.a",
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/geogram_static/libcassie_geogram_half.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libstdc++.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc_eh.a",
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/pmp_static/libcassie_pmp.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libstdc++.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc.a" ]

-- Surface-fairing smoke test — `lake exe surface_fair` builds a small
-- mesh in Lean, hands it to PMP via the CassiePmp FFI, runs implicit
-- smoothing, prints counts. Links against
--   modules/cassie/lean/.lake/build/pmp_static/libcassie_pmp.a
-- which is produced by `bash modules/cassie/lean/build_pmp_static.sh`
-- — MinGW g++ compiles the PMP subset + cassie_pmp_ffi.cpp into a
-- self-contained static archive.
lean_exe «surface_fair» where
  root := `SurfaceFair
  supportInterpreter := true
  -- geogram archive temporarily out of the link while debugging the
  -- static-init segfault its global ctors trigger on MinGW + lld. The
  -- FFI wrapper uses a self-contained fan-triangulation in the meantime
  -- so the boundary→mesh→smooth pipeline still demonstrates end to end.
  moreLinkArgs := #[
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/pmp_static/libcassie_pmp.a",
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/geogram_static/libcassie_geogram_ffi.a",
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/geogram_static/libcassie_geogram_half.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libstdc++.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc_eh.a",
    -- second pass to resolve circular refs between libstdc++ <-> libgcc
    "E:/multiplayer-fabric-godot/modules/cassie/lean/.lake/build/pmp_static/libcassie_pmp.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libstdc++.a",
    "C:/Users/ernest.lee/scoop/apps/mingw/15.2.0-rt_v13-rev1/lib/gcc/x86_64-w64-mingw32/15.2.0/libgcc.a" ]
