-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- CASSIE-side wrapper that re-exports the verified building blocks the
-- vendored Cloth library supplies and pins decidable properties of the
-- CASSIE kernel composition via `native_decide`.
--
-- No `sorry` and no placeholder theorems. Anything stated below is
-- either re-exported from `Cloth.Avbd` (already `native_decide`'d
-- upstream against fixture references) or pinned here with
-- `native_decide` against literal kernel-list properties.

import Cloth.Avbd
import Cloth.SlangCodegen.AttachmentDualUpdate
import Cloth.SlangCodegen.AttachmentProject
import Cloth.SlangCodegen.CGAlpha
import Cloth.SlangCodegen.CGBeta
import Cloth.SlangCodegen.DotReduce
import Cloth.SlangCodegen.Saxpby
import Cloth.SlangCodegen.Spmv
import Cloth.SlangCodegen.SpringForce

open Cloth.SlangCodegen
open LeanSlang

namespace CassieAvbd

/-- The CASSIE-relevant subset of the `Cloth.SlangCodegen.*` kernels —
    same list `Codegen.lean` emits. Re-exported here as the canonical
    name the `native_decide` fixtures pin against. -/
def kernelNames : List String :=
  [ "spmv"
  , "saxpby"
  , "dot_reduce"
  , "cg_alpha"
  , "cg_beta"
  , "attachment_dual_update"
  , "spring_force"
  , "attachment_project"
  ]

/-- Every kernel CASSIE emits comes with a `main` Slang entry point —
    this is the contract `RenderingDevice::shader_create_from_spirv`
    relies on. Pinned via `native_decide` against each shader's
    `entryPointName` field. -/
example : Spmv.shader.entryPointName = "main"                 := by native_decide
example : Saxpby.shader.entryPointName = "main"               := by native_decide
example : DotReduce.shader.entryPointName = "main"            := by native_decide
example : CGAlpha.shader.entryPointName = "main"              := by native_decide
example : CGBeta.shader.entryPointName = "main"               := by native_decide
example : AttachmentDualUpdate.shader.entryPointName = "main" := by native_decide
example : SpringForce.shader.entryPointName = "main"          := by native_decide
example : AttachmentProject.shader.entryPointName = "main"    := by native_decide

/-- The kernel name list has exactly the expected length. Lets the C++
    side trust the count when loading the emitted shader files. -/
example : kernelNames.length = 8 := by native_decide

end CassieAvbd
