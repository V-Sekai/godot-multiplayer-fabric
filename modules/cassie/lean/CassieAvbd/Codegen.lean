-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee
--
-- Slang codegen for CASSIE — composes DiffCloth's `Cloth.SlangCodegen.*`
-- kernels into the CASSIE-relevant subset, emits the Slang source via
-- `LeanSlang.emit`, then invokes `slangc -target spirv` to produce the
-- SPIR-V binaries that `RenderingDevice::shader_create_from_spirv`
-- loads.
--
-- Pipeline:
--
--   1. Pick the kernels CASSIE needs from `Cloth.SlangCodegen.*` —
--      same list `CassieAvbd.Step.kernelNames` pins via `native_decide`.
--   2. `LeanSlang.emit` pretty-prints each as Slang source.
--   3. Write to `<outDir>/<name>.slang`.
--   4. Spawn `slangc <name>.slang -target spirv -o <name>.spv` per
--      kernel; stream stderr through Lean for diagnosability.
--
-- ENG-52 phase 4.1b consumes Spmv + Saxpby for the sparse triangular
-- back-sub. ENG-49 / ENG-50 use the AVBD per-step blocks
-- (AttachmentDualUpdate, SpringForce, AttachmentProject, CGAlpha,
-- CGBeta, DotReduce).
--
-- The CPU fallback path stays on the Eigen `SimplicialLDLT::solve`
-- that ENG-46 phase 1.4b already ships — slangc-CPU output is a
-- follow-up if/when we want to fold the fallback into the same source.

import LeanSlang
import CassieAvbd.CgUbershader
import CassieAvbd.CgUbershader3
import CassieAvbd.CgUbershader3Serial
import CassieAvbd.CurveCasteljau
import CassieAvbd.CurveGenerateBezier
import CassieAvbd.CurveNewton
import CassieAvbd.CurveRdp
import CassieAvbd.MasPreconditioner
import CassieAvbd.MasPreconditionerSerial
import Cloth.SlangCodegen.AttachmentDualUpdate
import Cloth.SlangCodegen.AttachmentProject
import Cloth.SlangCodegen.CGAlpha
import Cloth.SlangCodegen.CGBeta
import Cloth.SlangCodegen.DotReduce
import Cloth.SlangCodegen.DotReduceSerial
import Cloth.SlangCodegen.Saxpby
import Cloth.SlangCodegen.SpmvDf32
import Cloth.SlangCodegen.SpringForce

open Cloth.SlangCodegen
open LeanSlang

namespace CassieAvbd.Codegen

/-- Per-target kernel pair. `gpu` is the parallel SIMT kernel emitted to
    `<name>.slang` → `<name>.spv` via `slangc -target spirv`. `cpu` is
    the single-threaded variant emitted to `<name>.cpu.slang` →
    `<name>.cpu.cpp` via `slangc -target cpp`. For most kernels the two
    are identical (slangc happily lowers them both ways); for
    `dot_reduce` the GPU version uses `groupshared` + barriers which
    slangc's CPU backend can't lower (E36107), so we substitute
    `DotReduceSerial` — same df32 EFTs, same result, sequential fold. -/
structure KernelPair where
  name : String
  gpu  : SlangShaderModule
  cpu  : SlangShaderModule

/-- Multi-entry-point shader module. The codegen emits a single
    `<name>.slang` source, then runs `slangc -entry <e> -target spirv`
    for each `e` in `shader.entryPointNames` to produce
    `<name>.<e>.spv`.

    When `cpuShader = some m`, the codegen also emits `<name>.cpu.slang`
    + per-entry `<name>.<entry>.cpu.cpp` from `m` — the CPU-target
    output is the production reference implementation. Used by
    ubershaders whose algorithm wants single-source CPU + GPU
    verifiability (e.g. the Wu/Wang/Wang 2022 MAS preconditioner port).

    `cpuShader` is left `none` for ubershaders whose entries use
    `groupshared` + `GroupMemoryBarrierWithGroupSync` — slangc's CPU
    backend can't lower those (E36107). For those modules a sibling
    CPU module (analogous to `DotReduceSerial` for `DotReduce`) must
    be provided explicitly. -/
