import LeanSlang

/-!
# `CassieAvbd.CurveNewton` — Newton-Raphson chord-length reparameterize

Third editing-pipeline kernel through the Lean → Slang → CPU pipeline.
Mirrors the hand-written `reparameterize` at
`modules/cassie/src/curves/cassie_curve_fit.cpp:148-163`.

Given a cubic Bezier (a, b, c, d) and N sample points each with an
initial parameter u_i, emits the Newton-Raphson refined parameters
u'_i. Per-point step (Schneider 1990 §III):

  Q(u) = B(u; a, b, c, d)
  Q'(u) = derivative1
  Q''(u) = derivative2
  e  = Q(u) - point
  num = e · Q'
  den = Q'·Q' + e · Q''
  u'  = (|den| < ε) ? u : u - num / den

## Bindings (set 0)

  0  ConstantBuffer<NewtonParams> { float3 a, b, c, d; uint count; }
  1  StructuredBuffer<float3>     in_points  length ≥ count
  2  StructuredBuffer<float>      in_u       length ≥ count
  3  RWStructuredBuffer<float>    out_u      length ≥ count
-/

namespace CassieAvbd.CurveNewton

open LeanSlang

private def f   : SlangType := .scalar .float
private def u   : SlangType := .scalar .uint
private def f3  : SlangType := .vec .float 3

private def paramsStruct : SlangStructDecl :=
  { name    := "NewtonParams"
  , fields  :=
      [ ⟨"a",     f3, Semantic.none, none, none, .qIn⟩
      , ⟨"b",     f3, Semantic.none, none, none, .qIn⟩
      , ⟨"c",     f3, Semantic.none, none, none, .qIn⟩
      , ⟨"d",     f3, Semantic.none, none, none, .qIn⟩
      , ⟨"count", u,  Semantic.none, none, none, .qIn⟩ ] }

private def globals : List SlangBinding :=
  [ ⟨"params",    .const "NewtonParams", Semantic.none, some 0, some 0, .qIn⟩
  , ⟨"in_points", .roBuf f3,             Semantic.none, some 1, some 0, .qIn⟩
  , ⟨"in_u",      .roBuf f,              Semantic.none, some 2, some 0, .qIn⟩
  , ⟨"out_u",     .rwBuf f,              Semantic.none, some 3, some 0, .qIn⟩ ]

/-- Builds a float3 from three component expressions. The DSL has no
    operator overloads on float3, so vector arithmetic spells out each
    component (`Vector` adds/subs unroll to scalar adds/subs anyway). -/
private def mkF3 (x y z : SlangExpr) : SlangExpr :=
  .call "float3" [x, y, z]

/-- Single-thread Newton reparameterize entry. One workgroup, one
    thread; the inner for-loop iterates `params.count` points. -/
