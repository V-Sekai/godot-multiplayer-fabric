import LeanSlang

/-!
# `CassieAvbd.CgUbershader3Serial` — CPU sibling of CgUbershader3

slangc's `-target cpp` backend can't lower `GroupMemoryBarrierWithGroupSync`
(error E36107). This module is a sibling of `CgUbershader3.lean` with
the three reduction entries (`dot_p_ap`, `dot_r_z`, `check_residual`)
replaced by `[numthreads(1, 1, 1)]` serial folds. The other 8 entries
are byte-equal to the parallel module.

Mirrors how `Cloth.SlangCodegen.DotReduceSerial` complements
`Cloth.SlangCodegen.DotReduce`. Same df32 EFTs, same final numerical
result — just sequential.

Registered in `Codegen.lean` as the `cpuShader` for the `cg_pcg3`
ubershader so slangc emits both `cg_pcg3.<entry>.spv` (parallel, GPU)
and `cg_pcg3.<entry>.cpu.cpp` (this module, CPU).
-/

namespace CassieAvbd.CgUbershader3Serial

open LeanSlang

private def floatTy  : SlangType := .scalar .float
private def uintTy   : SlangType := .scalar .uint
private def float3Ty : SlangType := .vec .float 3

private def fIn  (name : String) : SlangBinding :=
  ⟨name, floatTy, Semantic.none, none, none, .qIn⟩
private def fOut (name : String) : SlangBinding :=
  ⟨name, floatTy, Semantic.none, none, none, .qOut⟩

-- df32 EFT helpers — duplicated from CgUbershader3 (LeanSlang emits
-- per-module helpers inline, no cross-module import for free fns).

private def two_sum : SlangFunctionDecl :=
  { attrs   := []
  , retType := .named "void"
  , name    := "two_sum"
  , params  := [fIn "a", fIn "b", fOut "hi", fOut "lo"]
  , body    :=
      [ .declInit floatTy "h"    (.bin "+" (.var "a") (.var "b"))
      , .declInit floatTy "bb"   (.bin "-" (.var "h") (.var "a"))
      , .declInit floatTy "ah"   (.bin "-" (.var "h") (.var "bb"))
      , .declInit floatTy "lo_a" (.bin "-" (.var "a") (.var "ah"))
      , .declInit floatTy "lo_b" (.bin "-" (.var "b") (.var "bb"))
      , .assign (.var "hi") (.var "h")
      , .assign (.var "lo") (.bin "+" (.var "lo_a") (.var "lo_b"))
      , .ret none ] }

private def quick_two_sum : SlangFunctionDecl :=
  { attrs   := []
  , retType := .named "void"
  , name    := "quick_two_sum"
  , params  := [fIn "a", fIn "b", fOut "hi", fOut "lo"]
  , body    :=
      [ .declInit floatTy "h" (.bin "+" (.var "a") (.var "b"))
      , .declInit floatTy "t" (.bin "-" (.var "h") (.var "a"))
      , .assign (.var "hi") (.var "h")
      , .assign (.var "lo") (.bin "-" (.var "b") (.var "t"))
      , .ret none ] }

private def two_prod : SlangFunctionDecl :=
  { attrs   := []
  , retType := .named "void"
  , name    := "two_prod"
  , params  := [fIn "a", fIn "b", fOut "hi", fOut "lo"]
  , body    :=
      [ .declInit floatTy "h" (.bin "*" (.var "a") (.var "b"))
      , .assign (.var "hi") (.var "h")
      , .assign (.var "lo")
          (.call "fma" [.var "a", .var "b", .un "-" (.var "h")])
      , .ret none ] }

private def df_add : SlangFunctionDecl :=
  { attrs   := []
  , retType := .named "void"
  , name    := "df_add"
  , params  :=
      [ fIn "x_hi", fIn "x_lo", fIn "y_hi", fIn "y_lo"
      , fOut "z_hi", fOut "z_lo" ]
  , body    :=
      [ .declare floatTy "sh" none
      , .declare floatTy "sl" none
      , .expr (.call "two_sum"
          [.var "x_hi", .var "y_hi", .var "sh", .var "sl"])
      , .declInit floatTy "xy_lo" (.bin "+" (.var "x_lo") (.var "y_lo"))
      , .declInit floatTy "sl2"   (.bin "+" (.var "sl")  (.var "xy_lo"))
      , .expr (.call "quick_two_sum"
          [.var "sh", .var "sl2", .var "z_hi", .var "z_lo"])
      , .ret none ] }

