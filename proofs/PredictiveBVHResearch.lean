-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

-- Aspirational/research-tier proofs. Module sources live under `PredictiveBVH/`
-- alongside production files (no path conflict — name suffixes are unique),
-- but this aggregator stays separate so `lake build PredictiveBVHResearch`
-- targets only the research-tier closure. These modules are NOT load-bearing
-- for the production C codegen header (`predictive_bvh.h`); they encode
-- model-level claims about the abstract BVH, migration protocol, and
-- authorization logic, and some are currently broken under Lean 4.26.
-- See README.md for the repair roadmap.
import PredictiveBVH.Spatial.Partition
import PredictiveBVH.Protocol.Saturate
import PredictiveBVH.Protocol.Fabric
import PredictiveBVH.Interest.AuthorityInterest
import PredictiveBVH.Relativistic.ReBAC
