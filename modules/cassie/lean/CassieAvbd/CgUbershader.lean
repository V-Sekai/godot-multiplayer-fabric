import LeanSlang

/-!
# `CassieAvbd.CgUbershader` — Jacobi-PCG ubershader

A Lean module that emits one Slang source with 10 `[shader("compute")]`
entry points covering the preconditioned-CG iteration for Ax = b
(A SPD, M = diag(A)). Every entry references the same set of bindings
(0..11), so the C++ dispatch builds the uniform set in the bind
pass and reuses it for every entry-point dispatch — no
uniform_set_create per call.

GPU-only path for the harmonic deform. The CPU path keeps using
cassie_pcg::solve_sparse, which composes the existing 8 single-entry
Slang kernels via cassie_slang_dispatch.

## Bindings (shared across all entries)

  0  ConstantBuffer<CgPcgParams> { uint rows; }
  1  StructuredBuffer<int>       rowPtr
  2  StructuredBuffer<int>       colIdx
  3  StructuredBuffer<float>     values
  4  StructuredBuffer<float>     diag_inv     (Jacobi preconditioner)
  5  StructuredBuffer<float>     b            (RHS)
  6  RWStructuredBuffer<float>   x            (solution, in/out)
  7  RWStructuredBuffer<float>   r            (residual)
  8  RWStructuredBuffer<float>   z            (M^-1 r)
  9  RWStructuredBuffer<float>   p            (search direction)
  10 RWStructuredBuffer<float>   Ap           (A · p)
  11 RWStructuredBuffer<float>   scalars      (length 8; layout below)

`scalars[0..1]` = current rz (df32 hi/lo pair from dot_r_z)
`scalars[2..3]` = current pAp (df32 hi/lo pair from dot_p_ap)
`scalars[4]`    = alpha (from cg_alpha)
`scalars[5]`    = -alpha (from cg_alpha, for x += α·p / r -= α·Ap fused)
`scalars[6]`    = beta (from cg_beta)
`scalars[7]`    = (scratch for rz_new_lo; rolled into scalars[1] by beta_update)
`scalars[8]`    = (reserved)
`scalars[9]`    = ||r||² written by the single end-of-solve check_residual
                  dispatch — for caller observability only, not consumed
                  by any other kernel

## Entry points (the C++ side dispatches them in this order per CG iter)

  init           — r = b - A·x_initial; z = diag_inv · r; p = z
  spmv_p_to_ap   — Ap = A · p
  -- (dot p·Ap and dot r·z performed by separate DotReduce-style kernels
  --  TODO: fold those in)
  alpha_update   — scalars[4] = rz / pAp; scalars[5] = -alpha
  x_axpy_p       — x[i] += alpha · p[i]
  r_axpy_neg_ap  — r[i] -= alpha · Ap[i]
  jacobi_z       — z[i] = diag_inv[i] · r[i]
  beta_update    — scalars[6] = rz_new / rz_old; scalars[0..1] = rz_new
  p_update       — p[i] = z[i] + beta · p[i]

## Status

Eight entries land in this iteration: `init`, `spmv_p_to_ap`,
`alpha_update`, `x_axpy_p`, `r_axpy_neg_ap`, `jacobi_z`,
`beta_update`, `p_update`. The two missing pieces are the dot
reductions — `dot_p_ap` and `dot_r_z`. Both reuse the existing
`Cloth.SlangCodegen.DotReduce` kernel under its own binding set; the
C++ dispatch rebinds before each dot dispatch. Folding the reductions
into this module is a follow-up that needs cross-workgroup atomic
adds on float (VK_KHR_shader_atomic_float) before it pays off.
-/

namespace CassieAvbd.CgUbershader

open LeanSlang

private def floatTy : SlangType := .scalar .float
private def uintTy  : SlangType := .scalar .uint

private def fIn  (name : String) : SlangBinding :=
  ⟨name, floatTy, Semantic.none, none, none, .qIn⟩
private def fOut (name : String) : SlangBinding :=
  ⟨name, floatTy, Semantic.none, none, none, .qOut⟩

