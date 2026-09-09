-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import PredictiveBVH.Primitives.Types
import PredictiveBVH.Formulas.Formula

-- ============================================================================
-- SAH COST MODEL
--
-- `PartitionNode` itself is declared upstream in
-- `PredictiveBVH.Primitives.Types`. This module adds the SAH cost evaluator.
-- ============================================================================

/-- Predictive SAH cost for a leaf: surface area × traversal cost + predictive expansion.
    This is the cost used by the E-graph to select optimal partitions. -/
def predictiveSAH (data : LeafData) : Int :=
  bvhTraversalCost * surfaceArea data.bounds

/-- Full SAH cost evaluator for a partition node; returns `none` when any child EClassId is out of bounds.
    Traversal cost = surfaceArea(parent); leaf cost = predictiveSAH. -/
def evalNodeCost? (node : PartitionNode) (classes : Array EClass) : Option Int :=
  match node with
  | .none_split d => some (predictiveSAH d)
  | .horz p t b =>
    if t < classes.size ∧ b < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[t]!.minCost + classes[b]!.minCost)
    else none
  | .vert p l r =>
    if l < classes.size ∧ r < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[l]!.minCost + classes[r]!.minCost)
    else none
  | .depth p f b =>
    if f < classes.size ∧ b < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[f]!.minCost + classes[b]!.minCost)
    else none
  | .horz_a p tl tr b =>
    if tl < classes.size ∧ tr < classes.size ∧ b < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[tl]!.minCost + classes[tr]!.minCost + classes[b]!.minCost)
    else none
  | .vert_b p l tr br =>
    if l < classes.size ∧ tr < classes.size ∧ br < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[l]!.minCost + classes[tr]!.minCost + classes[br]!.minCost)
    else none
  | .xz_a p fl fr b =>
    if fl < classes.size ∧ fr < classes.size ∧ b < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[fl]!.minCost + classes[fr]!.minCost + classes[b]!.minCost)
    else none
  | .horz_4 p s1 s2 s3 s4 =>
    if s1 < classes.size ∧ s2 < classes.size ∧ s3 < classes.size ∧ s4 < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[s1]!.minCost + classes[s2]!.minCost +
            classes[s3]!.minCost + classes[s4]!.minCost)
    else none
  | .vert_4 p s1 s2 s3 s4 =>
    if s1 < classes.size ∧ s2 < classes.size ∧ s3 < classes.size ∧ s4 < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[s1]!.minCost + classes[s2]!.minCost +
            classes[s3]!.minCost + classes[s4]!.minCost)
    else none
  | .depth_4 p s1 s2 s3 s4 =>
    if s1 < classes.size ∧ s2 < classes.size ∧ s3 < classes.size ∧ s4 < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[s1]!.minCost + classes[s2]!.minCost +
            classes[s3]!.minCost + classes[s4]!.minCost)
    else none
  | .oct p s1 s2 s3 s4 s5 s6 s7 s8 =>
    if s1 < classes.size ∧ s2 < classes.size ∧ s3 < classes.size ∧ s4 < classes.size ∧
       s5 < classes.size ∧ s6 < classes.size ∧ s7 < classes.size ∧ s8 < classes.size then
      some (bvhTraversalCost * surfaceArea p + classes[s1]!.minCost + classes[s2]!.minCost +
            classes[s3]!.minCost + classes[s4]!.minCost + classes[s5]!.minCost + classes[s6]!.minCost +
            classes[s7]!.minCost + classes[s8]!.minCost)
    else none