structure UbershaderModule where
  name      : String
  shader    : SlangShaderModule
  cpuShader : Option SlangShaderModule := none

/-- Ubershader inventory. -/
def ubershaders : List UbershaderModule :=
  [ ⟨"cg_pcg", CgUbershader.shader, none⟩
    -- Phase B float3 variant of the CG outer loop. Multi-RHS by
    -- construction: today's 3 sequential scalar axis solves collapse
    -- to one float3 solve. Scaffold ships only jacobi_z initially;
    -- subsequent commits add the other 10 entries.
  , ⟨"cg_pcg3", CgUbershader3.shader, some CgUbershader3Serial.shader⟩
    -- Phase C MAS preconditioner. Dual-target: GPU dispatches via
    -- cassie_slang_gpu, CPU runs the .cpu.cpp emit for tests and the
    -- no-RD fallback. Scaffold ships only the per_domain_solve probe
    -- (identity transform) to validate workgroup-per-domain dispatch.
  , ⟨"mas_precond", MasPreconditioner.shader, some MasPreconditionerSerial.shader⟩ ]

/-- The CASSIE-relevant subset. -/
def kernels : List KernelPair :=
  -- ENG-52 phase 4.1b — sparse triangular back-sub building blocks.
  -- spmv uses the SpmvDf32 variant on both targets: row sums accumulate
  -- as a (hi, lo) df32 pair via Knuth/Dekker EFTs (two_prod, df_add),
  -- collapsed to fp32 at write-out. Binding layout matches plain Spmv
  -- (only the struct name changes — SpmvParams_0 → SpmvDf32Params_0),
  -- so wrappers swap with one identifier rename. Per-row error drops
  -- from ~7·ε to ~7·ε² — without this, the harmonic-deform PCG's
  -- residual plateaus around 1e-4 (PR #38).
  [ ⟨"spmv",                   SpmvDf32.shader,             SpmvDf32.shader⟩
  , ⟨"saxpby",                 Saxpby.shader,               Saxpby.shader⟩
    -- ENG-49 / ENG-50 — AVBD inner-step blocks.
  , ⟨"dot_reduce",             DotReduce.shader,            DotReduceSerial.shader⟩
  , ⟨"cg_alpha",               CGAlpha.shader,              CGAlpha.shader⟩
  , ⟨"cg_beta",                CGBeta.shader,               CGBeta.shader⟩
  , ⟨"attachment_dual_update", AttachmentDualUpdate.shader, AttachmentDualUpdate.shader⟩
  , ⟨"spring_force",           SpringForce.shader,          SpringForce.shader⟩
  , ⟨"attachment_project",     AttachmentProject.shader,    AttachmentProject.shader⟩
    -- First editing-pipeline kernel: De Casteljau cubic split. One thread
    -- per dispatch, pure function; CPU emission only — replaces the
    -- anonymous cubic_split helper in cassie_curve_fit.cpp.
  , ⟨"curve_casteljau",        CurveCasteljau.shader,       CurveCasteljau.shader⟩
    -- Second editing-pipeline kernel: iterative RDP polyline simplifier.
    -- Single-thread, fixed-capacity local stack; CPU emission only.
    -- Replaces the rdp_recursive body in modules/cassie/src/curves/
    -- rdp_simplify.cpp.
  , ⟨"curve_rdp",              CurveRdp.shader,             CurveRdp.shader⟩
    -- Third editing-pipeline kernel: Newton-Raphson reparameterize for
    -- Schneider cubic Bezier fits. Single-thread, loops over count
    -- points. Replaces the reparameterize body in
    -- modules/cassie/src/curves/cassie_curve_fit.cpp.
  , ⟨"curve_newton",           CurveNewton.shader,          CurveNewton.shader⟩
    -- Fourth editing-pipeline kernel: 2×2 LSQ Bezier generator (the
    -- math primitive inside Schneider 1990 §III). The recursive
    -- fit_curve_recursive driver in cassie_curve_fit.cpp stays in C++
    -- as a thin orchestrator over this + curve_newton + curve_rdp;
    -- Slang-side recursion would be all stack-management machinery
    -- for no perf or correctness gain on a CPU-only target.
  , ⟨"curve_generate_bezier",  CurveGenerateBezier.shader,  CurveGenerateBezier.shader⟩
  ]

end CassieAvbd.Codegen

/-- Invoke `slangc <input> -target <target> -o <output>`. Captures stdout
    + stderr and prints them so failures are diagnosable from the lake
    output. Returns the process exit code. -/
private def runSlangc (input : System.FilePath) (output : System.FilePath)
    (target : String) : IO UInt32 := do
  let result ← IO.Process.output {
    cmd := "slangc"
    args := #[input.toString, "-target", target, "-o", output.toString]
  }
  if result.stdout.length > 0 then
    IO.println s!"  [slangc stdout] {result.stdout.trim}"
  if result.stderr.length > 0 then
    IO.eprintln s!"  [slangc stderr] {result.stderr.trim}"
  return UInt32.ofNat result.exitCode.toNat

/-- Multi-entry variant: `slangc <input> -entry <entry> -target <target>
    -o <output>`. Single .slang source can declare multiple
    `[shader("compute")]` functions; slangc compiles only the named
    entry when `-entry` is supplied. Used by the ubershader pipeline
    to fan a single Lean module into N per-entry blobs at either the
    spirv or cpp target. -/
private def runSlangcEntry (input : System.FilePath) (entry : String)
    (output : System.FilePath) (target : String := "spirv") :
    IO UInt32 := do
  let result ← IO.Process.output {
    cmd := "slangc"
    args := #[input.toString, "-entry", entry, "-target", target,
              "-o", output.toString]
  }
  if result.stdout.length > 0 then
    IO.println s!"  [slangc stdout] {result.stdout.trim}"
  if result.stderr.length > 0 then
    IO.eprintln s!"  [slangc stderr] {result.stderr.trim}"
  return UInt32.ofNat result.exitCode.toNat


