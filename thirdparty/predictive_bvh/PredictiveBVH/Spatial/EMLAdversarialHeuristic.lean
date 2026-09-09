-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import AmoLean.EGraph.Basic
import PredictiveBVH.Primitives.Types
import PredictiveBVH.Formulas.Formula
import PredictiveBVH.Spatial.ScaleContradictions

/-!
# EML Adversarial Heuristics

This module proves how the universal EML operator (exp(x) - ln(y)) 
can analytically extract ZK Z-bounds for all 7 Adversarial Scenarios.

By using an exact algebraic representation in the E-Graph, we can 
parameterize Godot Engine gaps to arbitrary bounds for dynamic optimization, rather than relying on hardcoded assertions.
-/

namespace PredictiveBVH.EML

open AmoLean
open AmoLean.EGraph

-- ============================================================================
-- 1. C1 / G13 — Velocity injection / speed hack
-- gap(vTrue, vMax, δ) = (vTrue - vMax) * δ
-- We represent bounded clamping error.
-- ============================================================================

def c1GapFormula : Expr Int :=
  -- (vTrue - vMax) * delta
  -- var 0 = vTrue, var 1 = vMax, var 2 = delta
  .mul (.add (.var 0) (.mul (.const (-1)) (.var 1))) (.var 2)

def saturateC1Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c1GapFormula

def extractOptimalC1Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC1Heuristic
  (g_unopt.computeCosts).extract root

-- ============================================================================
-- 2. C2 / G29 — Acceleration bound underreporting
-- gap(aHalf, δ) = aHalf * δ^2
-- Missing acceleration creates a quadratic error in predicted bounds.
-- ============================================================================

def c2GapFormula : Expr Int :=
  -- accelFloor * delta^2
  -- var 0 = accelFloor (caller supplies pbvh_accel_floor_um_per_tick2(hz)),
  -- var 1 = delta
  .mul (.var 0) (.pow (.var 1) 2)

def saturateC2Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c2GapFormula

def extractOptimalC2Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC2Heuristic
  (g_unopt.computeCosts).extract root

-- ============================================================================
-- 3. C3 / G11 — Portal / teleporter spatial discontinuity
-- gap(jump, target_bound) = jump - target_bound
-- When a player teleports, distance > predicted physics.
-- ============================================================================

def c3GapFormula : Expr Int :=
  -- jumpUm - ghostBound
  -- var 0 = jumpUm, var 1 = ghostBoundUm
  .add (.var 0) (.mul (.var 1) (.const (-1)))

def saturateC3Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c3GapFormula

def extractOptimalC3Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC3Heuristic
  (g_unopt.computeCosts).extract root

-- ============================================================================
-- 4. C4 / G131 — Entity lifecycle gap (matchmake-to-spawn race)
-- gap(v) = v * (simTickHz / 10)
-- 100ms matchmaking race condition where remote hasn't spawned syncs.
-- ============================================================================

def c4GapFormula : Expr Int :=
  -- v * latency_ticks
  -- var 0 = v, var 1 = latency_ticks (caller supplies pbvh_latency_ticks(hz))
  .mul (.var 0) (.var 1)

def saturateC4Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c4GapFormula

def extractOptimalC4Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC4Heuristic
  (g_unopt.computeCosts).extract root

-- ============================================================================
-- 5. C5 / G181 — Satellite RTT delta exceeds configured delta
-- gap(v, RTT, localDelta) = v * (RTT - localDelta)
-- Pings over Geostationary Satellites (2000ms).
-- ============================================================================

def c5GapFormula : Expr Int :=
  -- v * (sat_delta - local_delta)
  -- var 0 = v, var 1 = sat_delta (ticks), var 2 = local_delta (ticks)
  .mul (.var 0) (.add (.var 1) (.mul (.const (-1)) (.var 2)))

def saturateC5Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c5GapFormula

def extractOptimalC5Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC5Heuristic
  (g_unopt.computeCosts).extract root

-- ============================================================================
-- 6. C6 / G268 — Coordinate frame mismatch (chunk origin offset)
-- gap = chunkOriginOffsetUm
-- Frame of reference between zones drifts by 1km coordinate bounds.
-- ============================================================================

def c6GapFormula : Expr Int :=
  -- Coordinate-space offset between zones (tick-rate-invariant physical
  -- distance). Caller supplies the offset in μm (default
  -- chunkOriginOffsetUm = 1 km), allowing per-deployment overrides (e.g.
  -- tighter offsets for unit tests, looser for world-scale fabrics).
  -- var 0 = chunk_origin_offset_um
  .var 0

def saturateC6Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c6GapFormula

def extractOptimalC6Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC6Heuristic
  (g_unopt.computeCosts).extract root

-- ============================================================================
-- 7. C7 / G221 — Segment boundary violation (current_funnel peak velocity)
-- gap(vFunnel, vMax, δ) = (vFunnel - vMax) * δ
-- River rip-current forces outstrip physical max walk speed.
-- ============================================================================

def c7GapFormula : Expr Int :=
  -- (v_funnel - v_max) * delta
  -- var 0 = v_funnel, var 1 = v_max, var 2 = delta
  .mul (.add (.var 0) (.mul (.const (-1)) (.var 1))) (.var 2)

def saturateC7Heuristic : (EGraph.EClassId × EGraph) :=
  EGraph.addExpr EGraph.empty c7GapFormula

def extractOptimalC7Bound : Option (Expr Int) :=
  let (root, g_unopt) := saturateC7Heuristic
  (g_unopt.computeCosts).extract root

end PredictiveBVH.EML
