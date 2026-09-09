"""Build-time embedder: .spv binary → C++ header with `static constexpr
uint8_t <name>_spv[]` byte array.

Used by modules/cassie/SCsub to bake the Slang-emitted SPIR-V kernels
into the Godot binary so RenderingDevice::shader_create_from_spirv can
dispatch without a runtime file lookup. The .spv files themselves are
the source of truth (committed under modules/cassie/thirdparty/avbd/);
the .gen.h headers are regenerable build artifacts gitignored via the
global `*.gen.*` rule.

SCons usage:

    env.Command(
        target="thirdparty/avbd/spmv.spv.gen.h",
        source="thirdparty/avbd/spmv.spv",
        action=Action(spv_to_header_builder, "Embedding $SOURCE"),
    )
"""

import os


def spv_to_header_builder(target, source, env):
    """SCons builder action: source .spv → target .gen.h with byte array."""
    src_path = str(source[0])
    dst_path = str(target[0])

    with open(src_path, "rb") as f:
        data = f.read()

    stem = os.path.basename(src_path)
    # Strip the .spv extension to get the kernel name. e.g. "spmv".
    if stem.endswith(".spv"):
        stem = stem[: -len(".spv")]
    # Multi-entry ubershader files use the form `<module>.<entry>.spv`
    # (e.g. cg_pcg.init.spv). Dots are not valid in C identifiers, so
    # rewrite them to underscores for both the array variable name and
    # the size constant. Same rewrite applied to dashes for safety.
    safe_stem = stem.replace(".", "_").replace("-", "_")
    var = f"{safe_stem}_spv"

    lines = []
    lines.append(f"// Auto-generated from {src_path}.")
    lines.append("// Do not edit — regenerated at SCons build time from the .spv.")
    lines.append("// SPIR-V bytes from slangc -target spirv on a")
    lines.append("// Cloth.SlangCodegen Lean kernel, ready to feed")
    lines.append("// RenderingDevice::shader_create_from_spirv.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstddef>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace cassie_slang_spirv {")
    lines.append("")
    lines.append(f"static constexpr uint8_t {var}[] = {{")

    # 16 bytes per line, leading 2-space indent.
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        hex_pairs = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"  {hex_pairs},")

    lines.append("};")
    lines.append("")
    lines.append(f"static constexpr size_t {var}_size = sizeof({var});")
    lines.append("")
    lines.append("} // namespace cassie_slang_spirv")
    lines.append("")

    with open(dst_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
