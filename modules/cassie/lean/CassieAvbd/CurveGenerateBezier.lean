import LeanSlang

/-!
# `CassieAvbd.CurveGenerateBezier` — Schneider 2×2 LSQ Bezier generator

Fourth editing-pipeline kernel. Mirrors the hand-written
`generate_bezier` at `modules/cassie/src/curves/cassie_curve_fit.cpp:75-132`.

Given N points + N chord-length parameters + two endpoint tangents,
solves the 2×2 normal equations for the inner control points α_a, α_b
(Schneider 1990 §III) and emits the four control points P0..P3.
Endpoint constraint: P0 = points[0], P3 = points[n-1]; the LSQ finds
P1 = P0 + α_a · t_a and P2 = P3 + α_b · t_b that minimize squared
chord-length error.

Falls back to (segment_length / 3)-tangent control points when the
2×2 system is singular or yields a non-positive α — the same fallback
the C++ port uses, preserving bug-for-bug behavior.

## Bindings (set 0)

  0  ConstantBuffer<GbParams>     { float3 tangent_a, tangent_b; uint count; }
  1  StructuredBuffer<float3>     in_points  length ≥ count
  2  StructuredBuffer<float>      in_u       length ≥ count
  3  RWStructuredBuffer<float3>   out_ctrl   length = 4  (P0, P1, P2, P3)
-/

namespace CassieAvbd.CurveGenerateBezier

open LeanSlang

private def f   : SlangType := .scalar .float
private def u   : SlangType := .scalar .uint
private def f3  : SlangType := .vec .float 3

private def paramsStruct : SlangStructDecl :=
  { name    := "GbParams"
  , fields  :=
      [ ⟨"tangent_a", f3, Semantic.none, none, none, .qIn⟩
      , ⟨"tangent_b", f3, Semantic.none, none, none, .qIn⟩
      , ⟨"count",     u,  Semantic.none, none, none, .qIn⟩ ] }

private def globals : List SlangBinding :=
  [ ⟨"params",    .const "GbParams", Semantic.none, some 0, some 0, .qIn⟩
  , ⟨"in_points", .roBuf f3,         Semantic.none, some 1, some 0, .qIn⟩
  , ⟨"in_u",      .roBuf f,          Semantic.none, some 2, some 0, .qIn⟩
  , ⟨"out_ctrl",  .rwBuf f3,         Semantic.none, some 3, some 0, .qIn⟩ ]

private def mkF3 (x y z : SlangExpr) : SlangExpr :=
  .call "float3" [x, y, z]

private def scale3 (s v : SlangExpr) : SlangExpr :=
  mkF3 (.bin "*" s (.member v "x"))
       (.bin "*" s (.member v "y"))
       (.bin "*" s (.member v "z"))

private def add3 (a b : SlangExpr) : SlangExpr :=
  mkF3 (.bin "+" (.member a "x") (.member b "x"))
       (.bin "+" (.member a "y") (.member b "y"))
       (.bin "+" (.member a "z") (.member b "z"))

private def sub3 (a b : SlangExpr) : SlangExpr :=
  mkF3 (.bin "-" (.member a "x") (.member b "x"))
       (.bin "-" (.member a "y") (.member b "y"))
       (.bin "-" (.member a "z") (.member b "z"))

