import LeanSlang

/-!
# `CassieAvbd.MasPreconditionerSerial` — CPU sibling of MasPreconditioner

slangc's `-target cpp` backend can't lower `GroupMemoryBarrierWithGroupSync`
(error E36107). This module is a sibling of `MasPreconditioner.lean`
with the `mas_per_domain_solve` entry replaced by a non-groupshared
version that reads `r_input` directly from global memory per the
Row 6 layout (`01925dbddd`).

The other two entries (`mas_coarsen_residual`, `mas_sum_levels`) don't
use groupshared and are byte-equal to the parallel module.

Registered in Codegen as the `cpuShader` for the `mas_precond`
ubershader. Same paper §7.1 packed lower-triangular layout for M⁻¹;
same symmetry-aware addressing. Produces the same numerical result
as the parallel module for any SPD M.
-/

namespace CassieAvbd.MasPreconditionerSerial

open LeanSlang

private def floatTy  : SlangType := .scalar .float
private def uintTy   : SlangType := .scalar .uint
private def float3Ty : SlangType := .vec .float 3

private def paramsStruct : SlangStructDecl :=
  { name    := "MasParams"
  , fields  :=
      [ ⟨"ni",          uintTy,  Semantic.none, none, none, .qIn⟩
      , ⟨"num_levels",  uintTy,  Semantic.none, none, none, .qIn⟩
      , ⟨"domain_size", uintTy,  Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_min_x",  floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_min_y",  floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_min_z",  floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_size_x", floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_size_y", floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_size_z", floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level",       uintTy,  Semantic.none, none, none, .qIn⟩
      , ⟨"level_r_offset",          uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level_z_offset",          uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level_domain_offset",     uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level_ni",                uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"total_coarse_supernodes", uintTy, Semantic.none, none, none, .qIn⟩ ] }

private def globals : List SlangBinding :=
  [ ⟨"params",         .const "MasParams",         Semantic.none, some 0,  some 0, .qIn⟩
  , ⟨"rowPtr",         .roBuf (.scalar .int),      Semantic.none, some 1,  some 0, .qIn⟩
  , ⟨"colIdx",         .roBuf (.scalar .int),      Semantic.none, some 2,  some 0, .qIn⟩
  , ⟨"values",         .roBuf floatTy,             Semantic.none, some 3,  some 0, .qIn⟩
  , ⟨"morton",         .rwBuf uintTy,              Semantic.none, some 4,  some 0, .qIn⟩
  , ⟨"sorted_idx",     .roBuf (.scalar .int),      Semantic.none, some 5,  some 0, .qIn⟩
  , ⟨"map_per_level",  .rwBuf (.scalar .int),      Semantic.none, some 6,  some 0, .qIn⟩
  , ⟨"domain_offsets", .roBuf uintTy,              Semantic.none, some 7,  some 0, .qIn⟩
  , ⟨"m_inv_packed",   .rwBuf floatTy,             Semantic.none, some 8,  some 0, .qIn⟩
  , ⟨"r_per_level",    .rwBuf float3Ty,            Semantic.none, some 9,  some 0, .qIn⟩
  , ⟨"z_per_level",    .rwBuf float3Ty,            Semantic.none, some 10, some 0, .qIn⟩
  , ⟨"r_input",        .roBuf float3Ty,            Semantic.none, some 11, some 0, .qIn⟩
  , ⟨"z_output",       .rwBuf float3Ty,            Semantic.none, some 12, some 0, .qIn⟩
  , ⟨"coarse_offsets", .roBuf uintTy,              Semantic.none, some 13, some 0, .qIn⟩
  , ⟨"coarse_indices", .roBuf (.scalar .int),      Semantic.none, some 14, some 0, .qIn⟩
  , ⟨"level_sizes",    .roBuf uintTy,              Semantic.none, some 15, some 0, .qIn⟩
  , ⟨"positions",      .roBuf float3Ty,            Semantic.none, some 16, some 0, .qIn⟩
  , ⟨"connect_mask",   .rwBuf uintTy,              Semantic.none, some 17, some 0, .qIn⟩
  , ⟨"dense_workspace", .rwBuf floatTy,             Semantic.none, some 18, some 0, .qIn⟩ ]

/-- Byte-equal to MasPreconditioner.coarsenResidualEntry. -/
private def coarsenResidualEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_coarsen_residual"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "s" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "s")
                    (.member (.var "params") "total_coarse_supernodes"))
          [ .ret none ]
      , .declInit uintTy "start" (.index (.var "coarse_offsets") (.var "s"))
      , .declInit uintTy "end"
          (.index (.var "coarse_offsets") (.bin "+" (.var "s") (.litUint 1)))
      , .declInit floatTy "acc_x" (.litFloat 0.0)
      , .declInit floatTy "acc_y" (.litFloat 0.0)
      , .declInit floatTy "acc_z" (.litFloat 0.0)
      , .forCount "k" (.var "start") (.var "end")
          [ .declInit uintTy "fine_vert"
              (.call "uint" [.index (.var "coarse_indices") (.var "k")])
          , .declInit float3Ty "ri" (.index (.var "r_per_level") (.var "fine_vert"))
          , .assign (.var "acc_x")
              (.bin "+" (.var "acc_x") (.member (.var "ri") "x"))
          , .assign (.var "acc_y")
              (.bin "+" (.var "acc_y") (.member (.var "ri") "y"))
          , .assign (.var "acc_z")
              (.bin "+" (.var "acc_z") (.member (.var "ri") "z")) ]
      , .assign (.index (.var "r_per_level")
                  (.bin "+" (.member (.var "params") "ni") (.var "s")))
          (.call "float3" [.var "acc_x", .var "acc_y", .var "acc_z"])
      , .ret none ] }