private def mainEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "main"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      let count := SlangExpr.member (.var "params") "count"
      let pA    := SlangExpr.member (.var "params") "a"
      let pB    := SlangExpr.member (.var "params") "b"
      let pC    := SlangExpr.member (.var "params") "c"
      let pD    := SlangExpr.member (.var "params") "d"
      let dot3 (x y : SlangExpr) : SlangExpr := .call "dot" [x, y]
      [ .forCount "i" (.litUint 0) count
          [ .declInit f  "u"   (.index (.var "in_u") (.var "i"))
          , .declInit f3 "pt"  (.index (.var "in_points") (.var "i"))
          , .declInit f  "omu" (.bin "-" (.litFloat 1.0) (.var "u"))
          -- Cubic Bezier evaluation B(u). Avoids De Casteljau (which
          -- exists in CurveCasteljau but emits 6 lerps + 8 writes for
          -- one split point) — for the eval-and-derivatives bundle the
          -- Bernstein-polynomial form is cheaper and more numerically
          -- straightforward.
          , .declInit f3 "ab" (mkF3
              (.bin "-" (.member pB "x") (.member pA "x"))
              (.bin "-" (.member pB "y") (.member pA "y"))
              (.bin "-" (.member pB "z") (.member pA "z")))
          , .declInit f3 "bc" (mkF3
              (.bin "-" (.member pC "x") (.member pB "x"))
              (.bin "-" (.member pC "y") (.member pB "y"))
              (.bin "-" (.member pC "z") (.member pB "z")))
          , .declInit f3 "cd" (mkF3
              (.bin "-" (.member pD "x") (.member pC "x"))
              (.bin "-" (.member pD "y") (.member pC "y"))
              (.bin "-" (.member pD "z") (.member pC "z")))
          -- Q' = 3 (omu² · ab + 2·omu·u · bc + u² · cd)
          , .declInit f "omu2"   (.bin "*" (.var "omu") (.var "omu"))
          , .declInit f "u2"     (.bin "*" (.var "u")   (.var "u"))
          , .declInit f "twoOmU" (.bin "*" (.litFloat 2.0)
                                   (.bin "*" (.var "omu") (.var "u")))
          , .declInit f3 "q1" (mkF3
              (.bin "*" (.litFloat 3.0)
                (.bin "+"
                  (.bin "+"
                    (.bin "*" (.var "omu2")   (.member (.var "ab") "x"))
                    (.bin "*" (.var "twoOmU") (.member (.var "bc") "x")))
                  (.bin "*" (.var "u2")       (.member (.var "cd") "x"))))
              (.bin "*" (.litFloat 3.0)
                (.bin "+"
                  (.bin "+"
                    (.bin "*" (.var "omu2")   (.member (.var "ab") "y"))
                    (.bin "*" (.var "twoOmU") (.member (.var "bc") "y")))
                  (.bin "*" (.var "u2")       (.member (.var "cd") "y"))))
              (.bin "*" (.litFloat 3.0)
                (.bin "+"
                  (.bin "+"
                    (.bin "*" (.var "omu2")   (.member (.var "ab") "z"))
                    (.bin "*" (.var "twoOmU") (.member (.var "bc") "z")))
                  (.bin "*" (.var "u2")       (.member (.var "cd") "z")))))
          -- Second-difference vectors for Q''.
          --   d² := c - 2b + a   (= (bc - ab))
          --   d²' := d - 2c + b  (= (cd - bc))
          , .declInit f3 "dd1" (mkF3
              (.bin "-" (.member (.var "bc") "x") (.member (.var "ab") "x"))
              (.bin "-" (.member (.var "bc") "y") (.member (.var "ab") "y"))
              (.bin "-" (.member (.var "bc") "z") (.member (.var "ab") "z")))
          , .declInit f3 "dd2" (mkF3
              (.bin "-" (.member (.var "cd") "x") (.member (.var "bc") "x"))
              (.bin "-" (.member (.var "cd") "y") (.member (.var "bc") "y"))
              (.bin "-" (.member (.var "cd") "z") (.member (.var "bc") "z")))
          -- Q'' = 6 (omu · dd1 + u · dd2)
          , .declInit f3 "q2" (mkF3
              (.bin "*" (.litFloat 6.0)
                (.bin "+"
                  (.bin "*" (.var "omu") (.member (.var "dd1") "x"))
                  (.bin "*" (.var "u")   (.member (.var "dd2") "x"))))
              (.bin "*" (.litFloat 6.0)
                (.bin "+"
                  (.bin "*" (.var "omu") (.member (.var "dd1") "y"))
                  (.bin "*" (.var "u")   (.member (.var "dd2") "y"))))
              (.bin "*" (.litFloat 6.0)
                (.bin "+"
                  (.bin "*" (.var "omu") (.member (.var "dd1") "z"))
                  (.bin "*" (.var "u")   (.member (.var "dd2") "z")))))
          -- Q(u) via lerp chain — three lerps + one lerp.
          , .declInit f3 "qab" (.call "lerp" [pA, pB, .var "u"])
          , .declInit f3 "qbc" (.call "lerp" [pB, pC, .var "u"])
          , .declInit f3 "qcd" (.call "lerp" [pC, pD, .var "u"])
          , .declInit f3 "qabc" (.call "lerp" [.var "qab", .var "qbc", .var "u"])
          , .declInit f3 "qbcd" (.call "lerp" [.var "qbc", .var "qcd", .var "u"])
          , .declInit f3 "qval" (.call "lerp" [.var "qabc", .var "qbcd", .var "u"])
          -- e = Q(u) - point
          , .declInit f3 "e" (mkF3
              (.bin "-" (.member (.var "qval") "x") (.member (.var "pt") "x"))
              (.bin "-" (.member (.var "qval") "y") (.member (.var "pt") "y"))
              (.bin "-" (.member (.var "qval") "z") (.member (.var "pt") "z")))
          , .declInit f "num" (dot3 (.var "e") (.var "q1"))
          , .declInit f "den"
              (.bin "+" (dot3 (.var "q1") (.var "q1"))
                        (dot3 (.var "e")  (.var "q2")))
          -- u' = (|den| < ε) ? u : u − num / den
          , .declInit f "abs_den" (.call "abs" [.var "den"])
          , .declInit f "u_new"
              (.ternary (.bin "<" (.var "abs_den") (.litFloat 1.0e-9))
                (.var "u")
                (.bin "-" (.var "u") (.bin "/" (.var "num") (.var "den"))))
          , .assign (.index (.var "out_u") (.var "i")) (.var "u_new") ]
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared := []
  , globals   := globals
  , functions := [mainEntry] }

example : shader.entryPointNames = ["main"] := by native_decide
example : shader.entryPoints.length = 1 := by native_decide

end CassieAvbd.CurveNewton