-- df32 error-free transform helpers shared by both dot reductions.
-- Same form as Cloth.SlangCodegen.DotReduce — duplicated rather than
-- imported because LeanSlang emits each module's helpers inline.

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
  { name := "CgPcgParams"
  , fields :=
      [ ⟨"rows", .scalar .uint, Semantic.none, none, none, .qIn⟩ ] }

-- The ubershader follows a constant-work architecture: every solve
-- records the same fixed N iters; no early-exit, no convergence
-- branching. See REFERENCES.bib @maccarthaigh_constant_work for the
-- pattern's reliability properties. check_residual fires once at the
-- end of the iter loop for observability only — the per-iter entries
-- never read scalars[8].

private def globals : List SlangBinding :=
  [ ⟨"params",   .const "CgPcgParams",       Semantic.none, some 0,  some 0, .qIn⟩
  , ⟨"rowPtr",   .roBuf (.scalar .int),      Semantic.none, some 1,  some 0, .qIn⟩
  , ⟨"colIdx",   .roBuf (.scalar .int),      Semantic.none, some 2,  some 0, .qIn⟩
  , ⟨"values",   .roBuf (.scalar .float),    Semantic.none, some 3,  some 0, .qIn⟩
  , ⟨"diag_inv", .roBuf (.scalar .float),    Semantic.none, some 4,  some 0, .qIn⟩
  , ⟨"b",        .roBuf (.scalar .float),    Semantic.none, some 5,  some 0, .qIn⟩
  , ⟨"x",        .rwBuf (.scalar .float),    Semantic.none, some 6,  some 0, .qIn⟩
  , ⟨"r",        .rwBuf (.scalar .float),    Semantic.none, some 7,  some 0, .qIn⟩
  , ⟨"z",        .rwBuf (.scalar .float),    Semantic.none, some 8,  some 0, .qIn⟩
  , ⟨"p",        .rwBuf (.scalar .float),    Semantic.none, some 9,  some 0, .qIn⟩
  , ⟨"Ap",       .rwBuf (.scalar .float),    Semantic.none, some 10, some 0, .qIn⟩
  , ⟨"scalars",  .rwBuf (.scalar .float),    Semantic.none, some 11, some 0, .qIn⟩ ]

/-- One thread per row: computes the initial residual r = b - A·x and
    sets z = M⁻¹·r, p = z. -/
private def initEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "init"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit (.scalar .uint) "rs"
          (.call "uint" [.index (.var "rowPtr") (.var "i")])
      , .declInit (.scalar .uint) "re"
          (.call "uint" [.index (.var "rowPtr") (.bin "+" (.var "i") (.litUint 1))])
      , .declInit (.scalar .float) "s" (.litFloat 0.0)
      , .forCount "k" (.var "rs") (.var "re")
          [ .assign (.var "s")
              (.call "fma"
                [ .index (.var "values") (.var "k")
                , .index (.var "x")
                    (.call "uint" [.index (.var "colIdx") (.var "k")])
                , .var "s" ]) ]
      , .declInit (.scalar .float) "ri"
          (.bin "-" (.index (.var "b") (.var "i")) (.var "s"))
      , .declInit (.scalar .float) "zi"
          (.bin "*" (.index (.var "diag_inv") (.var "i")) (.var "ri"))
      , .assign (.index (.var "r") (.var "i")) (.var "ri")
      , .assign (.index (.var "z") (.var "i")) (.var "zi")
      , .assign (.index (.var "p") (.var "i")) (.var "zi")
      , .ret none ] }

/-- One thread per row: Ap = A · p. Identical algorithm to the Spmv
    kernel but operates on this module's bindings (p instead of x, Ap
    instead of y) — that's the point of bundling: shared buffer set. -/
private def spmvPToApEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "spmv_p_to_ap"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .declInit (.scalar .uint) "rs"
          (.call "uint" [.index (.var "rowPtr") (.var "i")])
      , .declInit (.scalar .uint) "re"
          (.call "uint" [.index (.var "rowPtr") (.bin "+" (.var "i") (.litUint 1))])
      , .declInit (.scalar .float) "s" (.litFloat 0.0)
      , .forCount "k" (.var "rs") (.var "re")
          [ .assign (.var "s")
              (.call "fma"
                [ .index (.var "values") (.var "k")
                , .index (.var "p")
                    (.call "uint" [.index (.var "colIdx") (.var "k")])
                , .var "s" ]) ]
      , .assign (.index (.var "Ap") (.var "i")) (.var "s")
      , .ret none ] }

