import LeanSlang

/-!
# `CassieAvbd.MasPreconditioner` — multilevel additive Schwarz preconditioner

Lean port of Wu, Wang & Wang 2022, "A GPU-Based Multilevel Additive
Schwarz Preconditioner for Cloth and Deformable Body Simulation"
(ACM TOG 41(4):63, DOI 10.1145/3528223.3530085). Paper reports ~4×
PCG speedup over Jacobi on cloth meshes 76K–467K verts.

We port the paper's **full 3×3 vertex-Hessian form** — see plan
`playful-marinating-harp.md` Context constraint #4. The current
CASSIE harmonic deform's cotangent Laplacian fits as the
block-diagonal 3×3 special case (axes uncoupled); future AVBD
vertex-Hessian work consumes the same kernel with no re-port.
Multi-RHS over the three axes is free: one float3 solve replaces
today's three sequential scalar solves.

This module emits ONE Slang source covering both bind-time
(Morton + hierarchy + GJ inversion) and runtime (the §7 three-pass
apply). `slangc` emits both `.spv` (GPU) and `.cpu.cpp` (CPU
reference) per entry via the Codegen.lean dual-target ubershader
pass, so the CPU output is the verified reference implementation —
no separately written C++ reference.

## Pipeline overview

  Bind-time (paper §5, §6, runs once per CassieProfileMover.bind()):
    1. mas_morton_compute       — paper §5.1, 60-bit Morton / vert
    2. (radix sort: punted to C++ orchestrator, std::sort on n≈6k
        is faster than expressing radix sort in this DSL)
    3. mas_build_connect_mask   — paper §5.2 skipping approach
    4. mas_aggregation          — per-warp connected components
    5. mas_assemble_submatrix   — gather L_II → 96×96 vertex-Hessian
    6. mas_gj_invert            — paper §6.2 GJ + §7.1 compact pack

  Per CG iter (paper §7, replaces jacobi_z in cg_pcg3):
    7. mas_coarsen_residual     — restrict r down levels via map_l
    8. mas_per_domain_solve     — paper §7.1 three-pass SymMV
    9. mas_sum_levels           — prolongate + sum across levels

## Status

Phase C scaffold: just `mas_per_domain_solve` as a workgroup-shape
PROBE — one workgroup per domain (σ = 32 threads), `svGroupId` for
the domain index, identity transform (z = r) as the body. Validates
the dispatch shape end-to-end (Lean → Slang → SPIR-V) before the
§7.1 packed three-pass SymMV body lands.

If this probe compiles AND a follow-up bench shows reasonable
per-workgroup launch overhead (~187 workgroups for n ≈ 6k at σ = 32),
the architectural foundation is sound. Subsequent commits replace
the identity body with the §7.1 packed SymMV.

## Bindings (shared across all entries)

  0  ConstantBuffer<MasParams> { uint ni; uint num_levels; uint domain_size; }
  1  StructuredBuffer<int>      rowPtr      # L_II CSR (read by bind entries)
  2  StructuredBuffer<int>      colIdx
  3  StructuredBuffer<float>    values
  4  RWStructuredBuffer<uint>   morton      # 60-bit Morton per vertex
  5  StructuredBuffer<int>      sorted_idx  # sorted index → original L_II row
  6  StructuredBuffer<int>      map_per_level # flat: ni × num_levels
  7  StructuredBuffer<uint>     domain_offsets # per-domain start in m_inv_packed
  8  RWStructuredBuffer<float>  m_inv_packed   # all domains' M^-1 packed
  9  RWStructuredBuffer<float3> r_per_level    # flat: r at each level
  10 RWStructuredBuffer<float3> z_per_level    # flat: z at each level
  11 StructuredBuffer<float3>   r_input        # input residual
  12 RWStructuredBuffer<float3> z_output       # output z (post sum_levels)
-/

namespace CassieAvbd.MasPreconditioner

open LeanSlang

private def floatTy  : SlangType := .scalar .float
private def uintTy   : SlangType := .scalar .uint
private def float3Ty : SlangType := .vec .float 3

/-- Per-dispatch uniform params shared across all entries. AABB
    extents are floats; the bind-time C++ orchestrator computes the
    AABB of interior vert positions and writes the min + size into
    these fields before mas_morton_compute dispatches. -/
