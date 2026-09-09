-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

-- Core BVH algorithm
import PredictiveBVH.Primitives.Types
import PredictiveBVH.Formulas.Formula
import PredictiveBVH.Spatial.HilbertBroadphase
import PredictiveBVH.Spatial.HilbertRoundtrip
import PredictiveBVH.Spatial.BucketBound
import PredictiveBVH.Formulas.LowerBound
import PredictiveBVH.Protocol.WaypointBound

-- Scale and capacity proofs
import PredictiveBVH.Formulas.ScaleProofs
import PredictiveBVH.Spatial.ScaleContradictions
import PredictiveBVH.Spatial.EMLAdversarialHeuristic
import PredictiveBVH.Protocol.ScaleContradictionsGapClass
import PredictiveBVH.Protocol.AbyssalSLA

-- Relativistic zone theory (no ego, no god, no determinism)
import PredictiveBVH.Relativistic.NoGod

-- Resources
import PredictiveBVH.Formulas.Resources

-- Adapter formalization (bridges to predictive_bvh_adapter.h)
import PredictiveBVH.Codegen.RingOps
import PredictiveBVH.Spatial.BucketDir
import PredictiveBVH.Spatial.HilbertCell

-- Code generation pipeline
import PredictiveBVH.Codegen.QuinticHermite
import PredictiveBVH.Codegen.TreeC
import PredictiveBVH.Codegen.CodeGen

-- Research-tier proofs live in the sibling `PredictiveBVHResearch` lib
-- (root file: PredictiveBVHResearch.lean). They are not in the codegen
-- import closure; build them with `lake build PredictiveBVHResearch`.
