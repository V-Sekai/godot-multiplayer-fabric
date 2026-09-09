import LeanSlang

/-!
# `CassieAvbd.CgUbershader3` — Jacobi-PCG ubershader, float3 variant

Floats3-per-vertex variant of `CgUbershader.lean`. Same 11 entry points,
same constant-work design, same binding layout — only `b`, `x`, `r`,
`z`, `p`, `Ap` change from `RWStructuredBuffer<float>` to
`RWStructuredBuffer<float3>`. `values` and `diag_inv` stay scalar
(cotangent Laplacian off-diagonals are scalar; AVBD future work
upgrades values to a `float3x3` per-edge matrix).

The three axes of the vector solve are the three components of the
solve, not three sequential solves. This gives natural multi-RHS for
the harmonic deform: today's 3 sequential scalar PCG calls collapse
to one float3 PCG call. Combined with the MAS preconditioner port
(`MasPreconditioner.lean`), this is the path to fit the Quest 3
90 Hz budget — see plan `playful-marinating-harp.md`.

## Status

Phase B scaffold: only `jacobi_z` initially. Validates the float3
DSL pattern end-to-end (codegen → slangc → SPIR-V) before the other
10 entries land.

Per the AttachmentForceAl reference (`Cloth/SlangCodegen/AttachmentForceAl.lean`),
vector arithmetic in the DSL is expressed component-wise via
`.call "float3" [...x, ...y, ...z]`. slangc's SPIR-V emit fuses
the three scalar ops back into a single float3 op at compile time,
so the verbosity is in the DSL, not the runtime cost.

Initial bindings declared but several entries left unimplemented
until the float3 axpy / spmv / dot patterns are validated on this
single jacobi_z probe.
-/

namespace CassieAvbd.CgUbershader3

open LeanSlang

private def floatTy  : SlangType := .scalar .float
private def uintTy   : SlangType := .scalar .uint
private def float3Ty : SlangType := .vec .float 3

private def fIn  (name : String) : SlangBinding :=
  ⟨name, floatTy, Semantic.none, none, none, .qIn⟩
private def fOut (name : String) : SlangBinding :=
  ⟨name, floatTy, Semantic.none, none, none, .qOut⟩

-- df32 error-free transform helpers shared by all three reductions.
-- Same form as CgUbershader's — duplicated rather than imported because
-- LeanSlang emits each module's helpers inline.

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

/-- One thread per row: computes the initial residual r = b - A·x and
    sets z = M⁻¹·r, p = z. float3 form: each row block in A is λ_ij · I
    so the row sum becomes Σ values[k] * x[colIdx[k]] (scalar × float3
    fma3 per nnz). diag_inv is scalar; Jacobi step is scalar × float3. -/
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
      -- float3 accumulator: per-component scalars for the inner fma loop.
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
      -- ri = b - A·x  (component-wise subtract)
      , .declInit float3Ty "ri"
          (.call "float3"
            [ .bin "-" (.member (.var "bi") "x") (.var "s_x")
            , .bin "-" (.member (.var "bi") "y") (.var "s_y")
            , .bin "-" (.member (.var "bi") "z") (.var "s_z") ])
      , .declInit floatTy "d" (.index (.var "diag_inv") (.var "i"))
      -- zi = d · ri  (scalar × float3)
      , .declInit float3Ty "zi"
          (.call "float3"
            [ .bin "*" (.var "d") (.member (.var "ri") "x")
            , .bin "*" (.var "d") (.member (.var "ri") "y")
            , .bin "*" (.var "d") (.member (.var "ri") "z") ])
      , .assign (.index (.var "r") (.var "i")) (.var "ri")
      , .assign (.index (.var "z") (.var "i")) (.var "zi")
      , .assign (.index (.var "p") (.var "i")) (.var "zi")
      , .ret none ] }

/-- One thread per row: Ap = A · p. float3 row sum; scalar values per
    nnz multiply the float3 p[colIdx[k]] component-wise via fma. -/
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

/-- x[i] += alpha · p[i]. scalar alpha (from scalars[4]) times float3 p. -/
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