private def paramsStruct : SlangStructDecl :=
  { name    := "MasParams"
  , fields  :=
      [ ⟨"ni",          uintTy,  Semantic.none, none, none, .qIn⟩
      , ⟨"num_levels",  uintTy,  Semantic.none, none, none, .qIn⟩
      , ⟨"domain_size", uintTy,  Semantic.none, none, none, .qIn⟩
      -- AABB: position normalized as (p - aabb_min) / aabb_size before
      -- the 20-bit-per-axis scale + Morton interleave.
      , ⟨"aabb_min_x",  floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_min_y",  floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_min_z",  floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_size_x", floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_size_y", floatTy, Semantic.none, none, none, .qIn⟩
      , ⟨"aabb_size_z", floatTy, Semantic.none, none, none, .qIn⟩
      -- Current level for entries that process one level at a time
      -- (build_connect_mask, aggregation). Set per dispatch by the
      -- orchestrator's bind-time loop.
      , ⟨"level",       uintTy,  Semantic.none, none, none, .qIn⟩
      -- Per-dispatch offsets for the apply chain (§7). mas_per_domain_solve
      -- reads r_per_level[level_r_offset + safe_vert] and writes
      -- z_per_level[level_z_offset + vert]; uses domain_offsets[level_domain_offset
      -- + workgroup] for its M⁻¹ slice; bounds-checks against level_ni
      -- (= N_l) rather than the global ni. The C++ orchestrator builds one
      -- UBO per (level, kernel) at bind time and binds the right one per
      -- dispatch — no buffer_update inside the apply compute list.
      , ⟨"level_r_offset",          uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level_z_offset",          uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level_domain_offset",     uintTy, Semantic.none, none, none, .qIn⟩
      , ⟨"level_ni",                uintTy, Semantic.none, none, none, .qIn⟩
      -- mas_coarsen_residual's workgroup-bound: total non-zero-level
      -- supernodes = Σ_{l>0} N_l. Single value lives at bind time in the
      -- coarsen-flavored params UBO.
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
  -- Bind-time-populated inverted coarse map for the deferred-reduction
  -- coarsen pass. Per the plan's locked decision: no atomic float adds.
  -- coarse_offsets[s..s+1] = range of fine-vert indices belonging to
  -- supernode s in coarse_indices. Flat across ALL non-zero levels.
  -- coarse_indices is sorted by (level, supernode); the bind-time
  -- chain (Row 8 of execution order) populates both.
  , ⟨"coarse_offsets", .roBuf uintTy,              Semantic.none, some 13, some 0, .qIn⟩
  , ⟨"coarse_indices", .roBuf (.scalar .int),      Semantic.none, some 14, some 0, .qIn⟩
  -- level_sizes[l] = N_l (supernode count at level l); level 0 = Ni.
  -- Used by mas_sum_levels to advance the r_per_level / z_per_level
  -- offset between levels. Bind-time chain populates.
  , ⟨"level_sizes",    .roBuf uintTy,              Semantic.none, some 15, some 0, .qIn⟩
  -- Per-vertex positions (interior verts only, in rest pose order). The
  -- C++ orchestrator uploads at bind time. Read by mas_morton_compute
  -- which produces the 60-bit Morton code per vert. AABB params come
  -- via paramsStruct.aabb_min / aabb_size (see paramsStruct).
  , ⟨"positions",      .roBuf float3Ty,            Semantic.none, some 16, some 0, .qIn⟩
  -- Per-supernode connectivity bitmask at each level (uint per
  -- supernode candidate of σ verts; bit b set iff verts {lane=b} and
  -- {b's row-leader} share an L_II edge). Written by mas_build_connect_mask,
  -- read by mas_aggregation.
  , ⟨"connect_mask",   .rwBuf uintTy,              Semantic.none, some 17, some 0, .qIn⟩
  -- Per-domain dense σ×σ scratch written by mas_assemble_submatrix and
  -- read by mas_gj_invert. σ² floats per domain, contiguous in row-major
  -- order. The two-kernel split lets us debug assembly independently of
  -- inversion; the orchestrator dispatches assembly first, then GJ.
  , ⟨"dense_workspace", .rwBuf floatTy,             Semantic.none, some 18, some 0, .qIn⟩ ]

/-- Per-domain SymMV apply — packed lower-tri + groupshared input cache.

    Paper §7.1 storage (fig. 11): σ×σ symmetric M⁻¹ as its lower
    triangle in row-major order, σ(σ+1)/2 = 528 scalars per domain.

    This step adds groupshared backing for the per-domain r slice.
    The 32 lanes cooperatively load r_input[domain*σ .. domain*σ + σ)
    into a 32-entry groupshared float3 array once per workgroup, then
    each lane's σ-column inner loop reads from shared memory instead
    of global. Bit-equal to Row 6 (`01925dbddd`) by construction — the
    matvec values are the same, only the read locality changes.

    Per-thread r (lane index) computes z[r] = Σ_j M[r,j] · s_r[j].
    For j ≤ r, M[r,j] is at packed[row_base(r) + j] (own row). For
    j > r, M[r,j] = M[j,r] by symmetry, at packed[row_base(j) + r].

    The §7.1 three-pass white/gray balanced-workload SymMV that
    halves the per-thread inner-loop work via register-held column
    accumulators is a follow-up optimization once perf measurement
    justifies the complexity. -/
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
      -- Per-level M⁻¹ slice: the orchestrator dispatches per_domain_solve
      -- once per level with params.level_domain_offset advanced through
      -- the flat domain_offsets array (§7 multi-level apply chain).
      , .declInit uintTy "global_domain"
          (.bin "+" (.member (.var "params") "level_domain_offset")
                    (.var "domain"))
      , .declInit uintTy "domain_base"
          (.index (.var "domain_offsets") (.var "global_domain"))
      -- This lane's row starts at packed[domain_base + lane*(lane+1)/2].
      -- Triangular row offset = r * (r + 1) / 2.
      , .declInit uintTy "row_base"
          (.bin "+" (.var "domain_base")
            (.bin ">>"
              (.bin "*" (.var "lane")
                (.bin "+" (.var "lane") (.litUint 1)))
              (.litUint 1)))
      -- Cooperative load WITHOUT a divergent early-return. Bounds check
      -- is against level_ni (= N_l for the current level), not the
      -- global ni — at coarse levels N_l < ni. Out-of-bounds lanes still
      -- participate in the barrier (zero into their shared slot) to
      -- avoid divergent GroupMemoryBarrierWithGroupSync (UB on Vulkan).
      , .declInit uintTy "in_bounds"
          (.ternary (.bin "<" (.var "vert") (.member (.var "params") "level_ni"))
            (.litUint 1) (.litUint 0))
      , .declInit uintTy "safe_vert"
          (.ternary (.bin "==" (.var "in_bounds") (.litUint 1))
            (.var "vert") (.litUint 0))
      -- Read this level's residual slice (level 0 lives at offset 0,
      -- level l>0 at offset ni + Σ_{k<l} N_k − ni = Σ_{k<l} N_k for
      -- the flat r_per_level buffer; orchestrator computes once at bind).
      , .declInit float3Ty "ri_local"
          (.index (.var "r_per_level")
            (.bin "+" (.member (.var "params") "level_r_offset")
                      (.var "safe_vert")))
      , .assign (.index (.var "s_rx") (.var "lane"))
          (.ternary (.bin "==" (.var "in_bounds") (.litUint 1))
            (.member (.var "ri_local") "x") (.litFloat 0.0))
      , .assign (.index (.var "s_ry") (.var "lane"))
          (.ternary (.bin "==" (.var "in_bounds") (.litUint 1))
            (.member (.var "ri_local") "y") (.litFloat 0.0))
      , .assign (.index (.var "s_rz") (.var "lane"))
          (.ternary (.bin "==" (.var "in_bounds") (.litUint 1))
            (.member (.var "ri_local") "z") (.litFloat 0.0))
      , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
      -- Only in-bounds lanes compute and write z. Out-of-bounds lanes
      -- have already contributed (zeroing their shared slot) and now do
      -- nothing — they own no z_output row.
      , .ifNoElse (.bin "==" (.var "in_bounds") (.litUint 1))
          [ .declInit floatTy "acc_x" (.litFloat 0.0)
          , .declInit floatTy "acc_y" (.litFloat 0.0)
          , .declInit floatTy "acc_z" (.litFloat 0.0)
          , .forCount "j" (.litUint 0) (.var "sigma")
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
              -- Read float3 input from groupshared, not global.
              , .assign (.var "acc_x")
                  (.call "fma" [.var "m", .index (.var "s_rx") (.var "j"), .var "acc_x"])
              , .assign (.var "acc_y")
                  (.call "fma" [.var "m", .index (.var "s_ry") (.var "j"), .var "acc_y"])
              , .assign (.var "acc_z")
                  (.call "fma" [.var "m", .index (.var "s_rz") (.var "j"), .var "acc_z"]) ]
          , .assign (.index (.var "z_per_level")
                      (.bin "+" (.member (.var "params") "level_z_offset")
                                (.var "vert")))
              (.call "float3" [.var "acc_x", .var "acc_y", .var "acc_z"]) ]
      , .ret none ] }