/-- Post-process a slangc -target cpp output file:

      1. Rewrite the `#include "<absolute path>/slang-cpp-prelude.h"`
         line (baked at slangc install time) to the vendored relative
         path `../slang-prelude/slang-cpp-prelude.h` so the file is
         portable and resolvable from `modules/cassie/thirdparty/avbd/`.

      2. Wrap everything after the `#endif` that closes the
         `SLANG_PRELUDE_NAMESPACE` using-decl in
         `namespace cassie_slang_<name> { ... }`. Without this, every
         emitted kernel exports `_main_0`, `GlobalParams_0`,
         `KernelContext_0` etc. at file scope — linking all 8 .cpu.cpp
         into the cassie module would collide on those symbols. The
         dispatch driver in cassie_pcg.cpp calls
         `cassie_slang_spmv::_main_0(...)` etc.

      3. Suppress `SLANG_PRELUDE_EXPORT` (which expands to
         `extern "C" __declspec(dllexport)` / GCC visibility export).
         slangc emits three host-runtime wrapper functions per kernel
         — `main_0`, `main_0_Group`, `main_0_Thread` — each marked with
         this macro so they'd be callable as shared-lib entry points.
         The `extern "C"` strips C++ namespace mangling, so without
         this override the three symbols collide across the 8 kernels
         at link time. We don't use these wrappers (the dispatch driver
         calls `_main_0` directly), so neutering the macro is safe.
         `#undef` + empty `#define` placed AFTER the `using namespace
         SLANG_PRELUDE_NAMESPACE;` block and BEFORE our namespace open.

    Handles both LF and CRLF line endings. -/