/-- r[i] -= alpha · Ap[i]. Reads -alpha from scalars[5] so fma multiplies
    the negative directly — saves one negation per thread. -/
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

/-- Single-thread entry. Reads df32 rz pair from scalars[0..1] and
    df32 pAp pair from scalars[2..3]; writes alpha = rz / pAp to
    scalars[4] and -alpha to scalars[5]. Identical to CgUbershader's
    scalar version — the dot products feeding into rz/pAp are scalar
    (float3·float3 → scalar), so alpha is scalar arithmetic. The
    ternary guards against rz/0 once CG converges past pAp=0 under
    the constant-work design. -/
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

/-- Single-thread entry. Reads df32 rz_old from scalars[0..1] and df32
    rz_new from scalars[6..7]; writes beta = rz_new / rz_old to
    scalars[6] and rolls the rz_new pair into scalars[0..1] for the
    next iter. Identical to CgUbershader's scalar version. -/
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

/-- z[i] = diag_inv[i] * r[i]. Jacobi preconditioner step, float3 form.
    diag_inv is scalar per row (cotangent Laplacian's diagonal is
    scalar; for AVBD future this becomes float3x3 per row). -/
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

/-- Build a single-workgroup df32 dot-reduce entry over float3 buffers.
    Computes Σ dot(inputA[i], inputB[i]) and writes the (hi, lo) pair
    to scalars[outHiIdx..outHiIdx+1]. Workgroup width 256; each thread
    strides through the full vector.

    Per-iter inner work: 3 two_prods (component products) + 2 df_adds
    (chain into a single (p_hi, p_lo) df32 pair) + 1 df_add to
    accumulate into (acc_hi, acc_lo). Total: 3 EFT prods + 3 df adds
    per iter vs scalar reduce's 1 prod + 1 add. The extra cost is the
    real per-iter overhead of multi-RHS via float3 inside the reduce. -/
private def buildFloat3DotReduceEntry (name : String) (inputA inputB : String)
    (outHiIdx : Nat) : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := name
  , params := [⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "t"      (.member (.var "tid") "x")
      , .declInit uintTy "stride" (.litUint 256)
      , .declInit floatTy "acc_hi" (.litFloat 0.0)
      , .declInit floatTy "acc_lo" (.litFloat 0.0)
      , .declInit uintTy "i" (.var "t")
      , .whileLoop (.bin "<" (.var "i") (.member (.var "params") "rows"))
          [ .declInit float3Ty "ai" (.index (.var inputA) (.var "i"))
          , .declInit float3Ty "bi" (.index (.var inputB) (.var "i"))
          -- Three component products → three df32 pairs.
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
          -- Chain px + py → t, then t + pz → p (df32 pair).
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
          -- Accumulate into the per-thread df32 acc.
          , .declare floatTy "new_hi" none
          , .declare floatTy "new_lo" none
          , .expr (.call "df_add"
              [ .var "acc_hi", .var "acc_lo", .var "p_hi", .var "p_lo"
              , .var "new_hi", .var "new_lo" ])
          , .assign (.var "acc_hi") (.var "new_hi")
          , .assign (.var "acc_lo") (.var "new_lo")
          , .assign (.var "i") (.bin "+" (.var "i") (.var "stride")) ]
      -- Tree reduce across the workgroup via shared memory.
      , .assign (.index (.var "s_hi") (.var "t")) (.var "acc_hi")
      , .assign (.index (.var "s_lo") (.var "t")) (.var "acc_lo")
      , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
      , .declInit uintTy "step" (.litUint 128)
      , .whileLoop (.bin ">" (.var "step") (.litUint 0))
          [ .ifNoElse (.bin "<" (.var "t") (.var "step"))
              [ .declare floatTy "new_hi" none
              , .declare floatTy "new_lo" none
              , .expr (.call "df_add"
                  [ .index (.var "s_hi") (.var "t")
                  , .index (.var "s_lo") (.var "t")
                  , .index (.var "s_hi") (.bin "+" (.var "t") (.var "step"))
                  , .index (.var "s_lo") (.bin "+" (.var "t") (.var "step"))
                  , .var "new_hi", .var "new_lo" ])
              , .assign (.index (.var "s_hi") (.var "t")) (.var "new_hi")
              , .assign (.index (.var "s_lo") (.var "t")) (.var "new_lo") ]
          , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
          , .assign (.var "step") (.bin ">>" (.var "step") (.litUint 1)) ]
      , .ifNoElse (.bin "==" (.var "t") (.litUint 0))
          [ .assign (.index (.var "scalars") (.litUint outHiIdx))
              (.index (.var "s_hi") (.litUint 0))
          , .assign (.index (.var "scalars") (.litUint (outHiIdx + 1)))
              (.index (.var "s_lo") (.litUint 0)) ]
      , .ret none ] }