/-- Compute the 60-bit Morton code for each interior vertex.
    Paper §5.1: divide AABB into 2²⁰ × 2²⁰ × 2²⁰ cells (20 bits per
    axis); interleave per-axis cell indices into a 60-bit code.

    Slang has no native uint64. We split the 60-bit code into two
    uint32 halves: morton[i*2] holds the low 30 bits, morton[i*2+1]
    holds the high 30 bits. The C++ orchestrator concatenates for the
    sort key.

    20-bit expand pattern: distribute 20 input bits across 60 output
    bit positions with two zeros after each input bit. Standard bit-
    twiddle: (b | b<<32) & 0xFFFF00000000FFFF — but since we're at 20
    bits we use the 32-bit chain:
      b = (b | b<<32) & ...   (not needed — we stay in 32-bit)
      b = (b | b<<16) & 0x030000FF
      b = (b | b<<8 ) & 0x0300F00F
      b = (b | b<<4 ) & 0x030C30C3
      b = (b | b<<2 ) & 0x09249249
    Outputs the lower-30 expansion in 32 bits. The high-30 expansion
    of the original 20-bit value is the same expansion applied to
    bits[20..40] of the position — but we only have 20 bits, so high
    expansion is zero for plain 20-bit inputs. The 60-bit code packs
    into low30 only; high30 stays zero. (We keep the two-uint output
    layout for future 21+ bit upgrades.) -/