-- Identical bindings to CgUbershader3.

private def paramsStruct : SlangStructDecl :=
  { name := "CgPcg3Params"
  , fields :=
      [ ⟨"rows", uintTy, Semantic.none, none, none, .qIn⟩ ] }

private def globals : List SlangBinding :=
  [ ⟨"params",   .const "CgPcg3Params",      Semantic.none, some 0,  some 0, .qIn⟩
  , ⟨"rowPtr",   .roBuf (.scalar .int),      Semantic.none, some 1,  some 0, .qIn⟩
  , ⟨"colIdx",   .roBuf (.scalar .int),      Semantic.none, some 2,  some 0, .qIn⟩
  , ⟨"values",   .roBuf floatTy,             Semantic.none, some 3,  some 0, .qIn⟩
  , ⟨"diag_inv", .roBuf floatTy,             Semantic.none, some 4,  some 0, .qIn⟩
  , ⟨"b",        .roBuf float3Ty,            Semantic.none, some 5,  some 0, .qIn⟩
  , ⟨"x",        .rwBuf float3Ty,            Semantic.none, some 6,  some 0, .qIn⟩
  , ⟨"r",        .rwBuf float3Ty,            Semantic.none, some 7,  some 0, .qIn⟩
  , ⟨"z",        .rwBuf float3Ty,            Semantic.none, some 8,  some 0, .qIn⟩
  , ⟨"p",        .rwBuf float3Ty,            Semantic.none, some 9,  some 0, .qIn⟩
  , ⟨"Ap",       .rwBuf float3Ty,            Semantic.none, some 10, some 0, .qIn⟩
  , ⟨"scalars",  .rwBuf floatTy,             Semantic.none, some 11, some 0, .qIn⟩ ]

-- The 8 non-reduction entries are byte-equal to CgUbershader3. Inline
-- them rather than import-private — keeps each ubershader module
-- self-contained (Codegen reads `shader.entryPointNames` which only
-- looks at this module's functions list).

private def initEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "init"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit uintTy "rs"
          (.call "uint" [.index (.var "rowPtr") (.var "i")])
      , .declInit uintTy "re"
          (.call "uint" [.index (.var "rowPtr") (.bin "+" (.var "i") (.litUint 1))])
      , .declInit floatTy "s_x" (.litFloat 0.0)
      , .declInit floatTy "s_y" (.litFloat 0.0)
      , .declInit floatTy "s_z" (.litFloat 0.0)
      , .forCount "k" (.var "rs") (.var "re")
          [ .declInit floatTy "v" (.index (.var "values") (.var "k"))
          , .declInit float3Ty "xj"
              (.index (.var "x")
                (.call "uint" [.index (.var "colIdx") (.var "k")]))
          , .assign (.var "s_x")
              (.call "fma" [.var "v", .member (.var "xj") "x", .var "s_x"])
          , .assign (.var "s_y")
              (.call "fma" [.var "v", .member (.var "xj") "y", .var "s_y"])
          , .assign (.var "s_z")
              (.call "fma" [.var "v", .member (.var "xj") "z", .var "s_z"]) ]
      , .declInit float3Ty "bi" (.index (.var "b") (.var "i"))
      , .declInit float3Ty "ri"
          (.call "float3"
            [ .bin "-" (.member (.var "bi") "x") (.var "s_x")
            , .bin "-" (.member (.var "bi") "y") (.var "s_y")
            , .bin "-" (.member (.var "bi") "z") (.var "s_z") ])
      , .declInit floatTy "d" (.index (.var "diag_inv") (.var "i"))
      , .declInit float3Ty "zi"
          (.call "float3"
            [ .bin "*" (.var "d") (.member (.var "ri") "x")
            , .bin "*" (.var "d") (.member (.var "ri") "y")
            , .bin "*" (.var "d") (.member (.var "ri") "z") ])
      , .assign (.index (.var "r") (.var "i")) (.var "ri")
      , .assign (.index (.var "z") (.var "i")) (.var "zi")
      , .assign (.index (.var "p") (.var "i")) (.var "zi")
      , .ret none ] }