private def postProcessCpu (path : System.FilePath) (name : String) :
    IO Unit := do
  let content ← IO.FS.readFile path
  let lines := content.splitOn "\n"
  -- Step 1: rewrite prelude include.
  let lines := lines.map fun raw =>
    let line := raw.dropRightWhile (· == '\r')
    let trailing := raw.takeRightWhile (· == '\r')
    if line.startsWith "#include " && line.endsWith "slang-cpp-prelude.h\"" then
      "#include \"../slang-prelude/slang-cpp-prelude.h\"" ++ trailing
    else
      raw
  -- Step 2: locate the `#endif` that closes the SLANG_PRELUDE_NAMESPACE
  -- using-decl, then insert (a) SLANG_PRELUDE_EXPORT neutering and
  -- (b) namespace open after it.
  let nsName := "cassie_slang_" ++ name
  let exportNeuter := "\n#undef SLANG_PRELUDE_EXPORT\n#define SLANG_PRELUDE_EXPORT"
  let nsOpen := "\nnamespace " ++ nsName ++ " {"
  let nsClose := "} // namespace " ++ nsName
  let usingIdx := lines.findIdx? fun raw =>
    (raw.dropRightWhile (· == '\r')).endsWith "using namespace SLANG_PRELUDE_NAMESPACE;"
  let endifIdx := match usingIdx with
    | none => none
    | some u =>
      let after := lines.drop (u + 1)
      let rel := after.findIdx? fun raw =>
        (raw.dropRightWhile (· == '\r')) == "#endif"
      rel.map (· + u + 1)
  let lines := match endifIdx with
    | none => lines -- couldn't locate; leave file unwrapped (will fail to link, surfaces the bug)
    | some i => (lines.take (i + 1)) ++ [exportNeuter, nsOpen] ++ (lines.drop (i + 1))
  let body := String.intercalate "\n" lines
  -- Trim trailing newline before appending the close brace, then put one back.
  let body := body.dropRightWhile (fun c => c == '\n' || c == '\r')
  IO.FS.writeFile path (body ++ "\n" ++ nsClose ++ "\n")

/-- Entry point — `lake exe avbd-codegen [output-dir]`.

    For each kernel pair we emit four artifacts:

      * `<name>.slang`        — GPU Slang source (parallel SIMT kernel)
      * `<name>.spv`          — SPIR-V from `slangc -target spirv`
      * `<name>.cpu.slang`    — CPU Slang source (single-threaded variant)
      * `<name>.cpu.cpp`      — C++ from `slangc -target cpp`

    For kernels where the CPU and GPU shader modules are identical
    (every kernel except `dot_reduce`), the two .slang files are
    byte-equal — kept separate so the avbd-codegen output tree is
    uniform. The .cpu.cpp files are tracked in git (they're the
    production CPU implementation); the .slang and .spv files remain
    gitignored as regenerable. -/