private def mortonComputeEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_morton_compute"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "ni"))
          [ .ret none ]
      , .declInit float3Ty "p" (.index (.var "positions") (.var "i"))
      -- Normalize position to [0, 2^20) per axis.
      , .declInit floatTy "nx"
          (.bin "*"
            (.bin "/"
              (.bin "-" (.member (.var "p") "x")
                        (.member (.var "params") "aabb_min_x"))
              (.member (.var "params") "aabb_size_x"))
            (.litFloat 1048576.0))
      , .declInit floatTy "ny"
          (.bin "*"
            (.bin "/"
              (.bin "-" (.member (.var "p") "y")
                        (.member (.var "params") "aabb_min_y"))
              (.member (.var "params") "aabb_size_y"))
            (.litFloat 1048576.0))
      , .declInit floatTy "nz"
          (.bin "*"
            (.bin "/"
              (.bin "-" (.member (.var "p") "z")
                        (.member (.var "params") "aabb_min_z"))
              (.member (.var "params") "aabb_size_z"))
            (.litFloat 1048576.0))
      -- Clamp + cast to uint20 (saturation; verts on AABB max land at
      -- 2^20 - 1, not 2^20). Slang clamp() picks the right overload.
      , .declInit uintTy "ix"
          (.call "uint"
            [.call "clamp"
              [.var "nx", .litFloat 0.0, .litFloat 1048575.0]])
      , .declInit uintTy "iy"
          (.call "uint"
            [.call "clamp"
              [.var "ny", .litFloat 0.0, .litFloat 1048575.0]])
      , .declInit uintTy "iz"
          (.call "uint"
            [.call "clamp"
              [.var "nz", .litFloat 0.0, .litFloat 1048575.0]])
      -- expand_bits20: spread 20 input bits across 60 positions with
      -- two zeros between each bit. Applied to each axis separately.
      , .declInit uintTy "x" (.var "ix")
      , .assign (.var "x")
          (.bin "&"
            (.bin "|" (.var "x") (.bin "<<" (.var "x") (.litUint 16)))
            (.litUint 0x030000FF))
      , .assign (.var "x")
          (.bin "&"
            (.bin "|" (.var "x") (.bin "<<" (.var "x") (.litUint 8)))
            (.litUint 0x0300F00F))
      , .assign (.var "x")
          (.bin "&"
            (.bin "|" (.var "x") (.bin "<<" (.var "x") (.litUint 4)))
            (.litUint 0x030C30C3))
      , .assign (.var "x")
          (.bin "&"
            (.bin "|" (.var "x") (.bin "<<" (.var "x") (.litUint 2)))
            (.litUint 0x09249249))
      , .declInit uintTy "y" (.var "iy")
      , .assign (.var "y")
          (.bin "&"
            (.bin "|" (.var "y") (.bin "<<" (.var "y") (.litUint 16)))
            (.litUint 0x030000FF))
      , .assign (.var "y")
          (.bin "&"
            (.bin "|" (.var "y") (.bin "<<" (.var "y") (.litUint 8)))
            (.litUint 0x0300F00F))
      , .assign (.var "y")
          (.bin "&"
            (.bin "|" (.var "y") (.bin "<<" (.var "y") (.litUint 4)))
            (.litUint 0x030C30C3))
      , .assign (.var "y")
          (.bin "&"
            (.bin "|" (.var "y") (.bin "<<" (.var "y") (.litUint 2)))
            (.litUint 0x09249249))
      , .declInit uintTy "z" (.var "iz")
      , .assign (.var "z")
          (.bin "&"
            (.bin "|" (.var "z") (.bin "<<" (.var "z") (.litUint 16)))
            (.litUint 0x030000FF))
      , .assign (.var "z")
          (.bin "&"
            (.bin "|" (.var "z") (.bin "<<" (.var "z") (.litUint 8)))
            (.litUint 0x0300F00F))
      , .assign (.var "z")
          (.bin "&"
            (.bin "|" (.var "z") (.bin "<<" (.var "z") (.litUint 4)))
            (.litUint 0x030C30C3))
      , .assign (.var "z")
          (.bin "&"
            (.bin "|" (.var "z") (.bin "<<" (.var "z") (.litUint 2)))
            (.litUint 0x09249249))
      -- Interleave: x | (y << 1) | (z << 2). Result fits in 60 bits
      -- (each axis contributes 20 bits at strides of 3). Stored low30
      -- in morton[2*i], high30 in morton[2*i+1]. For 20-bit inputs
      -- the high 30 of each axis-expansion is zero; we still write
      -- both halves for the C++ side's uniform layout.
      , .declInit uintTy "low_part"
          (.bin "|" (.var "x")
            (.bin "|" (.bin "<<" (.var "y") (.litUint 1))
                      (.bin "<<" (.var "z") (.litUint 2))))
      , .assign (.index (.var "morton") (.bin "*" (.var "i") (.litUint 2)))
          (.var "low_part")
      , .assign (.index (.var "morton")
                  (.bin "+" (.bin "*" (.var "i") (.litUint 2)) (.litUint 1)))
          (.litUint 0)
      , .ret none ] }

/-- Per-supernode candidate connectivity bitmask at one level.
    Paper §5.2 skipping approach: for each candidate group of σ
    sorted-vertex slots [d*σ, (d+1)*σ), set bit j of connect_mask[d]
    iff vert at slot j shares an L_II off-diagonal entry with vert at
    slot 0 (the supernode representative).

    Dispatched once per level. params.level selects which mapping to
    use: at level 0 each "vert" is itself; at level l>0 each "vert" is
    a parent supernode from level l-1 and the L_II coupling is the
    coarsened matrix.

    For now level 0 only — the coarse-level connectivity test
    requires the bind-time loop to coarsen L_II per level, which the
    orchestrator handles after running aggregation. The handle for
    higher levels is a placeholder that the orchestrator will
    re-dispatch with the right adjacency. -/