/-- Single-thread entry. Reads df32 rz pair from scalars[0..1] and
    df32 pAp pair from scalars[2..3], writes alpha = rz / pAp to
    scalars[4] and -alpha to scalars[5]. The CG-side `cg_alpha` kernel
    does the same job for the modular dispatch; bundling it here means
    one fewer uniform-set rebind in the ubershader's per-iter sequence. -/
private def alphaUpdateEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "alpha_update"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .float) "rz"
          (.bin "+" (.index (.var "scalars") (.litUint 0))
                    (.index (.var "scalars") (.litUint 1)))
      , .declInit (.scalar .float) "pAp"
          (.bin "+" (.index (.var "scalars") (.litUint 2))
                    (.index (.var "scalars") (.litUint 3)))
      -- Once CG has converged, both rz and pAp drop to zero. Under
      -- the constant-work design we keep iterating past convergence,
      -- so the divide must be guarded: alpha = (pAp > 0) ? rz/pAp : 0.
      -- alpha = 0 means subsequent x/r updates apply zero delta —
      -- the iterate stays put without NaN polluting r (which the
      -- end-of-solve check_residual reads).
      , .declInit (.scalar .float) "alpha"
          (.ternary (.bin ">" (.var "pAp") (.litFloat 0.0))
                    (.bin "/" (.var "rz") (.var "pAp"))
                    (.litFloat 0.0))
      , .assign (.index (.var "scalars") (.litUint 4)) (.var "alpha")
      , .assign (.index (.var "scalars") (.litUint 5))
          (.un "-" (.var "alpha"))
      , .ret none ] }

/-- x[i] += alpha · p[i]. Reads alpha from scalars[4]. -/
private def xAxpyPEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "x_axpy_p"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .assign (.index (.var "x") (.var "i"))
          (.call "fma"
            [ .index (.var "scalars") (.litUint 4)
            , .index (.var "p") (.var "i")
            , .index (.var "x") (.var "i") ])
      , .ret none ] }

/-- r[i] -= alpha · Ap[i]. Reads -alpha from scalars[5] so the fma
    multiplies the negative directly — saves one negation per thread. -/
private def rAxpyNegApEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "r_axpy_neg_ap"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .assign (.index (.var "r") (.var "i"))
          (.call "fma"
            [ .index (.var "scalars") (.litUint 5)
            , .index (.var "Ap") (.var "i")
            , .index (.var "r") (.var "i") ])
      , .ret none ] }

/-- z[i] = diag_inv[i] · r[i]. Jacobi preconditioner step. -/
private def jacobiZEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "jacobi_z"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .assign (.index (.var "z") (.var "i"))
          (.bin "*" (.index (.var "diag_inv") (.var "i"))
                    (.index (.var "r") (.var "i")))
      , .ret none ] }

/-- Single-thread entry. Reads df32 rz_old from scalars[0..1] and
    df32 rz_new from scalars[6..7] (the per-iter dot reduce writes the
    new pair there), writes beta = rz_new / rz_old to scalars[6] and
    rolls the rz_new pair into scalars[0..1] for the next iter. -/
private def betaUpdateEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 1 1 1]
  , name   := "beta_update"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit (.scalar .float) "rz_old"
          (.bin "+" (.index (.var "scalars") (.litUint 0))
                    (.index (.var "scalars") (.litUint 1)))
      , .declInit (.scalar .float) "rz_new_hi"
          (.index (.var "scalars") (.litUint 6))
      , .declInit (.scalar .float) "rz_new_lo"
          (.index (.var "scalars") (.litUint 7))
      , .declInit (.scalar .float) "rz_new"
          (.bin "+" (.var "rz_new_hi") (.var "rz_new_lo"))
      , .declInit (.scalar .float) "beta"
          (.ternary (.bin ">" (.var "rz_old") (.litFloat 0.0))
                    (.bin "/" (.var "rz_new") (.var "rz_old"))
                    (.litFloat 0.0))
      , .assign (.index (.var "scalars") (.litUint 6)) (.var "beta")
      , .assign (.index (.var "scalars") (.litUint 0)) (.var "rz_new_hi")
      , .assign (.index (.var "scalars") (.litUint 1)) (.var "rz_new_lo")
      , .ret none ] }

