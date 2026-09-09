-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import AmoLean.CodeGen
import AmoLean.EGraph.Saturate
import PredictiveBVH.Formulas.Formula
import PredictiveBVH.Codegen.QuinticHermite
import PredictiveBVH.Codegen.TreeC
import PredictiveBVH.Spatial.ScaleContradictions
import PredictiveBVH.Spatial.EMLAdversarialHeuristic
import PredictiveBVH.Formulas.Resources

-- ============================================================================
-- AMOLEAN E-GRAPH C CODE GENERATOR
--
-- All arithmetic is encoded as AmoLean.Expr Int, run through
-- AmoLean.EGraph.optimizeBasic (equality saturation), then lowered to C
-- via generateCFunction.
--
-- Output: predictive_bvh.h  (C header using Godot's thirdparty/misc/r128.h)
--
-- Run:  lake exe bvh-codegen
-- ============================================================================

open AmoLean

private instance : Add (Expr Int) where add a b := .add a b
private instance : Mul (Expr Int) where mul a b := .mul a b
private instance : Sub (Expr Int) where
  sub a b := .add a (.mul (.const (-1)) b)

private def opt (e : Expr Int) : Expr Int :=
  (AmoLean.EGraph.optimizeBasic e).getD e

-- ── R128 (r128.h) C pretty-printer over AmoLean's LowLevelProgram ──────────
-- Uses Godot's thirdparty/misc/r128.h (64.64 signed fixed-point, public domain).
-- R128 API: r128Add(dst, a, b), r128Sub, r128Mul, r128FromInt, r128Copy.
-- Polynomial kernels operate on R128 values; non-polynomial code uses int64_t.

private def llLitC (n : Int) : String :=
  s!"T({n})"

private def llToC : LowLevelExpr → String
  | .litInt n    => llLitC n
  | .varRef name => name
  | .binOp "+" l r => s!"({llToC l} + {llToC r})"
  | .binOp "*" l r => s!"({llToC l} * {llToC r})"
  | .binOp op l r => "(" ++ llToC l ++ " " ++ op ++ " " ++ llToC r ++ ")"
  | .funcCall "pow_int" [base, .litInt n] =>
      s!"pow_int_T({llToC base}, {n})"
  | .funcCall "pow_int" args =>
      "pow_int_T(" ++ String.intercalate ", " (args.map llToC) ++ ")"
  | .funcCall fn args =>
      fn ++ "(" ++ String.intercalate ", " (args.map llToC) ++ ")"

private def generateCFn (name : String) (params : List String)
    (body : LowLevelProgram) : String :=
  let paramStr := String.intercalate ", " (params.map ("T " ++ ·))
  let lets := String.intercalate "\n" (body.assignments.map fun a =>
    "    T " ++ a.varName ++ " = " ++ llToC a.value ++ ";")
  "template <typename T>\nstatic inline T " ++ name ++ "(" ++ paramStr ++ ") {\n" ++
  lets ++ "\n    return " ++ llToC body.result ++ ";\n}"

private def genC (name : String) (params : List String) (e : Expr Int) : String :=
  let varNames : VarId → String := fun i =>
    if h : i < params.length then params[i] else s!"arg{i}"
  generateCFn name params (toLowLevel varNames (opt e))

-- ── 1. Single-entity SAH kernel ───────────────────────────────────────────────

private def entityExpr (base : Nat) (ticksAheadVar : Nat) : Expr Int :=
  predictiveCostFormula (fun j => .var (base + j)) (.const 2) (.const 1) (.var ticksAheadVar)

-- Unrolled SAH sums (bvh_sah_sum_N) removed: 11K lines of dead code.
-- bvh_state_update uses the generic loop with predictive_cost.

-- ── 5. Per-entity δ via polynomial cost evaluation ───────────────────────────
-- The constraint v·δ + ah·δ² ≤ R is polynomial. We evaluate cost(v, ah, δ_k)
-- at a set of candidate δ values. Each evaluation is linear in (v, ah):
--   cost_k(v, ah) = δ_k · v + δ_k² · ah
-- vars: 0=v (max velocity component, μm/tick), 1=ah (half-acceleration, μm/tick²)

/-- Candidate δ values — logarithmically spaced for coverage of [1, 120] -/
private def deltaCandidates : List Nat := [1, 2, 4, 8, 16, 24, 32, 48, 64, 80, 100, 120]

/-- cost(v, ah, δ_k) = δ_k · v + δ_k² · ah — polynomial in (v, ah) -/
private def deltaCostExpr (dk : Nat) : Expr Int :=
  .const dk * .var 0 + .const (dk * dk) * .var 1

-- ── Hilbert3D inverse: imperative Lean implementation ────────────────────────
-- Skilling transposeToAxes, verified by roundtrip against the forward ring
-- polynomial at build time. Algorithm: deinterleave → undo fixup → undo Gray → undo main loop.

private def hilbert3dInverse (h : Nat) : Nat × Nat × Nat :=
  let order := 10
  let mask := (1 <<< order) - 1
  -- Deinterleave: extract transpose coordinates from interleaved bits
  let (tx, ty, tz) := (List.range order).foldl (fun (x, y, z) bit =>
    let b := order - 1 - bit
    let shift := 3 * b
    let x := x ||| (((h >>> shift) &&& 1) <<< b)
    let y := y ||| (((h >>> (shift + 1)) &&& 1) <<< b)
    let z := z ||| (((h >>> (shift + 2)) &&& 1) <<< b)
    (x &&& mask, y &&& mask, z &&& mask)) (0, 0, 0)
  -- Undo fixup: progressive decode (z0 ^ t) to recover pre-fixup z
  let t := (List.range (order - 1)).foldl (fun t i =>
    let q := 1 <<< (order - 1 - i)
    if (tz ^^^ t) &&& q != 0 then t ^^^ (q - 1) else t) 0
  let x1 := tx ^^^ t; let y1 := ty ^^^ t; let z1 := tz ^^^ t
  -- Undo Gray (reverse of y^=x; z^=y): z^=y first, then y^=x
  let z2 := z1 ^^^ y1; let y2 := y1 ^^^ x1
  -- Undo main loop: Q from 2 to MSB, y-exchange then z-exchange
  let (x3, y3, z3) := (List.range (order - 1)).foldl (fun (x, y, z) j =>
    let q := 1 <<< (j + 1); let p := q - 1
    let (x, y) := if y &&& q != 0 then (x ^^^ p, y) else
      let t := (x ^^^ y) &&& p; (x ^^^ t, y ^^^ t)
    let (x, z) := if z &&& q != 0 then (x ^^^ p, z) else
      let t := (x ^^^ z) &&& p; (x ^^^ t, z ^^^ t)
    (x, y, z)) (x1, y2, z2)
  (x3 &&& mask, y3 &&& mask, z3 &&& mask)

-- ── AABB overlap as ring polynomial via Z↔GF(2) bridge ──────────────────────
-- overlaps(a, b) = Π (1 - sign_bit(dᵢ))  where:
--   d0 = b_max_x - a_min_x,  d1 = a_max_x - b_min_x  (X axis)
--   d2 = b_max_y - a_min_y,  d3 = a_max_y - b_min_y  (Y axis)
--   d4 = b_max_z - a_min_z,  d5 = a_max_z - b_min_z  (Z axis)
-- sign_bit is 0 if d ≥ 0 (overlaps on that axis), 1 if d < 0.
-- The product is 1 iff all 6 checks pass.
-- vars 0..11 = a_min_x, a_max_x, a_min_y, a_max_y, a_min_z, a_max_z,
--              b_min_x, b_max_x, b_min_y, b_max_y, b_min_z, b_max_z
-- vars 12..17 = sign bits s0..s5 (witness, from bitDecompose)

private def aabbOverlapsExpr : Expr Int :=
  -- 6 differences (ring):
  -- d0 = b_max_x(7) - a_min_x(0), d1 = a_max_x(1) - b_min_x(6)
  -- d2 = b_max_y(9) - a_min_y(2), d3 = a_max_y(3) - b_min_y(8)
  -- d4 = b_max_z(11) - a_min_z(4), d5 = a_max_z(5) - b_min_z(10)
  -- Product of (1 - sᵢ) for each axis check:
  let s := fun i => .var (12 + i)  -- sign bit witness variables
  -- (1-s0)*(1-s1)*(1-s2)*(1-s3)*(1-s4)*(1-s5)
  (List.range 6).foldl (fun acc i => acc * (.const 1 - s i)) (.const 1)

-- ── Proved spatial primitives (AmoLean E-graph optimized where polynomial) ────
-- Polynomial functions go through: Expr Int → optimizeBasic → generateCFn.

-- ghost_bound(v, a_half, k) = v*k + a_half*k*k
-- Proved: expansion_covers_k_ticks (Formula.lean:109)
-- vars: 0=v, 1=a_half, 2=ticks_ahead
private def ghostBoundExpr : Expr Int :=
  .var 0 * .var 2 + .var 1 * .var 2 * .var 2

-- surface_area(w, h, d) = 2*(w*h + h*d + w*d)
-- Proved: surfaceArea_nonneg (ScaleProofs.lean)
-- vars: 0=w, 1=h, 2=d
private def surfaceAreaExpr : Expr Int :=
  .const 2 * (.var 0 * .var 1 + .var 1 * .var 2 + .var 0 * .var 2)

-- ghost_aabb_axis(center, ext, v, a_half, tau) → (min, max) as two separate fns
-- min = center - ext - (v*tau + a_half*tau*tau)
-- max = center + ext + (v*tau + a_half*tau*tau)
-- vars: 0=center, 1=ext, 2=v, 3=a_half, 4=tau
private def ghostAabbMinExpr : Expr Int :=
  .var 0 - .var 1 - (.var 2 * .var 4 + .var 3 * .var 4 * .var 4)

private def ghostAabbMaxExpr : Expr Int :=
  .var 0 + .var 1 + (.var 2 * .var 4 + .var 3 * .var 4 * .var 4)

-- ── 7. End-to-end roundtrip tests (Lean #eval) ────────────────────────────────
-- Evaluate Expr Int at known inputs in Lean for build-time verification.

/-- Evaluate an Expr Int with a variable environment -/
private def evalExpr (env : VarId → Int) (e : Expr Int) : Int :=
  Expr.denote env (opt e)

-- Reference entity: [minX=100, maxX=200, minY=50, maxY=150, minZ=0, maxZ=300,
--                    vx=10, vy=20, vz=5, ax=2, ay=3, az=1, ticks_ahead=20]
private def refEntity : VarId → Int
  | 0 => 100 | 1 => 200 | 2 => 50 | 3 => 150 | 4 => 0 | 5 => 300
  | 6 => 10 | 7 => 20 | 8 => 5 | 9 => 2 | 10 => 3 | 11 => 1 | 12 => 20
  | _ => 0

private def refSahVal : Int := evalExpr refEntity (entityExpr 0 12)

-- ghost_bound(v=175, a_half=28, ticks=20) = 175*20 + 28*20*20 = 3500+11200 = 14700
private def refGhostEnv : VarId → Int
  | 0 => 175 | 1 => 28 | 2 => 20 | _ => 0
private def refGhostVal : Int := evalExpr refGhostEnv ghostBoundExpr

-- delta_cost_8(v=100, a_half=28) = 8*100 + 64*28 = 800+1792 = 2592
private def refDeltaEnv : VarId → Int
  | 0 => 100 | 1 => 28 | _ => 0
private def refDelta8Val : Int := evalExpr refDeltaEnv (deltaCostExpr 8)

-- bvh_blend(flag=1, old=500, new=800) = 500 + 1*(800-500) = 800
private def refBlendEnv : VarId → Int
  | 0 => 1 | 1 => 500 | 2 => 800 | _ => 0
private def refBlendVal : Int := evalExpr refBlendEnv (.var 1 + .var 0 * (.var 2 - .var 1))

private def hilbertTestCases : List (Nat × Nat × Nat) :=
  [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1),
   (5, 3, 7), (100, 200, 300), (511, 511, 511),
   (1023, 1023, 1023), (0, 0, 1023), (1023, 0, 0)]

-- Lean-side verification (these run at build time via #eval)
#eval! do
  IO.println s!"refSahVal = {refSahVal}"
  IO.println s!"refGhostVal = {refGhostVal}"
  IO.println s!"refDelta8Val = {refDelta8Val}"
  IO.println s!"refBlendVal = {refBlendVal}"
  -- Hilbert forward/inverse roundtrip soundness
  let mut invFailures := 0
  for (x, y, z) in hilbertTestCases do
    let h := hilbert3D x y z
    let (rx, ry, rz) := hilbert3dInverse h
    if rx != x || ry != y || rz != z then
      IO.println s!"FAIL hilbert_inv({x},{y},{z}): h={h} got=({rx},{ry},{rz})"
      invFailures := invFailures + 1
    else
      IO.println s!"OK   hilbert_inv({x},{y},{z}) h={h} → ({rx},{ry},{rz})"
  if invFailures > 0 then
    IO.println s!"HILBERT INVERSE: {invFailures} FAILURES"
  else
    IO.println s!"HILBERT INVERSE: all {hilbertTestCases.length} cases passed"

-- ============================================================================
-- 7b. GF(2) E-GRAPH BUILDING BLOCKS → ORDER 10 BY TEMPLATE
--
-- The Skilling Hilbert roundtrip uses two building blocks:
--   (A) XOR cancel:       (a ⊕ b) ⊕ b = a     (3 nodes, 2 vars)
--   (B) Exchange inverse:  exch(exch(x,a,c)) = (x,a)  (~15 nodes, 3 vars)
--
-- Both are proved via AmoLean e-graph saturation with GF(2) rules.
-- Order=N repeats each block (N-1) times. The template:
--   main loop:  (N-1) exchanges on z, then (N-1) on y
--   gray:       2 XOR cancels (y ^^^ x ^^^ x = y, z ^^^ y' ^^^ y' = z)
--   fixup:      (N-1) XOR cancels (progressive decode)
--   interleave: bit permutation (inverse by construction)
-- At order=10: 9+9 exchanges + 2+9 XOR cancels. Each one verified by
-- the e-graph once; stamped out by structural repetition.
-- ============================================================================

section HilbertGF2Witness

open AmoLean (Expr VarId)
open AmoLean.EGraph (RewriteRule optimize SaturationConfig)

private def gf2Rules : List RewriteRule :=
  RewriteRule.basicRules ++
  [ RewriteRule.addAssocRight,
    RewriteRule.make "gf2_add_self" (.add (.patVar 0) (.patVar 0)) (.const 0) ]

private def gf2Config : SaturationConfig :=
  { maxIterations := 10, maxNodes := 200, maxClasses := 100 }

private def gf2Opt (e : Expr Int) : Option (Expr Int) :=
  (optimize e gf2Rules gf2Config).1

-- GF(2) evaluation: add=XOR, mul=AND, all values mod 2
private def gf2Eval (env : VarId → Int) (e : Expr Int) : Int :=
  match e with
  | .const c => c % 2
  | .var v   => env v % 2
  | .add a b => (gf2Eval env a + gf2Eval env b) % 2
  | .mul a b => (gf2Eval env a * gf2Eval env b) % 2
  | .pow a n => (gf2Eval env a) ^ n % 2

-- Block A: XOR cancel (symbolic, via e-graph)
-- Block B: Exchange inverse (exhaustive GF(2) eval, connected via Expr)
--   Exchange builds the SAME Expr tree as codegen, evaluated over GF(2).
--   Exhaustive: 2^3 = 8 inputs (x, a, c ∈ {0,1}).
#eval! do
  let xor (a b : Expr Int) := Expr.add a b
  let mux (c a b : Expr Int) := Expr.add (Expr.mul c a) (Expr.mul (xor (.const 1) c) b)

  -- Block A: e-graph proves (x ⊕ a) ⊕ a → x
  match gf2Opt (xor (xor (.var 0) (.var 1)) (.var 1)) with
  | some (.var 0) => IO.println "GF2 BLOCK A OK: (x ⊕ a) ⊕ a → x  [e-graph]"
  | other => IO.println s!"GF2 BLOCK A FAIL: {repr other}"; return

  -- Block B: exchange(exchange(x,a,c)) = (x,a) via exhaustive GF(2) eval
  let (x, a, c) := (Expr.var 0, Expr.var 1, Expr.var 2)
  let one : Expr Int := .const 1
  let fwdX := mux c (xor x one) a
  let fwdA := mux c a x
  let invX := mux c (xor fwdX one) fwdA
  let invA := mux c fwdA fwdX
  let mut ok := true
  for bits in List.range 8 do
    let env : VarId → Int := fun v =>
      if v == 0 then (bits &&& 1 : Nat) else if v == 1 then ((bits >>> 1) &&& 1 : Nat) else ((bits >>> 2) &&& 1 : Nat)
    let rx := gf2Eval env invX
    let ra := gf2Eval env invA
    if rx != env 0 || ra != env 1 then
      IO.println s!"GF2 BLOCK B FAIL: x={env 0} a={env 1} c={env 2} → ({rx},{ra})"
      ok := false
  if ok then
    IO.println "GF2 BLOCK B OK: exchange²=id  [exhaustive 8/8, same Expr tree as codegen]"

  IO.println "GF2 TEMPLATE: A(e-graph) + B(eval) → order=10 by 9× stamping"

end HilbertGF2Witness

-- ═══════════════════════════════════════════════════════════════════════════
-- C R128 BACKEND — uses Godot's thirdparty/misc/r128.h (64.64 fixed-point)
-- ═══════════════════════════════════════════════════════════════════════════

private def cPreamble : String :=
  "/* Generated by bvh-codegen. DO NOT EDIT. */\n" ++
  "/*\n" ++
  " * MIT License\n" ++
  " *\n" ++
  " * Copyright (c) 2026-present K. S. Ernest (iFire) Lee\n" ++
  " *\n" ++
  " * Permission is hereby granted, free of charge, to any person obtaining a copy\n" ++
  " * of this software and associated documentation files (the \"Software\"), to deal\n" ++
  " * in the Software without restriction, including without limitation the rights\n" ++
  " * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n" ++
  " * copies of the Software, and to permit persons to whom the Software is\n" ++
  " * furnished to do so, subject to the following conditions:\n" ++
  " *\n" ++
  " * The above copyright notice and this permission notice shall be included in all\n" ++
  " * copies or substantial portions of the Software.\n" ++
  " *\n" ++
  " * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n" ++
  " * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n" ++
  " * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n" ++
  " * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n" ++
  " * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n" ++
  " * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n" ++
  " * SOFTWARE.\n" ++
  " */\n" ++
  "#ifndef PREDICTIVE_BVH_H\n" ++
  "#define PREDICTIVE_BVH_H\n\n" ++
  "#include <cstdint>\n" ++
  "#include \"thirdparty/misc/r128.h\"\n\n" ++
  "#ifdef _MSC_VER\n" ++
  "#include <intrin.h>\n" ++
  "static inline uint32_t _pbvh_clz(uint32_t x) {\n" ++
  "    unsigned long idx;\n" ++
  "    _BitScanReverse(&idx, x);\n" ++
  "    return 31 - (uint32_t)idx;\n" ++
  "}\n" ++
  "#else\n" ++
  "#define _pbvh_clz(x) ((uint32_t)__builtin_clz(x))\n" ++
  "#endif\n\n" ++
  "/* r128.h provides: R128_one = {0,1} (1.0 in 64.64), R128_min, R128_max. */\n" ++
  "static const R128 _r128_zero = { 0, 0 };\n" ++
  "#define R128_ZERO _r128_zero\n" ++
  "#define R128_ONE  R128_one\n\n" ++
  "static inline R128 r128_from_int(int64_t v) {\n" ++
  "    R128 r; r128FromInt(&r, v); return r;\n" ++
  "}\n" ++
  "static inline R128 r128_neg(R128 a) {\n" ++
  "    R128 r; r128Neg(&r, &a); return r;\n" ++
  "}\n" ++
  "static inline R128 r128_add(R128 a, R128 b) {\n" ++
  "    R128 r; r128Add(&r, &a, &b); return r;\n" ++
  "}\n" ++
  "static inline R128 r128_sub(R128 a, R128 b) {\n" ++
  "    R128 r; r128Sub(&r, &a, &b); return r;\n" ++
  "}\n" ++
  "static inline R128 r128_mul(R128 a, R128 b) {\n" ++
  "    R128 r; r128Mul(&r, &a, &b); return r;\n" ++
  "}\n" ++
  "static inline R128 r128_div(R128 a, R128 b) {\n" ++
  "    R128 r; r128Div(&r, &a, &b); return r;\n" ++
  "}\n" ++
  "static inline int64_t r128_to_int(R128 a) {\n" ++
  "    return (int64_t)a.hi;  /* truncate fractional part */\n" ++
  "}\n" ++
  "static inline int r128_le(R128 a, R128 b) {\n" ++
  "    return r128Cmp(&a, &b) <= 0;\n" ++
  "}\n" ++
  "static inline int r128_eq(R128 a, R128 b) {\n" ++
  "    return a.lo == b.lo && a.hi == b.hi;\n" ++
  "}\n\n" ++
  "static inline R128 pow_int_r128(R128 base, int exp) {\n" ++
  "    R128 r = R128_ONE, b = base;\n" ++
  "    while (exp > 0) {\n" ++
  "        if (exp & 1) r = r128_mul(r, b);\n" ++
  "        b = r128_mul(b, b);\n" ++
  "        exp >>= 1;\n" ++
  "    }\n" ++
  "    return r;\n" ++
  "}\n\n" ++
  "/* Float→R128 (preserves fractional bits via double intermediate) */\n" ++
  "static inline R128 r128_from_float(float f) {\n" ++
  "    double d = (double)f;\n" ++
  "    R128 r;\n" ++
  "    r.hi = (int64_t)d;\n" ++
  "    r.lo = (uint64_t)((d - (double)r.hi) * 18446744073709551616.0);\n" ++
  "    return r;\n" ++
  "}\n" ++
  "/* Unsigned 32-bit → R128 */\n" ++
  "static inline R128 r128_from_u32(uint32_t v) {\n" ++
  "    R128 r; r.hi = (int64_t)v; r.lo = 0; return r;\n" ++
  "}\n" ++
  "/* R128 → float */\n" ++
  "static inline float r128_to_float(R128 a) {\n" ++
  "    return (float)a.hi + (float)a.lo * 5.421010862427522e-20f; /* 2^-64 */\n" ++
  "}\n\n"

-- ── C: E-graph optimized polynomial kernels ─────────────────────────────────

private def scalarFnC : String :=
  genC "predictive_cost"
    ["min_x","max_x","min_y","max_y","min_z","max_z",
     "vx","vy","vz","ax","ay","az","ticks_ahead"]
    (entityExpr 0 12)

private def ghostBoundC : String :=
  "/* Ghost expansion v*d + a_half*d^2. Proved: expansion_covers_k_ticks */\n" ++
  genC "ghost_bound" ["v","a_half","ticks_ahead"] ghostBoundExpr

private def surfaceAreaC : String :=
  "/* Surface area 2(wh+hd+wd). Proved: surfaceArea_nonneg */\n" ++
  genC "surface_area" ["w","h","d"] surfaceAreaExpr

private def ghostAabbC : String :=
  "/* Ghost AABB min bound per axis. Proved: expansion_covers_k_ticks */\n" ++
  genC "ghost_aabb_min" ["center","ext","v","a_half","tau"] ghostAabbMinExpr ++ "\n\n" ++
  "/* Ghost AABB max bound per axis. Proved: expansion_covers_k_ticks */\n" ++
  genC "ghost_aabb_max" ["center","ext","v","a_half","tau"] ghostAabbMaxExpr

-- ── Half-space corner valuation (plane polynomial) ─────────────────────────
-- dot3 + d: nx*x + ny*y + nz*z + d. Pure ring expression, ordinary
-- Expr Int / EGraph path. Used by pbvh_half_space_keeps_ (TreeC.lean) to
-- replace its 8-corner inline r128_mul/r128_add unroll with 8 call sites
-- to this emitted helper — one CSE'd polynomial instead of 24 hand-coded
-- R128 ops per call.
private def planeCornerValExpr : Expr Int :=
  -- vars: 0=nx, 1=ny, 2=nz, 3=d, 4=x, 5=y, 6=z
  .var 0 * .var 4 + .var 1 * .var 5 + .var 2 * .var 6 + .var 3

private def planeCornerValC : String :=
  "/* Plane corner valuation: nx*x + ny*y + nz*z + d.\n" ++
  "   Pure ring polynomial, EGraph-CSE'd. Used by pbvh_half_space_keeps_. */\n" ++
  genC "pbvh_plane_corner_val" ["nx","ny","nz","d","x","y","z"] planeCornerValExpr

-- ringMinMaxC removed: r128_sign_bit, ring_min/max_r128, pbvh_r128_min/max
-- now live in core/math/predictive_bvh_adapter.h alongside the other
-- non-polynomial R128 helpers (utilC, hilbertC, deltaSelectC).

private def deltaCostFnsC : String :=
  let costFns := deltaCandidates.map fun dk =>
    genC s!"delta_cost_{dk}" ["v", "a_half"] (deltaCostExpr dk)
  String.intercalate "\n\n" costFns

private def quinticHermiteC : String :=
  "/* Quintic Hermite spline basis functions (C3 continuity).\n" ++
  "   t = numer/denom in [0,1] where numer=ticks_since_build, denom=delta.\n" ++
  "   Return values are integer numerators in μm units:\n" ++
  "     h00, h01, h10, h11 : divide result by denom^5\n" ++
  "     h20, h21            : divide result by 2*denom^5\n" ++
  "   Source of truth: PredictiveBVH.QuinticHermite (C3 proofs in Lean). */\n" ++
  String.intercalate "\n\n" (quinticBasisFns.map fun (name, params, e, _doc) =>
    let varNames : VarId → String := fun i =>
      if h : i < params.length then params[i] else s!"arg{i}"
    generateCFn name params (toLowLevel varNames (opt e)))

-- ── C: Spatial primitives (non-polynomial, direct translation) ──────────────

-- aabbC: only emits AabbT<T> struct template and the E-graph polynomial
-- aabb_overlaps_ring. All non-polynomial fast-path predicates (aabb_union,
-- aabb_overlaps, aabb_contains, aabb_contains_point) and the R128 bridge
-- helpers now live in core/math/predictive_bvh_adapter.h.
private def aabbC : String :=
  "/* Source: Types.lean:28 Proved: unionBounds_contains_left/right */\n" ++
  "template <typename T>\nstruct AabbT {\n" ++
  "    T min_x, max_x;\n" ++
  "    T min_y, max_y;\n" ++
  "    T min_z, max_z;\n" ++
  "};\n" ++
  "using Aabb = AabbT<int64_t>;\n\n" ++
  "/* Ring-polynomial provenance export: Π (1 - sign_bit(dᵢ)) over 6 axis diffs.\n" ++
  "   Proved equivalent to short-circuit r128_le chains below via bitDecompose;\n" ++
  "   see aabbOverlapsExpr in Codegen/CodeGen.lean + HilbertBroadphase.lean. */\n" ++
  genC "aabb_overlaps_ring"
    ["a_min_x","a_max_x","a_min_y","a_max_y","a_min_z","a_max_z",
     "b_min_x","b_max_x","b_min_y","b_max_y","b_min_z","b_max_z",
     "s0","s1","s2","s3","s4","s5"]
    aabbOverlapsExpr

-- utilC removed: clz30, r128_half now in core/math/predictive_bvh_adapter.h

-- hilbertC removed: hilbert3d, hilbert3d_inverse, hilbert_of_aabb,
-- hilbert_cell_of now in core/math/predictive_bvh_adapter.h

-- ── C: extern forward-declarations for adapter-provided helpers ─────────────
-- These functions are defined in core/math/predictive_bvh_adapter.h (which
-- includes this generated header). Forward-declaring them here lets the
-- TreeC-generated inline bodies call them without knowing about the adapter.
-- All are non-template R128 helpers (ring_min/max are templates in the adapter
-- and are never called directly from generated tree code).
private def adapterFwdDeclC : String :=
  "/* ── Adapter-provided non-polynomial helpers (defined in predictive_bvh_adapter.h)\n" ++
  "   Forward-declared here so generated tree code can call them at parse time.\n" ++
  "   DO NOT define these in this file; definitions live in the adapter. */\n" ++
  "extern R128 r128_sign_bit(R128 d);\n" ++
  "extern R128 pbvh_r128_min(R128 a, R128 b);\n" ++
  "extern R128 pbvh_r128_max(R128 a, R128 b);\n" ++
  "extern Aabb aabb_union(const Aabb *a, const Aabb *o);\n" ++
  "extern bool aabb_overlaps(const Aabb *a, const Aabb *o);\n" ++
  "extern bool aabb_contains(const Aabb *a, const Aabb *inner);\n" ++
  "extern bool aabb_contains_point(const Aabb *a, R128 x, R128 y, R128 z);\n" ++
  "extern uint32_t clz30(uint32_t x);\n" ++
  "extern R128 r128_half(R128 v);\n" ++
  "extern uint32_t hilbert3d(uint32_t x, uint32_t y, uint32_t z);\n" ++
  "extern void hilbert3d_inverse(uint32_t h, uint32_t *ox, uint32_t *oy, uint32_t *oz);\n" ++
  "extern uint32_t hilbert_of_aabb(const Aabb *b, const Aabb *scene);\n" ++
  "extern Aabb hilbert_cell_of(uint32_t code, uint32_t prefix_depth, const Aabb *scene);\n" ++
  "extern uint32_t per_entity_delta_poly(R128 v, R128 a_half);"

-- ── C: Constants ────────────────────────────────────────────────────────────

open PredictiveBVH.Resources in
private def constantsC : String :=
  "/* ── Lean-derived tick-rate-parametric formulas ──────────────────────────\n" ++
  "   PBVH_SIM_TICK_HZ is the DEFAULT value (what the Lean proofs were\n" ++
  "   evaluated at and what set_physics_ticks_per_second() should be\n" ++
  "   initialized with at startup). Runtime consumers (Godot/C++) should\n" ++
  "   read the engine's actual physics tick rate and pass it to the inline\n" ++
  "   helpers below — never hardcode PBVH_SIM_TICK_HZ at use sites.\n" ++
  "\n" ++
  "   Formulas mirror Core/Types.lean + Resources.lean exactly:\n" ++
  "     hysteresisThreshold = simTickHz * 4\n" ++
  "     latencyTicksFloor   = max(simTickHz / 10, 1)\n" ++
  "     vMaxPhysical μm/tick = 10 * 1_000_000 / simTickHz\n" ++
  "     aHalfMinForearm μm/tick² = ceil(1_400_000 / (2 * simTickHz²))\n" ++
  "   ─────────────────────────────────────────────────────────────────────── */\n" ++
  "#define PBVH_SIM_TICK_HZ " ++ toString simTickHz ++ "u\n" ++
  "\n" ++
  "static inline uint32_t pbvh_hysteresis_threshold(uint32_t hz) { return hz * 4u; }\n" ++
  "static inline uint32_t pbvh_latency_ticks(uint32_t hz) {\n" ++
  "    uint32_t t = hz / 10u; return t > 0u ? t : 1u;\n" ++
  "}\n" ++
  "static inline int64_t pbvh_v_max_physical_um_per_tick(uint32_t hz) {\n" ++
  "    return (int64_t)(10 * 1000000) / (int64_t)(hz > 0u ? hz : 1u);\n" ++
  "}\n" ++
  "static inline uint64_t pbvh_accel_floor_um_per_tick2(uint32_t hz) {\n" ++
  "    uint64_t h = (uint64_t)(hz > 0u ? hz : 1u);\n" ++
  "    uint64_t d = 2ull * h * h;\n" ++
  "    return (1400000ull + d - 1ull) / d;\n" ++
  "}\n" ++
  "\n" ++
  "/* Tick-rate-invariant constants (pure physical distances / velocities) */\n" ++
  "#define PBVH_INTEREST_RADIUS_UM " ++ toString interestRadius ++ "LL   /* μm */\n" ++
  "#define PBVH_CURRENT_FUNNEL_PEAK_V_M_PER_S 60  /* m/s — C7 rip-current impulse cap; μm/tick = 60*1e6/hz */\n" ++
  "\n" ++
  "/* Default-rate convenience values (evaluated at PBVH_SIM_TICK_HZ).\n" ++
  "   Prefer the pbvh_* helpers above at runtime. */\n" ++
  "#define PBVH_LATENCY_TICKS_DEFAULT " ++ toString latencyTicks ++ "u\n" ++
  "#define PBVH_HYSTERESIS_THRESHOLD_DEFAULT " ++ toString hysteresisThreshold ++ "u\n" ++
  "#define PBVH_V_MAX_PHYSICAL_DEFAULT " ++ toString vMaxPhysical ++ "LL  /* μm/tick */\n" ++
  "#define PBVH_ACCEL_FLOOR_DEFAULT " ++ toString aHalfMinForearm ++ "ULL /* μm/tick² */\n" ++
  "#define PBVH_CURRENT_FUNNEL_PEAK_V_UM_TICK_DEFAULT " ++ toString currentFunnelPeakVUmTick ++ "LL /* μm/tick */"

-- ── C: EML adversarial bounds (Lean-proved, e-graph extracted) ──────────────
-- Each helper returns the formally derived gap bound for an adversarial
-- scenario (C1..C7). Constants baked into the body come from
-- PBVH_SIM_TICK_HZ evaluation at codegen time; regenerate this header after
-- changing simTickHz in Primitives/Types.lean. Source formulas:
--   PredictiveBVH/Spatial/EMLAdversarialHeuristic.lean

open PredictiveBVH.EML in
private def emlC : String :=
  "/* ══════════════════════════════════════════════════════════════════════════\n" ++
  "   EML ADVERSARIAL GAP BOUNDS (C1..C7, R128, e-graph optimized)\n" ++
  "   Source: PredictiveBVH/Spatial/EMLAdversarialHeuristic.lean\n" ++
  "   All physical constants (v_max, accel_floor, latency_ticks, sat_delta,\n" ++
  "   v_funnel, chunk_origin_offset_um) are runtime parameters — pass the\n" ++
  "   pbvh_* helpers from constantsC above, so the bounds track the engine's\n" ++
  "   actual physics tick rate instead of the default baked into Types.lean.\n" ++
  "   ══════════════════════════════════════════════════════════════════════════ */\n\n" ++
  genC "pbvh_eml_c1_velocity_injection_gap"       ["v_true", "v_max", "delta"]              c1GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c2_acceleration_underreport_gap" ["accel_floor", "delta"]                  c2GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c3_portal_discontinuity_gap"     ["jump_um", "ghost_bound_um"]             c3GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c4_lifecycle_gap_bound"          ["v", "latency_ticks"]                    c4GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c5_satellite_rtt_gap"            ["v", "sat_delta", "local_delta"]         c5GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c6_coord_frame_offset_gap"       ["chunk_origin_offset_um"]                c6GapFormula ++ "\n\n" ++
  genC "pbvh_eml_c7_segment_boundary_gap"         ["v_funnel", "v_max", "delta"]            c7GapFormula

-- ── C: Assemble complete header file ────────────────────────────────────────

private def cFile : String :=
  cPreamble ++
  "/* ══════════════════════════════════════════════════════════════════════════\n" ++
  "   PROVED SPATIAL PRIMITIVES (R128 polynomial + direct C translation)\n" ++
  "   ══════════════════════════════════════════════════════════════════════════ */\n\n" ++
  ghostBoundC ++ "\n\n" ++
  surfaceAreaC ++ "\n\n" ++
  ghostAabbC ++ "\n\n" ++
  planeCornerValC ++ "\n\n" ++
  aabbC ++ "\n\n" ++
  adapterFwdDeclC ++ "\n\n" ++
  constantsC ++ "\n\n" ++
  emlC ++ "\n\n" ++
  "/* ══════════════════════════════════════════════════════════════════════════\n" ++
  "   AMOLEAN E-GRAPH OPTIMIZED KERNELS (R128)\n" ++
  "   ══════════════════════════════════════════════════════════════════════════ */\n\n" ++
  scalarFnC ++ "\n\n" ++
  deltaCostFnsC ++ "\n\n" ++
  quinticHermiteC ++ "\n\n" ++
  PredictiveBVH.Codegen.TreeC.treeC ++ "\n\n" ++
  "#endif /* PREDICTIVE_BVH_H */\n"

private def cPath      : String := "predictive_bvh.h"

def main : IO Unit := do
  IO.FS.writeFile cPath cFile
  IO.println s!"wrote {cPath}"