def main (args : List String) : IO UInt32 := do
  let outDir : System.FilePath := match args with
    | []     => "../thirdparty/avbd"
    | a :: _ => System.FilePath.mk a
  IO.FS.createDirAll outDir

  let mut emitted := 0
  let mut spv_compiled := 0
  let mut cpp_compiled := 0
  let mut spv_failed := 0
  let mut cpp_failed := 0

  for ⟨name, gpu, cpu⟩ in CassieAvbd.Codegen.kernels do
    -- GPU pass: <name>.slang → <name>.spv
    let gpuSlangPath := outDir / (name ++ ".slang")
    let spvPath      := outDir / (name ++ ".spv")
    IO.FS.writeFile gpuSlangPath (LeanSlang.emit gpu ++ "\n")
    IO.println s!"emit  {gpuSlangPath}"
    emitted := emitted + 1

    let rc ← runSlangc gpuSlangPath spvPath "spirv"
    if rc == 0 then
      IO.println s!"spirv {spvPath}"
      spv_compiled := spv_compiled + 1
    else
      IO.eprintln s!"FAIL  {gpuSlangPath} → spv (slangc exit {rc})"
      spv_failed := spv_failed + 1

    -- CPU pass: <name>.cpu.slang → <name>.cpu.cpp
    let cpuSlangPath := outDir / (name ++ ".cpu.slang")
    let cppPath      := outDir / (name ++ ".cpu.cpp")
    IO.FS.writeFile cpuSlangPath (LeanSlang.emit cpu ++ "\n")
    IO.println s!"emit  {cpuSlangPath}"
    emitted := emitted + 1

    let rc ← runSlangc cpuSlangPath cppPath "cpp"
    if rc == 0 then
      postProcessCpu cppPath name
      IO.println s!"cpp   {cppPath}"
      cpp_compiled := cpp_compiled + 1
    else
      IO.eprintln s!"FAIL  {cpuSlangPath} → cpp (slangc exit {rc})"
      cpp_failed := cpp_failed + 1

  -- Multi-entry-point ubershader pass: emit <name>.slang once, then
  -- run slangc -entry <e> -target spirv for each e in entryPointNames,
  -- producing <name>.<e>.spv per entry. When cpuShader is provided,
  -- also emit <name>.cpu.slang and per-entry <name>.<e>.cpu.cpp so the
  -- module has both GPU and CPU production implementations from one
  -- Lean source — used by MAS preconditioner verification.
  let mut ubershader_entries := 0
  let mut ubershader_compiled := 0
  let mut ubershader_failed := 0
  let mut ubershader_cpu_compiled := 0
  let mut ubershader_cpu_failed := 0
  for ⟨name, shader, cpuShader?⟩ in CassieAvbd.Codegen.ubershaders do
    let slangPath := outDir / (name ++ ".slang")
    IO.FS.writeFile slangPath (LeanSlang.emit shader ++ "\n")
    IO.println s!"emit  {slangPath}"
    emitted := emitted + 1
    for entry in shader.entryPointNames do
      let spvPath := outDir / (name ++ "." ++ entry ++ ".spv")
      let rc ← runSlangcEntry slangPath entry spvPath
      ubershader_entries := ubershader_entries + 1
      if rc == 0 then
        IO.println s!"spirv {spvPath}"
        ubershader_compiled := ubershader_compiled + 1
      else
        IO.eprintln s!"FAIL  {slangPath} -entry {entry} → spv (slangc exit {rc})"
        ubershader_failed := ubershader_failed + 1
    -- CPU pass (if cpuShader provided): one cpu.slang per ubershader,
    -- one cpu.cpp per entry. Each emitted .cpu.cpp is postprocessed the
    -- same way single-kernel CPU outputs are (prelude rewrite + symbol
    -- namespacing) — see postProcessCpu doc.
    match cpuShader? with
    | none => pure ()
    | some cpu =>
      let cpuSlangPath := outDir / (name ++ ".cpu.slang")
      IO.FS.writeFile cpuSlangPath (LeanSlang.emit cpu ++ "\n")
      IO.println s!"emit  {cpuSlangPath}"
      emitted := emitted + 1
      for entry in cpu.entryPointNames do
        let cppPath := outDir / (name ++ "." ++ entry ++ ".cpu.cpp")
        let rc ← runSlangcEntry cpuSlangPath entry cppPath "cpp"
        if rc == 0 then
          postProcessCpu cppPath (name ++ "_" ++ entry)
          IO.println s!"cpp   {cppPath}"
          ubershader_cpu_compiled := ubershader_cpu_compiled + 1
        else
          IO.eprintln s!"FAIL  {cpuSlangPath} -entry {entry} → cpp (slangc exit {rc})"
          ubershader_cpu_failed := ubershader_cpu_failed + 1

  IO.println s!""
  IO.println s!"emitted {emitted} Slang sources"
  IO.println s!"  SPIR-V: {spv_compiled} compiled, {spv_failed} failed"
  IO.println s!"  C++   : {cpp_compiled} compiled, {cpp_failed} failed"
  IO.println s!"  ubershader entries (GPU): {ubershader_compiled} compiled, {ubershader_failed} failed"
  IO.println s!"  ubershader entries (CPU): {ubershader_cpu_compiled} compiled, {ubershader_cpu_failed} failed"
  return if spv_failed == 0 && cpp_failed == 0 && ubershader_failed == 0
           && ubershader_cpu_failed == 0 then 0 else 1