/-- Build a single-workgroup df32 dot-reduce entry point that computes
    `dst = inputA · inputB` and writes the (hi, lo) pair into
    `scalars[out_hi_idx]` and `scalars[out_hi_idx + 1]`. Workgroup
    width 256; each thread strides through the full vector. Both
    `dot_p_ap` and `dot_r_z` instantiate this with different inputs
    and output slots. -/
private def buildDotReduceEntry (name : String) (inputA inputB : String)
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
          [ .declare floatTy "p_hi" none
          , .declare floatTy "p_lo" none
          , .expr (.call "two_prod"
              [ .index (.var inputA) (.var "i")
              , .index (.var inputB) (.var "i")
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
          [ .assign (.index (.var "scalars") (.litUint outHiIdx))
              (.index (.var "s_hi") (.litUint 0))
          , .assign (.index (.var "scalars") (.litUint (outHiIdx + 1)))
              (.index (.var "s_lo") (.litUint 0)) ]
      , .ret none ] }

/-- p · Ap → scalars[2..3] (the df32 pair the alpha_update entry
    consumes from those slots). -/
private def dotPApEntry : SlangFunctionDecl :=
  buildDotReduceEntry "dot_p_ap" "p" "Ap" 2

/-- r · z → scalars[6..7] (the df32 pair beta_update consumes there
    as the next-iter rz). -/
private def dotRZEntry : SlangFunctionDecl :=
  buildDotReduceEntry "dot_r_z" "r" "z" 6

/-- Single-workgroup df32 reduce of r·r, write the collapsed fp32 result
    to scalars[9]. Dispatched ONCE at the end of solve_sparse_gpu so the
    caller can observe the final ||r||² — does not influence the iter
    loop, which runs a fixed N iters under the constant-work design. -/
private def checkResidualEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "check_residual"
  , params := [⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩]
  , body   :=
      -- Single call at end of solve_sparse_gpu. Writes ||r||² to
      -- scalars[9] for the caller to observe convergence quality.
      -- No tol_sq compare or flag write — the constant-work design
      -- doesn't act on convergence, only reports it.
      [ .declInit uintTy "t"      (.member (.var "tid") "x")
      , .declInit uintTy "stride" (.litUint 256)
      , .declInit floatTy "acc_hi" (.litFloat 0.0)
      , .declInit floatTy "acc_lo" (.litFloat 0.0)
      , .declInit uintTy "i" (.var "t")
      , .whileLoop (.bin "<" (.var "i") (.member (.var "params") "rows"))
          [ .declare floatTy "p_hi" none
          , .declare floatTy "p_lo" none
          , .expr (.call "two_prod"
              [ .index (.var "r") (.var "i")
              , .index (.var "r") (.var "i")
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
      [ .declInit (.scalar .uint) "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "rows"))
          [ .ret none ]
      , .assign (.index (.var "p") (.var "i"))
          (.call "fma"
            [ .index (.var "scalars") (.litUint 6)
            , .index (.var "p") (.var "i")
            , .index (.var "z") (.var "i") ])
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared :=
      -- Shared by dot_p_ap and dot_r_z; the two reductions run in
      -- separate dispatches so reusing the storage is safe.
      [ { name := "s_hi", elemType := .scalar .float, dims := [256] }
      , { name := "s_lo", elemType := .scalar .float, dims := [256] } ]
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
    [ "init", "spmv_p_to_ap", "dot_p_ap", "alpha_update", "x_axpy_p",
      "r_axpy_neg_ap", "jacobi_z", "dot_r_z", "beta_update",
      "p_update", "check_residual" ] := by
  native_decide

example : shader.entryPoints.length = 11 := by native_decide

end CassieAvbd.CgUbershader