private def mainEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "main"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      let count  := SlangExpr.member (.var "params") "count"
      let pTanA  := SlangExpr.member (.var "params") "tangent_a"
      let pTanB  := SlangExpr.member (.var "params") "tangent_b"
      let dot3 (x y : SlangExpr) : SlangExpr := .call "dot" [x, y]
      [ .declInit f3 "p0" (.index (.var "in_points") (.litUint 0))
      , .declInit f3 "p3"
          (.index (.var "in_points") (.bin "-" count (.litUint 1)))
      , .declInit f "c00" (.litFloat 0.0)
      , .declInit f "c01" (.litFloat 0.0)
      , .declInit f "c11" (.litFloat 0.0)
      , .declInit f "x0"  (.litFloat 0.0)
      , .declInit f "x1"  (.litFloat 0.0)
      , .forCount "i" (.litUint 0) count
          [ .declInit f  "ui"  (.index (.var "in_u") (.var "i"))
          , .declInit f  "omu" (.bin "-" (.litFloat 1.0) (.var "ui"))
          -- a1 = tangent_a * (3 · omu² · ui)
          , .declInit f "ka"
              (.bin "*" (.litFloat 3.0)
                (.bin "*" (.bin "*" (.var "omu") (.var "omu")) (.var "ui")))
          -- a2 = tangent_b * (3 · ui² · omu)
          , .declInit f "kb"
              (.bin "*" (.litFloat 3.0)
                (.bin "*" (.bin "*" (.var "ui") (.var "ui")) (.var "omu")))
          , .declInit f3 "a1" (scale3 (.var "ka") pTanA)
          , .declInit f3 "a2" (scale3 (.var "kb") pTanB)
          , .assign (.var "c00") (.bin "+" (.var "c00") (dot3 (.var "a1") (.var "a1")))
          , .assign (.var "c01") (.bin "+" (.var "c01") (dot3 (.var "a1") (.var "a2")))
          , .assign (.var "c11") (.bin "+" (.var "c11") (dot3 (.var "a2") (.var "a2")))
          -- baseline(ui) = (1-ui)³·p0 + 3(1-ui)²ui·p0 + 3(1-ui)ui²·p3 + ui³·p3
          -- collapses to (1-ui)²·(1+2ui)·p0 + ui²·(3-2ui)·p3
          , .declInit f "wA"
              (.bin "*" (.bin "*" (.var "omu") (.var "omu"))
                (.bin "+" (.litFloat 1.0) (.bin "*" (.litFloat 2.0) (.var "ui"))))
          , .declInit f "wB"
              (.bin "*" (.bin "*" (.var "ui") (.var "ui"))
                (.bin "-" (.litFloat 3.0) (.bin "*" (.litFloat 2.0) (.var "ui"))))
          , .declInit f3 "baseline"
              (add3 (scale3 (.var "wA") (.var "p0"))
                    (scale3 (.var "wB") (.var "p3")))
          , .declInit f3 "pi" (.index (.var "in_points") (.var "i"))
          , .declInit f3 "tmp" (sub3 (.var "pi") (.var "baseline"))
          , .assign (.var "x0") (.bin "+" (.var "x0") (dot3 (.var "a1") (.var "tmp")))
          , .assign (.var "x1") (.bin "+" (.var "x1") (dot3 (.var "a2") (.var "tmp"))) ]
      -- Cramer's rule for the 2×2 system [c00 c01; c01 c11] [α_a; α_b] = [x0; x1]
      , .declInit f "det"
          (.bin "-" (.bin "*" (.var "c00") (.var "c11"))
                    (.bin "*" (.var "c01") (.var "c01")))
      , .declInit f "det_x"
          (.bin "-" (.bin "*" (.var "c00") (.var "x1"))
                    (.bin "*" (.var "c01") (.var "x0")))
      , .declInit f "det_y"
          (.bin "-" (.bin "*" (.var "x0")  (.var "c11"))
                    (.bin "*" (.var "x1")  (.var "c01")))
      , .declInit f3 "chord" (sub3 (.var "p3") (.var "p0"))
      , .declInit f "seg_length" (.call "length" [.var "chord"])
      , .declInit f "epsilon"
          (.bin "*" (.litFloat 1.0e-5) (.var "seg_length"))
      , .declInit f "alpha_a" (.litFloat 0.0)
      , .declInit f "alpha_b" (.litFloat 0.0)
      , .declInit u "fallback"
          (.ternary (.bin "<" (.call "abs" [.var "det"]) (.litFloat 1.0e-12))
            (.litUint 1) (.litUint 0))
      , .ifNoElse (.bin "==" (.var "fallback") (.litUint 0))
          [ .assign (.var "alpha_a") (.bin "/" (.var "det_y") (.var "det"))
          , .assign (.var "alpha_b") (.bin "/" (.var "det_x") (.var "det"))
          , .ifNoElse
              (.bin "||"
                (.bin "<" (.var "alpha_a") (.var "epsilon"))
                (.bin "<" (.var "alpha_b") (.var "epsilon")))
              [ .assign (.var "fallback") (.litUint 1) ] ]
      , .declInit f3 "p1" (.var "p0")
      , .declInit f3 "p2" (.var "p3")
      , .ifThen (.bin "==" (.var "fallback") (.litUint 1))
          [ .declInit f "third"
              (.bin "/" (.var "seg_length") (.litFloat 3.0))
          , .assign (.var "p1") (add3 (.var "p0") (scale3 (.var "third") pTanA))
          , .assign (.var "p2") (add3 (.var "p3") (scale3 (.var "third") pTanB)) ]
          [ .assign (.var "p1") (add3 (.var "p0") (scale3 (.var "alpha_a") pTanA))
          , .assign (.var "p2") (add3 (.var "p3") (scale3 (.var "alpha_b") pTanB)) ]
      , .assign (.index (.var "out_ctrl") (.litUint 0)) (.var "p0")
      , .assign (.index (.var "out_ctrl") (.litUint 1)) (.var "p1")
      , .assign (.index (.var "out_ctrl") (.litUint 2)) (.var "p2")
      , .assign (.index (.var "out_ctrl") (.litUint 3)) (.var "p3")
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared := []
  , globals   := globals
  , functions := [mainEntry] }

example : shader.entryPointNames = ["main"] := by native_decide
example : shader.entryPoints.length = 1 := by native_decide

end CassieAvbd.CurveGenerateBezier
