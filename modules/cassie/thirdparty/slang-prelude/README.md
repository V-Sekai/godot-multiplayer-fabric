# Vendored slang-cpp prelude

These four headers are copied verbatim from the [Slang](https://github.com/shader-slang/slang)
project's runtime include tree:

- `slang-cpp-prelude.h`
- `slang-cpp-types.h`
- `slang-cpp-types-core.h`
- `slang-cpp-scalar-intrinsics.h`

They define the types (`StructuredBuffer<T>`, `RWStructuredBuffer<T>`,
`Vector<T, N>`, `ComputeThreadVaryingInput`, `slang_bit_cast`, etc.)
that `slangc -target cpp` emits references to. The cassie module's
`*.cpu.cpp` translation units (under `../avbd/`) include this prelude
via the relative path `../slang-prelude/slang-cpp-prelude.h`.

## Why vendored

`slangc` bakes the prelude's absolute path at install time. Without
vendoring, every `*.cpu.cpp` would include
`C:/Users/<whoever-ran-lake-exe>/scoop/apps/slang/current/include/slang-cpp-prelude.h`
— non-portable. The `lake exe avbd-codegen` runner rewrites the
`#include` line to the relative path above after slangc emits.

## License

Apache-2.0 WITH LLVM-exception — see `LICENSE`. Compatible with
Godot's MIT licensing.

## Regeneration

If Slang upstream changes the prelude shape, re-vendor:

```sh
cp /path/to/slang/include/slang-cpp-{prelude,types,types-core,scalar-intrinsics}.h \
   modules/cassie/thirdparty/slang-prelude/
```

Then rerun `lake exe avbd-codegen` from `modules/cassie/lean/` to
regenerate `../avbd/*.cpu.cpp` against the new prelude.