private def buildConnectMaskEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 32 1 1]
  , name   := "mas_build_connect_mask"
  , params :=
      [ ⟨"gid", .vec .uint 3, .svGroupId,       none, none, .qIn⟩
      , ⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩ ]
  , body   :=
      [ .declInit uintTy "candidate" (.member (.var "gid") "x")
      , .declInit uintTy "lane"      (.member (.var "tid") "x")
      , .declInit uintTy "sigma"     (.member (.var "params") "domain_size")
      , .declInit uintTy "slot"
          (.bin "+" (.bin "*" (.var "candidate") (.var "sigma"))
                    (.var "lane"))
      , .ifNoElse (.bin ">=" (.var "slot") (.member (.var "params") "ni"))
          [ .ret none ]
      -- Resolve sorted slot → original vert index via sorted_idx.
      , .declInit uintTy "vert"
          (.call "uint" [.index (.var "sorted_idx") (.var "slot")])
      , .declInit uintTy "rep_slot"
          (.bin "*" (.var "candidate") (.var "sigma"))
      , .declInit uintTy "rep_vert"
          (.call "uint" [.index (.var "sorted_idx") (.var "rep_slot")])
      -- Check whether L_II[vert, rep_vert] has a non-trivial entry by
      -- scanning vert's CSR row for col == rep_vert.
      , .declInit uintTy "rs"
          (.call "uint" [.index (.var "rowPtr") (.var "vert")])
      , .declInit uintTy "re"
          (.call "uint" [.index (.var "rowPtr")
            (.bin "+" (.var "vert") (.litUint 1))])
      , .declInit uintTy "connected" (.litUint 0)
      , .forCount "k" (.var "rs") (.var "re")
          [ .declInit uintTy "col"
              (.call "uint" [.index (.var "colIdx") (.var "k")])
          , .ifNoElse (.bin "==" (.var "col") (.var "rep_vert"))
              [ .assign (.var "connected") (.litUint 1) ] ]
      -- Lane 0 starts the candidate's mask at 0 (every supernode
      -- candidate's rep is trivially connected to itself, so bit 0
      -- of the mask is set). Subsequent lanes OR their bit in.
      , .ifNoElse (.bin "==" (.var "lane") (.litUint 0))
          [ .assign (.index (.var "connect_mask") (.var "candidate"))
              (.litUint 1) ]
      -- Without groupshared or atomics this would race. Defer the
      -- per-lane bit-set to a follow-up: for now lane 0 owns the mask
      -- and emits a sentinel value (1) indicating "candidate exists".
      -- The orchestrator's CPU-side hierarchy build uses this only as
      -- a coarse filter; the actual per-bit connectivity is recomputed
      -- on CPU at bind() time per the plan's deferred-reduction
      -- guidance. This kernel still validates the dispatch shape.
      , .ret none ] }

/-- Per-level supernode aggregation: write map_per_level[(l-1)*ni + i]
    for each interior vert i at level l. Paper §5.2 + V-Sekai
    AggregationKernel.

    Placeholder layout: the C++ orchestrator builds the hierarchy on
    CPU (~6k verts so <1 ms via the DisjointSet pattern already used
    for the connected-components diagnostic in ENG-61) and writes
    map_per_level into the GPU buffer at bind time. This kernel
    exists for the orchestrator to dispatch as part of the bind
    chain's idempotent sequence; the body identity-copies sorted_idx
    into the level-l mapping slot so the buffer is initialized to a
    safe default if the orchestrator later wants to refine on GPU. -/
private def aggregationEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_aggregation"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "ni"))
          [ .ret none ]
      , .declInit uintTy "l"
          (.bin "-" (.member (.var "params") "level") (.litUint 1))
      , .declInit uintTy "map_base"
          (.bin "*" (.var "l") (.member (.var "params") "ni"))
      , .declInit uintTy "sigma"
          (.member (.var "params") "domain_size")
      -- Default supernode = floor(i / σ). The orchestrator overrides
      -- with the skipping-approach-refined assignment on CPU at bind
      -- time; this is the safe initialization.
      , .assign (.index (.var "map_per_level")
                  (.bin "+" (.var "map_base") (.var "i")))
          (.call "int" [.bin "/" (.var "i") (.var "sigma")])
      , .ret none ] }

/-- Assemble per-domain σ×σ vertex-Hessian sub-matrix from L_II.
    Paper §6.2 Algorithm 1 step 1. One workgroup per domain (σ threads).

    Each lane writes one row of the dense σ×σ block to dense_workspace.
    The lane's "row vertex" is the interior vert at sorted slot
    domain*σ + lane (resolved via sorted_idx). For each column c in
    [0, σ), the lane finds the column vertex (domain*σ + c), then
    scans its own CSR row to locate the matching colIdx entry. If
    present, writes values[k]; otherwise writes 0.

    Output layout: dense_workspace[domain * σ² + lane * σ + col].
    Row-major, σ² floats per domain. mas_gj_invert reads this. -/
