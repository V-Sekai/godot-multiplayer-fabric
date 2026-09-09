# CASSIE Lean tree

Composes the DiffCloth AVBD kernel library + LeanSlang codegen into the CASSIE-specific Slang kernels the C++ solver and Profile Mover consume.

## Vendored layout

- **`Cloth.lean`, `Cloth/`, `EmitShaders.lean`** — vendored verbatim from `V-Sekai/TOOL_cloth_dynamics/lean` (upstream). Updates flow back via re-copy when needed; the headers inside each `Cloth/SlangCodegen/*.lean` already reference `V-Sekai-fire/Curvenet.SlangCodegen.*` as the upstream they mirror, so this is a third-tier vendor.
- **`CassieAvbd/`** — CASSIE-specific composition layer.
- **`lakefile.lean`** declares `Cloth` and `CassieAvbd` as two local `lean_lib`s plus the `emit_shaders` (DiffCloth's original entry-point, kept available as the reference codegen invocation) and `avbd-codegen` exes.

Vendoring instead of requiring DiffCloth from git — Lake didn't accept the `/ "lean"` subpath required to point at the upstream's nested package; vendoring keeps proofs + kernels co-located with the C++ consumer.

## Provided by the vendored tree

- `Cloth.Avbd.{AdjacencySpring, AdjacencyKwise, Coloring}` — host-side AVBD precomputes.
- `Cloth.SlangCodegen.*` (~40 kernels): `Spmv`, `SpmvDf32`, `Saxpby`, `SaxpbyIndirect`, `SaxpbyIndirectDf32`, `CGAlpha`, `CGBeta`, `DotReduce`, `DotReduceSerial`, `AssembleB`, `AttachmentDualUpdate`, `AttachmentForce{,Al,AlBackward}`, `AttachmentProject`, `SpringForce{,Backward}`, `SpringProject`, `TriangleBending`, `TriangleBendingDualUpdate`, `TriangleBendingForce{,Al,AlBackward}`, `TriangleMembraneDualUpdate`, `TriangleMembraneForce{,Al,AlBackward}`, `TriangleProject`, `SelfCollisionScan`, `VbdInit{,Backward}`, `VbdGather{Attachment,Bending,Spring,Triangle}{,Backward}`, `VbdSolveApply{,Backward}`.

## Required deps

- **`LeanSlang`** — `require LeanSlang from git "https://github.com/V-Sekai-fire/lean-slang.git" @ "v0.0.5"`. Provides the AST + emitter for Slang shader source. Pulled by `lake build` on first run.
- **`slangc`** — Slang's reference compiler. Lowers Slang source to SPIR-V (Vulkan), CPU code, HLSL, Metal in one pass. DiffCloth ships it under `E:\TOOL_cloth_dynamics\bin\slangc` with the runtime DLLs — point the codegen exe at that path or install slangc separately.

## Pipeline

1. CASSIE Lean source composes DiffCloth kernels for the CASSIE-shaped problem (`CassieAvbd/Step.lean`, `CassieAvbd/Codegen.lean`).
2. `lake exe avbd-codegen` runs `LeanSlang.Emit.run` on the composed terms → Slang source.
3. The codegen exe invokes `slangc -target spirv` and `slangc -target cpu` → two binary outputs.
4. Both get embedded as byte arrays into `modules/cassie/thirdparty/avbd/avbd_step.h` (gitignored).
5. C++ side: `RenderingDevice::shader_create_from_spirv` loads the SPIR-V on `Pose-mode-active && rd-not-null`; the CPU output runs in headless tests + when `RenderingDevice` is null.

## What this covers in the issue tracker

- **ENG-52 phase 4.1b** — the level-set schedule shipped under `ae1af56242` is exactly the input `Cloth.SlangCodegen.Spmv` consumes. Reuse > hand-rolled GLSL.
- **ENG-49 + ENG-50** — the AVBD inner step uses DiffCloth's already-proved `AttachmentDualUpdate` + `SpringForce` + `CGAlpha` + `CGBeta` + `DotReduce` kernels rather than re-deriving them in CASSIE's Lean tree.

## How to build

```
cd modules/cassie/lean
lake build              # resolves LeanSlang → builds Cloth + CassieAvbd (native_decide runs here)
lake exe avbd-codegen   # emits .slang + invokes slangc → .spv per kernel under ../thirdparty/avbd/
lake exe emit_shaders   # DiffCloth's original full-shader-set entry-point, kept available
```

Requires `slangc` on `PATH` for the SPIR-V step (`scoop install slang` works on Windows; ships with the Slang SDK on other platforms). `lake build` only needs to fetch `lean-slang @ v0.0.5` on first run — Cloth is vendored. Mathlib closure (~1 GB disk) downloads transitively through LeanSlang on first build.

## Soundness

No `sorry`. No `: True := by trivial` wrapper "theorems". The vendored Cloth tree's correctness checks (fixture equalities + Slang text references) are all `native_decide`'d at the type-check pass, and `CassieAvbd/Step.lean` adds CASSIE-specific `native_decide` pins:

- Each emitted kernel's `entryPointName = "main"` (the contract `RenderingDevice::shader_create_from_spirv` relies on).
- The kernel-name list has exactly the expected length.

`lake build` exit code 0 ⇒ every `native_decide` succeeded.
