import LeanSlang

/-!
# `CassieAvbd.CurveRdp` — Ramer-Douglas-Peucker polyline simplifier

Second editing-pipeline kernel ported through the Lean → Slang → CPU
pipeline. Mirrors the hand-written `cassie_rdp_reduce` at
`modules/cassie/src/curves/rdp_simplify.cpp`.

Given a polyline of `in_count` points and a perpendicular-distance
`tolerance`, emits a bitmask `out_keep[0..in_count)` (1 for kept, 0
for dropped) and a total `out_count[0]`. The dispatch wrapper turns
the bitmask into the existing `PackedInt32Array` of indices.

## Algorithm (iterative, stack-based)

Slang has no recursion, so the classic recursive RDP is reshaped as a
fixed-capacity stack of `(first, last)` pairs. Each loop iteration
pops a pair, scans `(first, last)` for the max-perp-distance point,
and — if that distance exceeds tolerance — marks the split and pushes
the two new sub-ranges. Bug-for-bug compatible with the C++ port: the
`split == 0` sentinel acts as "no split found" since `first == 0` is
only valid when the whole stroke is the open range (and that case
either splits at an interior point ≠ 0 or doesn't split at all).

## Bindings (set 0)

  0  ConstantBuffer<RdpParams>     { uint in_count; float tolerance; }
  1  StructuredBuffer<float3>      in_points  length ≥ in_count
  2  RWStructuredBuffer<uint>      out_keep   length ≥ in_count
  3  RWStructuredBuffer<uint>      out_count  length = 1

## Capacity

The local stack is sized to 512 entries (= 256 (first, last) pairs).
Strokes past 256 sample-points after dedup are out of scope — the
beautifier resamples upstream. Overflow is silent (extra pushes are
dropped); the bitmask still reflects whatever splits were processed
before the overflow.
-/

namespace CassieAvbd.CurveRdp

open LeanSlang

private def f   : SlangType := .scalar .float
private def u   : SlangType := .scalar .uint
private def f3  : SlangType := .vec .float 3

private def paramsStruct : SlangStructDecl :=
  { name    := "RdpParams"
  , fields  :=
      [ ⟨"in_count",  u, Semantic.none, none, none, .qIn⟩
      , ⟨"tolerance", f, Semantic.none, none, none, .qIn⟩ ] }

private def globals : List SlangBinding :=
  [ ⟨"params",    .const "RdpParams", Semantic.none, some 0, some 0, .qIn⟩
  , ⟨"in_points", .roBuf f3,          Semantic.none, some 1, some 0, .qIn⟩
  , ⟨"out_keep",  .rwBuf u,           Semantic.none, some 2, some 0, .qIn⟩
  , ⟨"out_count", .rwBuf u,           Semantic.none, some 3, some 0, .qIn⟩ ]

/-- Single-thread RDP entry. One workgroup, one thread; the
    dispatch wrapper calls it once per polyline. -/
private def mainEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "main"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      let stackCap : Nat := 512  -- 256 (first, last) pairs
      let n   := SlangExpr.member (.var "params") "in_count"
      let tol := SlangExpr.member (.var "params") "tolerance"
      [ .declareArray u "stack" stackCap
      , .declInit u "stack_top" (.litUint 0)
      , .declInit u "kept" (.litUint 0)
      -- Zero out_keep, then mark endpoints.
      , .forCount "i" (.litUint 0) n
          [ .assign (.index (.var "out_keep") (.var "i")) (.litUint 0) ]
      , .ifNoElse (.bin ">=" n (.litUint 1))
          [ .assign (.index (.var "out_keep") (.litUint 0)) (.litUint 1) ]
      , .ifNoElse (.bin ">=" n (.litUint 2))
          [ .assign (.index (.var "out_keep")
                      (.bin "-" n (.litUint 1))) (.litUint 1) ]
      -- Push the seed range (0, n - 1) if there's an interior to split.
      , .ifNoElse (.bin ">=" n (.litUint 3))
          [ .assign (.index (.var "stack") (.litUint 0)) (.litUint 0)
          , .assign (.index (.var "stack") (.litUint 1))
              (.bin "-" n (.litUint 1))
          , .assign (.var "stack_top") (.litUint 1) ]
      -- Pop-and-process loop. Each iteration consumes one pair and
      -- may push two; total pushes ≤ in_count - 2 (each split adds
      -- one new keep, capped at in_count - 2 splits). Loop terminates
      -- when the stack drains or we hit the safety cap.
      , .whileLoop (.bin ">" (.var "stack_top") (.litUint 0))
          [ .assign (.var "stack_top")
              (.bin "-" (.var "stack_top") (.litUint 1))
          , .declInit u "first"
              (.index (.var "stack")
                (.bin "*" (.var "stack_top") (.litUint 2)))
          , .declInit u "last"
              (.index (.var "stack")
                (.bin "+" (.bin "*" (.var "stack_top") (.litUint 2))
                          (.litUint 1)))
          -- Only process spans of ≥ 3 (first, interior, last).
          , .ifNoElse (.bin ">" (.bin "-" (.var "last") (.var "first"))
                                (.litUint 1))
              [ .declInit f3 "a" (.index (.var "in_points") (.var "first"))
              , .declInit f3 "b" (.index (.var "in_points") (.var "last"))
              , .declInit f3 "ab"
                  (.call "float3"
                    [ .bin "-" (.member (.var "b") "x") (.member (.var "a") "x")
                    , .bin "-" (.member (.var "b") "y") (.member (.var "a") "y")
                    , .bin "-" (.member (.var "b") "z") (.member (.var "a") "z") ])
              , .declInit f "ab_len2" (.call "dot" [.var "ab", .var "ab"])
              , .declInit f "max_dist" tol
              , .declInit u "split" (.litUint 0)
              , .forCount "j" (.bin "+" (.var "first") (.litUint 1)) (.var "last")
                  [ .declInit f3 "p" (.index (.var "in_points") (.var "j"))
                  -- ap = p - a; bp = p - b
                  , .declInit f3 "ap"
                      (.call "float3"
                        [ .bin "-" (.member (.var "p") "x") (.member (.var "a") "x")
                        , .bin "-" (.member (.var "p") "y") (.member (.var "a") "y")
                        , .bin "-" (.member (.var "p") "z") (.member (.var "a") "z") ])
                  , .declInit f3 "bp"
                      (.call "float3"
                        [ .bin "-" (.member (.var "p") "x") (.member (.var "b") "x")
                        , .bin "-" (.member (.var "p") "y") (.member (.var "b") "y")
                        , .bin "-" (.member (.var "p") "z") (.member (.var "b") "z") ])
                  -- d = |ap| if a == b (degenerate); else |cross(ap, bp)| / |ab|.
                  , .declInit f "d"
                      (.ternary (.bin "<=" (.var "ab_len2") (.litFloat 0.0))
                        (.call "length" [.var "ap"])
                        (.call "sqrt"
                          [.bin "/"
                            (.call "dot"
                              [ .call "cross" [.var "ap", .var "bp"]
                              , .call "cross" [.var "ap", .var "bp"] ])
                            (.var "ab_len2")]))
                  , .ifNoElse (.bin ">" (.var "d") (.var "max_dist"))
                      [ .assign (.var "max_dist") (.var "d")
                      , .assign (.var "split") (.var "j") ] ]
              -- The C++ port uses split != 0 as "found one"; the
              -- seed range is always (0, n-1) so split == 0 means
              -- the inner loop didn't beat the tolerance.
              , .ifNoElse (.bin "!=" (.var "split") (.litUint 0))
                  [ .assign (.index (.var "out_keep") (.var "split")) (.litUint 1)
                  -- Push (first, split) — overflow-guard against stackCap/2.
                  , .ifNoElse (.bin "<" (.var "stack_top") (.litUint (stackCap / 2 - 1)))
                      [ .assign (.index (.var "stack")
                                  (.bin "*" (.var "stack_top") (.litUint 2)))
                          (.var "first")
                      , .assign (.index (.var "stack")
                                  (.bin "+" (.bin "*" (.var "stack_top") (.litUint 2))
                                            (.litUint 1)))
                          (.var "split")
                      , .assign (.var "stack_top")
                          (.bin "+" (.var "stack_top") (.litUint 1)) ]
                  -- Push (split, last).
                  , .ifNoElse (.bin "<" (.var "stack_top") (.litUint (stackCap / 2 - 1)))
                      [ .assign (.index (.var "stack")
                                  (.bin "*" (.var "stack_top") (.litUint 2)))
                          (.var "split")
                      , .assign (.index (.var "stack")
                                  (.bin "+" (.bin "*" (.var "stack_top") (.litUint 2))
                                            (.litUint 1)))
                          (.var "last")
                      , .assign (.var "stack_top")
                          (.bin "+" (.var "stack_top") (.litUint 1)) ] ] ] ]
      -- Count keeps.
      , .forCount "i" (.litUint 0) n
          [ .ifNoElse (.bin "==" (.index (.var "out_keep") (.var "i")) (.litUint 1))
              [ .assign (.var "kept") (.bin "+" (.var "kept") (.litUint 1)) ] ]
      , .assign (.index (.var "out_count") (.litUint 0)) (.var "kept")
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared := []
  , globals   := globals
  , functions := [mainEntry] }

example : shader.entryPointNames = ["main"] := by native_decide
example : shader.entryPoints.length = 1 := by native_decide

end CassieAvbd.CurveRdp