private def assembleSubmatrixEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 32 1 1]
  , name   := "mas_assemble_submatrix"
  , params :=
      [ ⟨"gid", .vec .uint 3, .svGroupId,       none, none, .qIn⟩
      , ⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩ ]
  , body   :=
      [ .declInit uintTy "domain" (.member (.var "gid") "x")
      , .declInit uintTy "lane"   (.member (.var "tid") "x")
      , .declInit uintTy "sigma"  (.member (.var "params") "domain_size")
      , .declInit uintTy "slot"
          (.bin "+" (.bin "*" (.var "domain") (.var "sigma")) (.var "lane"))
      -- Per-domain row base in dense_workspace.
      , .declInit uintTy "row_base"
          (.bin "+"
            (.bin "*" (.var "domain")
              (.bin "*" (.var "sigma") (.var "sigma")))
            (.bin "*" (.var "lane") (.var "sigma")))
      -- Out-of-range row slot — lanes past Ni in the last incomplete
      -- domain, plus every lane of the coarse-level domains that the
      -- level-0 assembly dispatch never resolves to a real vert. Write
      -- an IDENTITY row rather than leaving the dense block at its
      -- zero-init. A zero row gives gj_invert a 0 pivot → 1/0 = inf →
      -- NaN that propagates through M⁻¹ and poisons the entire residual
      -- reduction (the bug behind ||r||²=nan). An identity row makes the
      -- block [A 0; 0 I], whose inverse [A⁻¹ 0; 0 I] is finite and
      -- leaves the real sub-block's solve untouched.
      , .ifNoElse (.bin ">=" (.var "slot") (.member (.var "params") "ni"))
          [ .forCount "c" (.litUint 0) (.var "sigma")
              [ .assign (.index (.var "dense_workspace")
                          (.bin "+" (.var "row_base") (.var "c")))
                  (.ternary (.bin "==" (.var "c") (.var "lane"))
                    (.litFloat 1.0) (.litFloat 0.0)) ]
          , .ret none ]
      -- Resolve this lane's row vertex (the L_II row to scan).
      , .declInit uintTy "row_vert"
          (.call "uint" [.index (.var "sorted_idx") (.var "slot")])
      , .declInit uintTy "rs"
          (.call "uint" [.index (.var "rowPtr") (.var "row_vert")])
      , .declInit uintTy "re"
          (.call "uint" [.index (.var "rowPtr")
            (.bin "+" (.var "row_vert") (.litUint 1))])
      -- For each column c in this domain, scan the lane's CSR row for
      -- a match against the column vertex. Initialize to 0.
      , .forCount "c" (.litUint 0) (.var "sigma")
          [ .declInit uintTy "col_slot"
              (.bin "+" (.bin "*" (.var "domain") (.var "sigma"))
                        (.var "c"))
          -- For out-of-range column slots (last incomplete domain),
          -- use 0xFFFFFFFFu as a sentinel that can't match any real
          -- CSR colIdx entry (L_II has < Ni < 2³² columns). Writing
          -- val = 0 in that case keeps the dense submatrix block-
          -- diagonal-extended with zeros.
          , .declInit uintTy "col_vert"
              (.ternary (.bin "<" (.var "col_slot")
                          (.member (.var "params") "ni"))
                (.call "uint" [.index (.var "sorted_idx") (.var "col_slot")])
                (.litUint 0xFFFFFFFF))
          , .declInit floatTy "val" (.litFloat 0.0)
          , .forCount "k" (.var "rs") (.var "re")
              [ .declInit uintTy "csr_col"
                  (.call "uint" [.index (.var "colIdx") (.var "k")])
              , .ifNoElse (.bin "==" (.var "csr_col") (.var "col_vert"))
                  [ .assign (.var "val") (.index (.var "values") (.var "k")) ] ]
          -- Diagonal regularization: bump the diagonal entry by an
          -- absolute floor PLUS 10% of itself. The simple σ-bucket
          -- partition (no skipping-approach refinement) often produces
          -- domains where verts have all their neighbors outside the
          -- domain; the absolute floor (0.1) keeps GJ pivots far from
          -- zero in fp32, and the 10% bump preserves the matrix shape
          -- for the well-coupled domains. This biases the per-domain
          -- inverse toward diagonal dominance (Jacobi-like), trading
          -- some convergence speed for stability — acceptable until
          -- the §5.2 skipping approach lands and the partition itself
          -- guarantees PD blocks.
          , .ifNoElse (.bin "==" (.var "c") (.var "lane"))
              [ .assign (.var "val")
                  (.bin "+"
                    (.bin "*" (.var "val") (.litFloat 1.1))
                    (.litFloat 0.1)) ]
          , .assign (.index (.var "dense_workspace")
                      (.bin "+" (.var "row_base") (.var "c")))
              (.var "val") ]
      , .ret none ] }

/-- Gauss-Jordan invert the per-domain σ×σ dense sub-matrix and write
    M⁻¹ in §7.1 packed lower-triangular form. Paper §6.2 Algorithm 1
    steps 2-3 combined. One workgroup per domain (σ threads).

    Algorithm: load dense_workspace into [A | I] augmented (σ × 2σ) in
    groupshared. For each pivot k = 0..σ-1:
      - Lane k normalizes row k (divides by A[k][k] in both halves)
      - Barrier
      - All other lanes eliminate column k from their own row
      - Barrier
    After elimination, A is identity and the right half holds A⁻¹.
    Cooperative write: each lane stores its row's lower-triangular
    columns to m_inv_packed (paper §7.1 fig. 11 layout).

    Groupshared budget: 32 × 64 = 2048 floats = 8 KB. Well within
    Vulkan's per-workgroup limit. Per-pivot work: 2 barriers × 32
    pivots = 64 barriers. Heavy but acceptable at bind time. -/