/-- Identity-copy r_input → r_per_level[0..ni) for level 0. Byte-equal
    to MasPreconditioner.identityCopyL0Entry. -/
private def identityCopyL0Entry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_identity_copy_l0"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "ni"))
          [ .ret none ]
      , .assign (.index (.var "r_per_level") (.var "i"))
          (.index (.var "r_input") (.var "i"))
      , .ret none ] }

/-- CPU-safe per-domain SymMV — same algorithm as Row 6
    (`01925dbddd`): packed lower-triangular layout with symmetry-aware
    addressing, reads r_input directly from global (no groupshared, no
    barriers). slangc -target cpp serializes the workgroup into a
    per-thread for-loop. -/
private def perDomainSolveEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 32 1 1]
  , name   := "mas_per_domain_solve"
  , params :=
      [ ⟨"gid", .vec .uint 3, .svGroupId,       none, none, .qIn⟩
      , ⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩ ]
  , body   :=
      [ .declInit uintTy "domain" (.member (.var "gid") "x")
      , .declInit uintTy "lane"   (.member (.var "tid") "x")
      , .declInit uintTy "sigma"  (.member (.var "params") "domain_size")
      , .declInit uintTy "vert"
          (.bin "+" (.bin "*" (.var "domain") (.var "sigma")) (.var "lane"))
      , .ifNoElse (.bin ">=" (.var "vert") (.member (.var "params") "level_ni"))
          [ .ret none ]
      , .declInit uintTy "global_domain"
          (.bin "+" (.member (.var "params") "level_domain_offset")
                    (.var "domain"))
      , .declInit uintTy "domain_base"
          (.index (.var "domain_offsets") (.var "global_domain"))
      , .declInit uintTy "row_base"
          (.bin "+" (.var "domain_base")
            (.bin ">>"
              (.bin "*" (.var "lane")
                (.bin "+" (.var "lane") (.litUint 1)))
              (.litUint 1)))
      , .declInit floatTy "acc_x" (.litFloat 0.0)
      , .declInit floatTy "acc_y" (.litFloat 0.0)
      , .declInit floatTy "acc_z" (.litFloat 0.0)
      , .forCount "j" (.litUint 0) (.var "sigma")
          -- Skip columns past level_ni in the last incomplete domain.
          [ .declInit uintTy "other_vert"
              (.bin "+" (.bin "*" (.var "domain") (.var "sigma")) (.var "j"))
          , .ifNoElse (.bin "<" (.var "other_vert")
                        (.member (.var "params") "level_ni"))
              [ .declInit uintTy "addr"
                  (.ternary (.bin "<=" (.var "j") (.var "lane"))
                    (.bin "+" (.var "row_base") (.var "j"))
                    (.bin "+"
                      (.bin "+" (.var "domain_base")
                        (.bin ">>"
                          (.bin "*" (.var "j")
                            (.bin "+" (.var "j") (.litUint 1)))
                          (.litUint 1)))
                      (.var "lane")))
              , .declInit floatTy "m" (.index (.var "m_inv_packed") (.var "addr"))
              , .declInit float3Ty "ri"
                  (.index (.var "r_per_level")
                    (.bin "+" (.member (.var "params") "level_r_offset")
                              (.var "other_vert")))
              , .assign (.var "acc_x")
                  (.call "fma" [.var "m", .member (.var "ri") "x", .var "acc_x"])
              , .assign (.var "acc_y")
                  (.call "fma" [.var "m", .member (.var "ri") "y", .var "acc_y"])
              , .assign (.var "acc_z")
                  (.call "fma" [.var "m", .member (.var "ri") "z", .var "acc_z"]) ] ]
      , .assign (.index (.var "z_per_level")
                  (.bin "+" (.member (.var "params") "level_z_offset")
                            (.var "vert")))
          (.call "float3" [.var "acc_x", .var "acc_y", .var "acc_z"])
      , .ret none ] }

