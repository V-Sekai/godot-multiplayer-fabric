import LeanSlang

/-!
# `CassieAvbd.CycleDetectKernel` — Slang/CPU cycle finder

GPU/CPU implementation of `CassieAvbd.CycleDetect.findCycles`, emitted
via LeanSlang → slangc → `.cpu.cpp` + `.spv`. The pure-Lean spec lives
in `CassieAvbd.CycleDetect.Walk`; this file is the runtime form.

## Binding layout (set 0)

  0  ConstantBuffer<CycleParams> {
       uint num_edges;
       uint num_nodes;
       uint max_cycle_len;
       float3 plane_n;
       uint want_ccw;        // 1 = CCW, 0 = CW
       uint exclude_oop;     // 1 = exclude out-of-plane, 0 = include
     }
  1  StructuredBuffer<float3>   node_positions   length = num_nodes
  2  StructuredBuffer<EdgeRec>  edges            length = num_edges
       struct EdgeRec { uint na, nb, src, pts_offset, pts_count; }
  3  StructuredBuffer<float3>   edge_pts         length = sum_pts
       — flat polyline samples for every edge, indexed by EdgeRec.pts_offset
  4  StructuredBuffer<uint>     node_edges_off   length = num_nodes + 1
       — CSR offsets into `node_edges_idx`
  5  StructuredBuffer<uint>     node_edges_idx   length = sum_degree
       — CSR neighbor list; node n's incident edges sit at
       [node_edges_off[n], node_edges_off[n+1])
  6  RWStructuredBuffer<uint>   out_cycle_edges  length = max_cycle_len * max_cycles
       — flat (cycle_idx, step_idx) -> edge_id, padded with 0xffffffff
  7  RWStructuredBuffer<uint>   out_cycle_lens   length = max_cycles
       — number of edges in cycle i (0 if unused)
  8  RWStructuredBuffer<uint>   out_cycle_count  length = 1
       — number of cycles written

## Threading model

`numthreads(1, 1, 1)` single-thread sequential walk over every
starting half-edge `(eid, side)`. Matches the pattern of
`CurveRdp.lean` / `CurveCasteljau.lean`. Parallel walk-per-thread is
a follow-up — single-thread first to keep the algorithm body close
to the Lean spec.

## Status

This file ships the binding layout + a stub entry that zeroes the
output count. The algorithm body lands incrementally in follow-up
commits. The intent is to wire the codegen pipeline first so any
shape change (binding layout, struct shape) is caught at lake-build
time via the codegen's gen-header diff against `thirdparty/avbd/`.
-/

namespace CassieAvbd.CycleDetectKernel

open LeanSlang

private def f  : SlangType := .scalar .float
private def u  : SlangType := .scalar .uint
private def f3 : SlangType := .vec .float 3

private def paramsStruct : SlangStructDecl :=
  { name    := "CycleParams"
  , fields  :=
      [ ⟨"num_edges",     u,  Semantic.none, none, none, .qIn⟩
      , ⟨"num_nodes",     u,  Semantic.none, none, none, .qIn⟩
      , ⟨"max_cycle_len", u,  Semantic.none, none, none, .qIn⟩
      , ⟨"plane_n",       f3, Semantic.none, none, none, .qIn⟩
      , ⟨"want_ccw",      u,  Semantic.none, none, none, .qIn⟩
      , ⟨"exclude_oop",   u,  Semantic.none, none, none, .qIn⟩ ] }

private def edgeRecStruct : SlangStructDecl :=
  { name    := "EdgeRec"
  , fields  :=
      [ ⟨"na",         u, Semantic.none, none, none, .qIn⟩
      , ⟨"nb",         u, Semantic.none, none, none, .qIn⟩
      , ⟨"src",        u, Semantic.none, none, none, .qIn⟩
      , ⟨"pts_offset", u, Semantic.none, none, none, .qIn⟩
      , ⟨"pts_count",  u, Semantic.none, none, none, .qIn⟩ ] }

private def globals : List SlangBinding :=
  [ ⟨"params",          .const "CycleParams", Semantic.none, some 0, some 0, .qIn⟩
  , ⟨"node_positions",  .roBuf f3,            Semantic.none, some 1, some 0, .qIn⟩
  , ⟨"edges",           .roBuf (.const "EdgeRec"), Semantic.none, some 2, some 0, .qIn⟩
  , ⟨"edge_pts",        .roBuf f3,            Semantic.none, some 3, some 0, .qIn⟩
  , ⟨"node_edges_off",  .roBuf u,             Semantic.none, some 4, some 0, .qIn⟩
  , ⟨"node_edges_idx",  .roBuf u,             Semantic.none, some 5, some 0, .qIn⟩
  , ⟨"out_cycle_edges", .rwBuf u,             Semantic.none, some 6, some 0, .qIn⟩
  , ⟨"out_cycle_lens",  .rwBuf u,             Semantic.none, some 7, some 0, .qIn⟩
  , ⟨"out_cycle_count", .rwBuf u,             Semantic.none, some 8, some 0, .qIn⟩ ]

/-- Stub entry point. Zeroes `out_cycle_count` and returns. The
    algorithm body — walk every (eid, side) seed, find cycles, write to
    out_cycle_edges/lens — lands in follow-up commits. -/
private def mainEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "main"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .assign (.index (.var "out_cycle_count") (.litUint 0)) (.litUint 0) ]
  , ret    := .scalar .void }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct, edgeRecStruct]
  , globals
  , functions := [mainEntry] }

end CassieAvbd.CycleDetectKernel