private def spmvPToApEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "spmv_p_to_ap"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit uintTy "rs"
          (.call "uint" [.index (.var "rowPtr") (.var "i")])
      , .declInit uintTy "re"
          (.call "uint" [.index (.var "rowPtr") (.bin "+" (.var "i") (.litUint 1))])
      , .declInit floatTy "s_x" (.litFloat 0.0)
      , .declInit floatTy "s_y" (.litFloat 0.0)
      , .declInit floatTy "s_z" (.litFloat 0.0)
      , .forCount "k" (.var "rs") (.var "re")
          [ .declInit floatTy "v" (.index (.var "values") (.var "k"))
          , .declInit float3Ty "pj"
              (.index (.var "p")
                (.call "uint" [.index (.var "colIdx") (.var "k")]))
          , .assign (.var "s_x")
              (.call "fma" [.var "v", .member (.var "pj") "x", .var "s_x"])
          , .assign (.var "s_y")
              (.call "fma" [.var "v", .member (.var "pj") "y", .var "s_y"])
          , .assign (.var "s_z")
              (.call "fma" [.var "v", .member (.var "pj") "z", .var "s_z"]) ]
      , .assign (.index (.var "Ap") (.var "i"))
          (.call "float3" [.var "s_x", .var "s_y", .var "s_z"])
      , .ret none ] }

private def alphaUpdateEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "alpha_update"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit floatTy "rz"
          (.bin "+" (.index (.var "scalars") (.litUint 0))
                    (.index (.var "scalars") (.litUint 1)))
      , .declInit floatTy "pAp"
          (.bin "+" (.index (.var "scalars") (.litUint 2))
                    (.index (.var "scalars") (.litUint 3)))
      , .declInit floatTy "alpha"
          (.ternary (.bin ">" (.var "pAp") (.litFloat 0.0))
                    (.bin "/" (.var "rz") (.var "pAp"))
                    (.litFloat 0.0))
      , .assign (.index (.var "scalars") (.litUint 4)) (.var "alpha")
      , .assign (.index (.var "scalars") (.litUint 5))
          (.un "-" (.var "alpha"))
      , .ret none ] }

private def xAxpyPEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "x_axpy_p"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit floatTy "a" (.index (.var "scalars") (.litUint 4))
      , .declInit float3Ty "pi" (.index (.var "p") (.var "i"))
      , .declInit float3Ty "xi" (.index (.var "x") (.var "i"))
      , .assign (.index (.var "x") (.var "i"))
          (.call "float3"
            [ .call "fma" [.var "a", .member (.var "pi") "x", .member (.var "xi") "x"]
            , .call "fma" [.var "a", .member (.var "pi") "y", .member (.var "xi") "y"]
            , .call "fma" [.var "a", .member (.var "pi") "z", .member (.var "xi") "z"] ])
      , .ret none ] }

private def rAxpyNegApEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "r_axpy_neg_ap"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit floatTy "na" (.index (.var "scalars") (.litUint 5))
      , .declInit float3Ty "Api" (.index (.var "Ap") (.var "i"))
      , .declInit float3Ty "ri"  (.index (.var "r")  (.var "i"))
      , .assign (.index (.var "r") (.var "i"))
          (.call "float3"
            [ .call "fma" [.var "na", .member (.var "Api") "x", .member (.var "ri") "x"]
            , .call "fma" [.var "na", .member (.var "Api") "y", .member (.var "ri") "y"]
            , .call "fma" [.var "na", .member (.var "Api") "z", .member (.var "ri") "z"] ])
      , .ret none ] }