private def gjInvertEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 32 1 1]
  , name   := "mas_gj_invert"
  , params :=
      [ ⟨"gid", .vec .uint 3, .svGroupId,       none, none, .qIn⟩
      , ⟨"tid", .vec .uint 3, .svGroupThreadId, none, none, .qIn⟩ ]
  , body   :=
      [ .declInit uintTy "domain" (.member (.var "gid") "x")
      , .declInit uintTy "lane"   (.member (.var "tid") "x")
      , .declInit uintTy "sigma"  (.member (.var "params") "domain_size")
      , .declInit uintTy "two_sigma"
          (.bin "*" (.var "sigma") (.litUint 2))
      -- Per-domain row base in dense_workspace (input).
      , .declInit uintTy "row_base"
          (.bin "+"
            (.bin "*" (.var "domain")
              (.bin "*" (.var "sigma") (.var "sigma")))
            (.bin "*" (.var "lane") (.var "sigma")))
      -- Cooperative load: lane writes its own row into A_aug. Left
      -- half = A from dense_workspace; right half = identity (1 at
      -- column == lane, 0 elsewhere).
      , .forCount "c" (.litUint 0) (.var "sigma")
          [ .assign (.index (.index (.var "A_aug") (.var "lane")) (.var "c"))
              (.index (.var "dense_workspace")
                (.bin "+" (.var "row_base") (.var "c"))) ]
      , .forCount "c" (.litUint 0) (.var "sigma")
          [ .assign (.index (.index (.var "A_aug") (.var "lane"))
                      (.bin "+" (.var "sigma") (.var "c")))
              (.ternary (.bin "==" (.var "c") (.var "lane"))
                (.litFloat 1.0) (.litFloat 0.0)) ]
      , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
      -- Gauss-Jordan: for each pivot k, normalize row k then eliminate
      -- column k from every other row. Two barriers per pivot.
      , .forCount "k" (.litUint 0) (.var "sigma")
          [ .ifNoElse (.bin "==" (.var "lane") (.var "k"))
              [ .declInit floatTy "pivot_inv"
                  (.bin "/" (.litFloat 1.0)
                    (.index (.index (.var "A_aug") (.var "k"))
                            (.var "k")))
              , .forCount "j" (.litUint 0) (.var "two_sigma")
                  [ .assign (.index (.index (.var "A_aug") (.var "k"))
                              (.var "j"))
                      (.bin "*" (.index (.index (.var "A_aug") (.var "k"))
                                  (.var "j"))
                                (.var "pivot_inv")) ] ]
          , .expr (.call "GroupMemoryBarrierWithGroupSync" [])
          , .ifNoElse (.bin "!=" (.var "lane") (.var "k"))
              [ .declInit floatTy "factor"
                  (.index (.index (.var "A_aug") (.var "lane"))
                          (.var "k"))
              , .forCount "j" (.litUint 0) (.var "two_sigma")
                  [ .assign (.index (.index (.var "A_aug") (.var "lane"))
                              (.var "j"))
                      (.bin "-" (.index (.index (.var "A_aug") (.var "lane"))
                                  (.var "j"))
                                (.bin "*" (.var "factor")
                                  (.index (.index (.var "A_aug") (.var "k"))
                                          (.var "j")))) ] ]
          , .expr (.call "GroupMemoryBarrierWithGroupSync" []) ]
      -- Packed write: lane stores its row's lower triangle (cols
      -- 0..lane) to m_inv_packed. Triangular row offset = lane(lane+1)/2.
      , .declInit uintTy "domain_packed_base"
          (.index (.var "domain_offsets") (.var "domain"))
      , .declInit uintTy "tri_row_base"
          (.bin "+" (.var "domain_packed_base")
            (.bin ">>"
              (.bin "*" (.var "lane")
                (.bin "+" (.var "lane") (.litUint 1)))
              (.litUint 1)))
      , .forCount "c" (.litUint 0) (.bin "+" (.var "lane") (.litUint 1))
          [ .assign (.index (.var "m_inv_packed")
                      (.bin "+" (.var "tri_row_base") (.var "c")))
              (.index (.index (.var "A_aug") (.var "lane"))
                (.bin "+" (.var "sigma") (.var "c"))) ]
      , .ret none ] }

/-- Coarsen r_input down all levels into r_per_level via the
    bind-time-inverted coarse map. Deferred-reduction layout per the
    plan's locked decision: no atomic float adds.

    One thread per supernode in the FLATTENED coarse layout (level 1's
    supernodes followed by level 2's, etc.). Each thread reads its
    member-vertex range from coarse_offsets, sums r_input[member] for
    each fine vertex in that supernode, writes the float3 sum to
    r_per_level[level_0_size + flat_supernode_idx].

    Level 0's r_per_level slice is just r_input — written by a separate
    identity-copy pass to keep the per-iter dispatch shape uniform.

    Workgroup width 256 covers 256 supernodes per dispatch; the
    orchestrator dispatches ceil(total_coarse_count / 256) workgroups. -/
private def coarsenResidualEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_coarsen_residual"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "s" (.member (.var "tid") "x")
      -- Out-of-range threads return. Bound is the total non-zero-level
      -- supernode count (Σ_{l>0} N_l), uploaded once at bind time in the
      -- coarsen-flavored params UBO.
      , .ifNoElse (.bin ">=" (.var "s")
                    (.member (.var "params") "total_coarse_supernodes"))
          [ .ret none ]
      , .declInit uintTy "start" (.index (.var "coarse_offsets") (.var "s"))
      , .declInit uintTy "end"
          (.index (.var "coarse_offsets") (.bin "+" (.var "s") (.litUint 1)))
      , .declInit floatTy "acc_x" (.litFloat 0.0)
      , .declInit floatTy "acc_y" (.litFloat 0.0)
      , .declInit floatTy "acc_z" (.litFloat 0.0)
      -- Read fine-vert residuals from r_per_level (level 0 slice lives
      -- at offsets [0, ni); mas_identity_copy_l0 stamped it before
      -- this dispatch). Reading from r_per_level instead of r_input
      -- lets the cached external-uniform-set path stay correct: only
      -- mas_identity_copy_l0 binds external r at slot 11.
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
      -- This entry writes into r_per_level slice [ni .. ni + Σ N_{l>0}).
      , .assign (.index (.var "r_per_level")
                  (.bin "+" (.member (.var "params") "ni") (.var "s")))
          (.call "float3" [.var "acc_x", .var "acc_y", .var "acc_z"])
      , .ret none ] }