/-- p · Ap → scalars[2..3] (the df32 pair alpha_update consumes). -/
private def dotPApEntry : SlangFunctionDecl :=
  buildFloat3DotReduceEntry "dot_p_ap" "p" "Ap" 2

/-- r · z → scalars[6..7] (the df32 pair beta_update consumes as the
    next-iter rz). -/
private def dotRZEntry : SlangFunctionDecl :=
  buildFloat3DotReduceEntry "dot_r_z" "r" "z" 6

/-- Single-workgroup df32 reduce of dot(r,r), write the collapsed fp32
    result to scalars[9]. Dispatched ONCE at end of solve_sparse_gpu
    for caller observability; does not influence the iter loop under
    the constant-work design. -/
private def checkResidualEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "check_residual"
  , params := [⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "t"      (.member (.var "tid") "x")
      , .declInit uintTy "stride" (.litUint 256)
      , .declInit floatTy "acc_hi" (.litFloat 0.0)
      , .declInit floatTy "acc_lo" (.litFloat 0.0)
      , .declInit uintTy "i" (.var "t")
      , .whileLoop (.bin "<" (.var "i") (.member (.var "params") "rows"))
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
          , .assign (.var "acc_lo") (.var "new_lo")
          , .assign (.var "i") (.bin "+" (.var "i") (.var "stride")) ]
      , .assign (.index (.var "s_hi") (.var "t")) (.var "acc_hi")
      , .assign (.index (.var "s_lo") (.var "t")) (.var "acc_lo")
      , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
      , .declInit uintTy "step" (.litUint 128)
      , .whileLoop (.bin ">" (.var "step") (.litUint 0))
          [ .ifNoElse (.bin "<" (.var "t") (.var "step"))
              [ .declare floatTy "new_hi" none
              , .declare floatTy "new_lo" none
              , .expr (.call "df_add"
                  [ .index (.var "s_hi") (.var "t")
                  , .index (.var "s_lo") (.var "t")
                  , .index (.var "s_hi") (.bin "+" (.var "t") (.var "step"))
                  , .index (.var "s_lo") (.bin "+" (.var "t") (.var "step"))
                  , .var "new_hi", .var "new_lo" ])
              , .assign (.index (.var "s_hi") (.var "t")) (.var "new_hi")
              , .assign (.index (.var "s_lo") (.var "t")) (.var "new_lo") ]
          , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
          , .assign (.var "step") (.bin ">>" (.var "step") (.litUint 1)) ]
      , .ifNoElse (.bin "==" (.var "t") (.litUint 0))
          [ .declInit floatTy "rr"
              (.bin "+" (.index (.var "s_hi") (.litUint 0))
                        (.index (.var "s_lo") (.litUint 0)))
          , .assign (.index (.var "scalars") (.litUint 9)) (.var "rr") ]
      , .ret none ] }

/-- p[i] = z[i] + beta · p[i]. Reads beta from scalars[6]. -/
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

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared :=
      -- Shared by dot_p_ap, dot_r_z, and check_residual — three
      -- reductions in separate dispatches so reuse is safe.
      [ { name := "s_hi", elemType := floatTy, dims := [256] }
      , { name := "s_lo", elemType := floatTy, dims := [256] } ]
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

end CassieAvbd.CgUbershader3