private def jacobiZEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "jacobi_z"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit floatTy "d" (.index (.var "diag_inv") (.var "i"))
      , .declInit float3Ty "ri" (.index (.var "r") (.var "i"))
      , .assign (.index (.var "z") (.var "i"))
          (.call "float3"
            [ .bin "*" (.var "d") (.member (.var "ri") "x")
            , .bin "*" (.var "d") (.member (.var "ri") "y")
            , .bin "*" (.var "d") (.member (.var "ri") "z") ])
      , .ret none ] }

private def betaUpdateEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "beta_update"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit floatTy "rz_old"
          (.bin "+" (.index (.var "scalars") (.litUint 0))
                    (.index (.var "scalars") (.litUint 1)))
      , .declInit floatTy "rz_new_hi" (.index (.var "scalars") (.litUint 6))
      , .declInit floatTy "rz_new_lo" (.index (.var "scalars") (.litUint 7))
      , .declInit floatTy "rz_new"
          (.bin "+" (.var "rz_new_hi") (.var "rz_new_lo"))
      , .declInit floatTy "beta"
          (.ternary (.bin ">" (.var "rz_old") (.litFloat 0.0))
                    (.bin "/" (.var "rz_new") (.var "rz_old"))
                    (.litFloat 0.0))
      , .assign (.index (.var "scalars") (.litUint 6)) (.var "beta")
      , .assign (.index (.var "scalars") (.litUint 0)) (.var "rz_new_hi")
      , .assign (.index (.var "scalars") (.litUint 1)) (.var "rz_new_lo")
      , .ret none ] }

private def pUpdateEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "p_update"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit floatTy "b" (.index (.var "scalars") (.litUint 6))
      , .declInit float3Ty "zi" (.index (.var "z") (.var "i"))
      , .declInit float3Ty "pi" (.index (.var "p") (.var "i"))
      , .assign (.index (.var "p") (.var "i"))
          (.call "float3"
            [ .call "fma" [.var "b", .member (.var "pi") "x", .member (.var "zi") "x"]
            , .call "fma" [.var "b", .member (.var "pi") "y", .member (.var "zi") "y"]
            , .call "fma" [.var "b", .member (.var "pi") "z", .member (.var "zi") "z"] ])
      , .ret none ] }

/-- Serial single-thread df32 dot-reduce over float3 buffers. One
    thread, plain for-loop, no groupshared, no barriers — what
    slangc's CPU backend accepts. Produces the same df32 result as
    the parallel `dot_p_ap` / `dot_r_z` in CgUbershader3, just
    sequentially folded. -/
private def buildFloat3DotReduceSerial (name : String)
    (inputA inputB : String) (outHiIdx : Nat) : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := name
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit floatTy "acc_hi" (.litFloat 0.0)
      , .declInit floatTy "acc_lo" (.litFloat 0.0)
      , .forCount "i" (.litUint 0) (.member (.var "params") "rows")
          [ .declInit float3Ty "ai" (.index (.var inputA) (.var "i"))
          , .declInit float3Ty "bi" (.index (.var inputB) (.var "i"))
          , .declare floatTy "px_hi" none
          , .declare floatTy "px_lo" none
          , .expr (.call "two_prod"
              [ .member (.var "ai") "x", .member (.var "bi") "x"
              , .var "px_hi", .var "px_lo" ])
          , .declare floatTy "py_hi" none
          , .declare floatTy "py_lo" none
          , .expr (.call "two_prod"
              [ .member (.var "ai") "y", .member (.var "bi") "y"
              , .var "py_hi", .var "py_lo" ])
          , .declare floatTy "pz_hi" none
          , .declare floatTy "pz_lo" none
          , .expr (.call "two_prod"
              [ .member (.var "ai") "z", .member (.var "bi") "z"
              , .var "pz_hi", .var "pz_lo" ])
          , .declare floatTy "t_hi" none
          , .declare floatTy "t_lo" none
          , .expr (.call "df_add"
              [ .var "px_hi", .var "px_lo", .var "py_hi", .var "py_lo"
              , .var "t_hi", .var "t_lo" ])
          , .declare floatTy "p_hi" none
          , .declare floatTy "p_lo" none
          , .expr (.call "df_add"
              [ .var "t_hi", .var "t_lo", .var "pz_hi", .var "pz_lo"
              , .var "p_hi", .var "p_lo" ])
          , .declare floatTy "new_hi" none
          , .declare floatTy "new_lo" none
          , .expr (.call "df_add"
              [ .var "acc_hi", .var "acc_lo", .var "p_hi", .var "p_lo"
              , .var "new_hi", .var "new_lo" ])
          , .assign (.var "acc_hi") (.var "new_hi")
          , .assign (.var "acc_lo") (.var "new_lo") ]
      , .assign (.index (.var "scalars") (.litUint outHiIdx))     (.var "acc_hi")
      , .assign (.index (.var "scalars") (.litUint (outHiIdx + 1))) (.var "acc_lo")
      , .ret none ] }