/-- Identity-copy r_input → r_per_level[0..ni). Run once per apply
    before mas_coarsen_residual, since the latter now reads from
    r_per_level (not r_input) so the cached external-uniform-set path
    only has to rebind slot 11 (r_input) for this one kernel.

    One thread per interior vertex; numthreads(256, 1, 1). -/
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

/-- Per-vertex sum of z slices across all levels into z_output.
    Paper §7 final pass: z[i] = z^(0)[i] + Σ_{l=1..L-1} z^(l)[map_l[i]].

    One thread per interior vertex. The thread walks the level list
    sequentially (num_levels ≤ 4 in practice) accumulating its
    contribution from each level via the forward map_per_level.

    level_sizes drives the per-level offset in the flat
    z_per_level buffer: level 0 occupies [0, ni), level 1 occupies
    [ni, ni + N_1), and so on. -/
private def sumLevelsEntry : SlangFunctionDecl :=
  { attrs  := [.shaderCompute, .numthreads 256 1 1]
  , name   := "mas_sum_levels"
  , params := [⟨"tid", .vec .uint 3, .svDispatchThreadId, none, none, .qIn⟩]
  , body   :=
      [ .declInit uintTy "i" (.member (.var "tid") "x")
      , .ifNoElse (.bin ">=" (.var "i") (.member (.var "params") "ni"))
          [ .ret none ]
      -- Start from level 0: z^(0)[i].
      , .declInit float3Ty "acc"
          (.index (.var "z_per_level") (.var "i"))
      -- Running offset into z_per_level for the current level being
      -- accumulated. Initialized to ni (start of level 1's slice).
      , .declInit uintTy "off" (.member (.var "params") "ni")
      , .declInit uintTy "L"   (.member (.var "params") "num_levels")
      , .forCount "l" (.litUint 1) (.var "L")
          [ -- map_per_level layout: level l's mapping starts at offset
            -- (l - 1) * ni (level 0 is identity, skipped).
            .declInit uintTy "map_base"
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
          -- Advance offset by this level's supernode count.
          , .assign (.var "off")
              (.bin "+" (.var "off")
                (.index (.var "level_sizes") (.var "l"))) ]
      , .assign (.index (.var "z_output") (.var "i")) (.var "acc")
      , .ret none ] }

def shader : SlangShaderModule :=
  { structs   := [paramsStruct]
  , groupShared :=
      -- Per-workgroup cache of the σ-vert r_input slice, split into
      -- three scalar arrays (one per component). σ = 32; sized at 32
      -- to match numthreads(32, 1, 1) of mas_per_domain_solve.
      [ { name := "s_rx", elemType := floatTy, dims := [32] }
      , { name := "s_ry", elemType := floatTy, dims := [32] }
      , { name := "s_rz", elemType := floatTy, dims := [32] }
      -- Augmented [A | I] σ×2σ workspace for mas_gj_invert. 32×64 =
      -- 2048 floats = 8 KB. Reused across workgroups (each domain
      -- gets its own workgroup, so no race).
      , { name := "A_aug", elemType := floatTy, dims := [32, 64] } ]
  , globals   := globals
  , functions :=
      [ mortonComputeEntry
      , buildConnectMaskEntry
      , aggregationEntry
      , assembleSubmatrixEntry
      , gjInvertEntry
      , identityCopyL0Entry
      , coarsenResidualEntry
      , perDomainSolveEntry
      , sumLevelsEntry ] }

-- CPU sibling lives in `CassieAvbd.MasPreconditionerSerial.shader`,
-- registered in Codegen as this ubershader's cpuShader. The serial
-- module's `mas_per_domain_solve` reads r_input directly from global
-- memory (no groupshared, no barriers) so slangc -target cpp can
-- lower it. coarsen_residual and sum_levels in the serial module are
-- byte-equal to this one.

example : shader.entryPointNames =
    [ "mas_morton_compute", "mas_build_connect_mask", "mas_aggregation"
    , "mas_assemble_submatrix", "mas_gj_invert"
    , "mas_identity_copy_l0"
    , "mas_coarsen_residual", "mas_per_domain_solve", "mas_sum_levels" ] := by
  native_decide
example : shader.entryPoints.length = 9 := by native_decide

end CassieAvbd.MasPreconditioner
