import LeanSlang

/-!
# `CassieAvbd.CurveCasteljau` — De Casteljau split of a cubic Bezier

First editing-pipeline kernel ported through the Lean → Slang → CPU
pipeline (per plan `playful-marinating-harp.md` — "CASSIE editing demo
via Lean 4 — first kernel"). Mirrors the hand-written `cubic_split`
at `modules/cassie/src/curves/cassie_curve_fit.cpp:272-291`.

Given a cubic Bezier (a, b, c, d) and a split parameter `u ∈ [0, 1]`,
emits the control points of the left half (la..ld) and right half
(ra..rd). After the split: `la == a`, `rd == d`, and `ld == ra == split`
is the cut point on the curve.

  omu  = 1 - u
  ab   = omu·a   + u·b
  bc   = omu·b   + u·c
  cd   = omu·c   + u·d
  abc  = omu·ab  + u·bc
  bcd  = omu·bc  + u·cd
  split = omu·abc + u·bcd

  out[0..3] = a,     ab,  abc, split    -- la, lb, lc, ld
  out[4..7] = split, bcd, cd,  d        -- ra, rb, rc, rd

One workgroup, one thread per dispatch: this kernel is a pure
8-output function called once per cut, not a SIMT batch operation.
slangc emits a `.cpu.cpp` we invoke from the dispatch wrapper via a
single (1,1,1) group launch. `_main_0`'s overhead is negligible
relative to the surrounding cassie_curve_cut_at work.

## Bindings (set 0)

  0  ConstantBuffer<CasteljauParams> { float3 a, b, c, d; float u; }
  1  RWStructuredBuffer<float3>      out   length = 8
-/

namespace CassieAvbd.CurveCasteljau

open LeanSlang

private def f  : SlangType := .scalar .float
private def f3 : SlangType := .vec .float 3

def shader : SlangShaderModule :=
  { structs :=
      [ { name := "CasteljauParams"
        , fields :=
            [ ⟨"a", f3, Semantic.none, none, none, .qIn⟩
            , ⟨"b", f3, Semantic.none, none, none, .qIn⟩
            , ⟨"c", f3, Semantic.none, none, none, .qIn⟩
            , ⟨"d", f3, Semantic.none, none, none, .qIn⟩
            , ⟨"u", f,  Semantic.none, none, none, .qIn⟩ ] } ]
  , globals :=
      [ ⟨"params", .const "CasteljauParams", Semantic.none, some 0, some 0, .qIn⟩
      , ⟨"out",    .rwBuf f3,                Semantic.none, some 1, some 0, .qIn⟩ ]
  , functions := [{
      attrs  := [.shaderCompute, .numthreads 1 1 1]
      name   := "main"
      params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
      body   :=
        let pA   := SlangExpr.member (.var "params") "a"
        let pB   := SlangExpr.member (.var "params") "b"
        let pC   := SlangExpr.member (.var "params") "c"
        let pD   := SlangExpr.member (.var "params") "d"
        let pU   := SlangExpr.member (.var "params") "u"
        let lerp (x y t : SlangExpr) : SlangExpr :=
          .call "lerp" [x, y, t]
        [ .declInit f3 "ab"    (lerp pA pB pU)
        , .declInit f3 "bc"    (lerp pB pC pU)
        , .declInit f3 "cd"    (lerp pC pD pU)
        , .declInit f3 "abc"   (lerp (.var "ab") (.var "bc") pU)
        , .declInit f3 "bcd"   (lerp (.var "bc") (.var "cd") pU)
        , .declInit f3 "split" (lerp (.var "abc") (.var "bcd") pU)
        , .assign (.index (.var "out") (.litUint 0)) pA
        , .assign (.index (.var "out") (.litUint 1)) (.var "ab")
        , .assign (.index (.var "out") (.litUint 2)) (.var "abc")
        , .assign (.index (.var "out") (.litUint 3)) (.var "split")
        , .assign (.index (.var "out") (.litUint 4)) (.var "split")
        , .assign (.index (.var "out") (.litUint 5)) (.var "bcd")
        , .assign (.index (.var "out") (.litUint 6)) (.var "cd")
        , .assign (.index (.var "out") (.litUint 7)) pD
        ] }] }

end CassieAvbd.CurveCasteljau
