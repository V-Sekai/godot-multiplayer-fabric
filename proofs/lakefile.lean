-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import Lake
open System Lake DSL

package «optimal-partition» where

-- Dependency for flat ECS-style E-node and E-class storage patterns
require «truth_research_zk» from git
  "https://github.com/V-Sekai-fire/truth_research_zk.git" @ "add-i64-r128-emitters-fix-mapscalar"

lean_lib «PredictiveBVH» where
  roots := #[`PredictiveBVH]

lean_lib Lasso where
  roots := #[`Lasso.Mapping, `Lasso.InputDelivery]

lean_lib WorkQueue where
  roots := #[`WorkQueue.Reachability]

-- Research-tier proofs (Spatial.{Partition, Tree, RefitIncremental},
-- Protocol.{Saturate, Fabric}, Interest.AuthorityInterest,
-- Relativistic.ReBAC). Module sources live under `PredictiveBVH/` alongside
-- production files; this aggregator pins the research-tier import closure.
lean_lib «PredictiveBVHResearch» where
  roots := #[`PredictiveBVHResearch]

-- AmoLean C code generator: writes thirdparty/predictive_bvh/predictive_bvh.h
@[default_target]
lean_exe «bvh-codegen» where
  root := `PredictiveBVH.Codegen.CodeGen
  supportInterpreter := true