private def dotPApEntry : SlangFunctionDecl :=
  buildFloat3DotReduceSerial "dot_p_ap" "p" "Ap" 2

private def dotRZEntry : SlangFunctionDecl :=
  buildFloat3DotReduceSerial "dot_r_z" "r" "z" 6

/-- Serial single-thread df32 reduce of dot(r,r) → scalars[9]. -/
private def checkResidualEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "check_residual"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit floatTy "acc_hi" (.litFloat 0.0)
      , .declInit floatTy "acc_lo" (.litFloat 0.0)
      , .forCount "i" (.litUint 0) (.member (.var "params") "rows")
          [ .declInit float3Ty "ri" (.index (.var "r") (.var "i"))
          , .declare floatTy "px_hi" none
          , .declare floatTy "px_lo" none
          , .expr (.call "two_prod"
              [ .member (.var "ri") "x", .member (.var "ri") "x"
              , .var "px_hi", .var "px_lo" ])
          , .declare floatTy "py_hi" none
          , .declare floatTy "py_lo" none
          , .expr (.call "two_prod"
              [ .member (.var "ri") "y", .member (.var "ri") "y"
              , .var "py_hi", .var "py_lo" ])
          , .declare floatTy "pz_hi" none
          , .declare floatTy "pz_lo" none
          , .expr (.call "two_prod"
              [ .member (.var "ri") "z", .member (.var "ri") "z"
              , .var "pz_hi", .var "pz_lo" ])
          , .declare floatTy "t_hi" none
          , .declare floatTy "t_lo" none
          , .expr (.call "df_add"
              [ .var "px_hi", .var "px_lo", .var "py_hi", .var "py_lo"
              , .var "t_hi", .var "t_lo" ])
          , .declare floatTy "p_hi" none
          , .declare floatTy "p_lo" none
          , .expr (.call "df_add"
              [ .var "t_hi", .var "t_lo", .var "pz_hi", .var "pz_lo"
              , .var "p_hi", .var "p_lo" ])
          , .declare floatTy "new_hi" none
          , .declare floatTy "new_lo" none
          , .expr (.call "df_add"
              [ .var "acc_hi", .var "acc_lo", .var "p_hi", .var "p_lo"
              , .var "new_hi", .var "new_lo" ])
          , .assign (.var "acc_hi") (.var "new_hi")
          , .assign (.var "acc_lo") (.var "new_lo") ]
      , .declInit floatTy "rr" (.bin "+" (.var "acc_hi") (.var "acc_lo"))
      , .assign (.index (.var "scalars") (.litUint 9)) (.var "rr")
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared := []  -- no shared memory, no barriers — CPU-target safe
  , globals   := globals
  , functions :=
      [ two_sum, quick_two_sum, two_prod, df_add
      , initEntry
      , spmvPToApEntry
      , dotPApEntry
      , alphaUpdateEntry
      , xAxpyPEntry
      , rAxpyNegApEntry
      , jacobiZEntry
      , dotRZEntry
      , betaUpdateEntry
      , pUpdateEntry
      , checkResidualEntry ] }

example : shader.entryPointNames =
    [ "init", "spmv_p_to_ap", "dot_p_ap", "alpha_update", "x_axpy_p"
    , "r_axpy_neg_ap", "jacobi_z", "dot_r_z", "beta_update"
    , "p_update", "check_residual" ] := by native_decide
example : shader.entryPoints.length = 11 := by native_decide

end CassieAvbd.CgUbershader3Serial