/-- Byte-equal to MasPreconditioner.sumLevelsEntry. -/
private def sumLevelsEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_sum_levels"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "ni"))
          [ .ret none ]
      , .declInit float3Ty "acc"
          (.index (.var "z_per_level") (.var "i"))
      , .declInit uintTy "off" (.member (.var "params") "ni")
      , .declInit uintTy "L"   (.member (.var "params") "num_levels")
      , .forCount "l" (.litUint 1) (.var "L")
          [ .declInit uintTy "map_base"
              (.bin "*" (.bin "-" (.var "l") (.litUint 1))
                        (.member (.var "params") "ni"))
          , .declInit uintTy "parent"
              (.call "uint"
                [.index (.var "map_per_level")
                  (.bin "+" (.var "map_base") (.var "i"))])
          , .declInit float3Ty "zl"
              (.index (.var "z_per_level")
                (.bin "+" (.var "off") (.var "parent")))
          , .assign (.var "acc")
              (.call "float3"
                [ .bin "+" (.member (.var "acc") "x") (.member (.var "zl") "x")
                , .bin "+" (.member (.var "acc") "y") (.member (.var "zl") "y")
                , .bin "+" (.member (.var "acc") "z") (.member (.var "zl") "z") ])
          , .assign (.var "off")
              (.bin "+" (.var "off")
                (.index (.var "level_sizes") (.var "l"))) ]
      , .assign (.index (.var "z_output") (.var "i")) (.var "acc")
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared := []  -- no shared memory, no barriers — CPU-target safe
  , globals   := globals
  , functions :=
      [ identityCopyL0Entry
      , coarsenResidualEntry
      , perDomainSolveEntry
      , sumLevelsEntry ] }

example : shader.entryPointNames =
    [ "mas_identity_copy_l0"
    , "mas_coarsen_residual", "mas_per_domain_solve", "mas_sum_levels" ] := by
  native_decide
example : shader.entryPoints.length = 4 := by native_decide

end CassieAvbd.MasPreconditionerSerial
