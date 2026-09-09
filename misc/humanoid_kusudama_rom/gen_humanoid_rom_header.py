"""gen_humanoid_rom_header.py — bake the per-bone model-extreme ROM into a C++ header
so the generator ships inside Godot's monolithic binary (no runtime JSON).

Reads rom_limits.jsonl (per-subject NORMAL-range joint limits), takes the median
template per bone, rotates each bone's swing cones into the Kusudama canonical frame
(cloud mean -> +Y, since make_space aligns +Y to the bone forward), and emits
scene/resources/3d/humanoid_kusudama_rom_data.h with cone arrays keyed by Godot
SkeletonProfileHumanoid bone names + clinical gap-fills + phenotype-scale coeffs.
"""

import json
import os
import sys

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.normpath(os.path.join(_HERE, "..", ".."))  # misc/humanoid_kusudama_rom -> repo root
sys.path.insert(0, _HERE)  # for `import humanoid_joint_fans`
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(_HERE, "rom_limits.jsonl.zst")
OUT = (
    sys.argv[2]
    if len(sys.argv) > 2
    else os.path.join(_REPO, "scene", "resources", "3d", "humanoid_kusudama_rom_data.h")
)


def _open_lines(path):
    """Read a .jsonl or zstd-compressed .jsonl.zst into a list of lines."""
    if path.endswith(".zst"):
        try:
            import zstandard

            with open(path, "rb") as fh:
                return zstandard.ZstdDecompressor().stream_reader(fh).read().decode().splitlines()
        except ImportError:
            import subprocess  # fall back to the zstd CLI

            return subprocess.run(["zstd", "-dc", path], capture_output=True, check=True).stdout.decode().splitlines()
    with open(path) as fh:
        return fh.readlines()


# sinew bone -> Godot SkeletonProfileHumanoid bone slot
BONE_MAP = {
    "LeftUpperLeg": "LeftUpperLeg",
    "RightUpperLeg": "RightUpperLeg",
    "LeftLowerLeg": "LeftLowerLeg",
    "RightLowerLeg": "RightLowerLeg",
    "LeftFoot": "LeftFoot",
    "RightFoot": "RightFoot",
    "LeftUpperArm": "LeftUpperArm",
    "RightUpperArm": "RightUpperArm",
    "LeftLowerArm": "LeftLowerArm",
    "RightLowerArm": "RightLowerArm",
    "Chest": "Spine",
}


def muscle_frame(centers, mean):
    """Rows-of-basis rotation into the bone MUSCLE frame: forward (mean swing) -> +Y AND
    the fan's principal spread axis -> +X, so the flexion plane is fixed (deterministic
    roll), not the arbitrary roll a shortest-arc alignment leaves. R @ center expresses a
    center in that frame. For single/round cones (no spread) the roll is irrelevant and an
    arbitrary perpendicular is used."""
    y = mean / (np.linalg.norm(mean) + 1e-12)
    # tangent components of the cone centers (project out the forward axis)
    t = centers - np.outer(centers @ y, y)
    if len(centers) >= 2 and np.linalg.norm(t) > 1e-6:
        # principal spread direction = first right-singular vector of the tangent cloud
        spread = np.linalg.svd(t, full_matrices=False)[2][0]
    else:
        spread = np.array([1.0, 0.0, 0.0]) if abs(y[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
    x = spread - (spread @ y) * y
    x /= np.linalg.norm(x) + 1e-12
    z = np.cross(x, y)
    z /= np.linalg.norm(z) + 1e-12
    x = np.cross(y, z)  # re-orthonormalize, right-handed
    return np.array([x, y, z])


def aggregate_bone(cand):
    """Population template at the ANNY reference phenotype: per-bone ROM aggregated
    ACROSS subjects (not one subject). Keep the modal cone count K; among the K-cone
    subjects, match each subject's cones to an anchor by nearest center direction and
    average center + radius -> a canonical, subject-independent cone set."""
    K = int(np.median([c["cone_count"] for c in cand]))
    kcand = [c for c in cand if c["cone_count"] == K] or cand
    anchor = np.array(kcand[0]["cones"], dtype=float)
    acc = np.zeros_like(anchor)
    cnt = 0
    for c in kcand:
        cones = np.array(c["cones"], dtype=float)
        if len(cones) != len(anchor):
            continue
        used = set()
        for ai in range(len(anchor)):
            an = anchor[ai, :3] / (np.linalg.norm(anchor[ai, :3]) + 1e-12)
            best, bj = 1e9, 0
            for j in range(len(cones)):
                if j in used:
                    continue
                cj = cones[j, :3] / (np.linalg.norm(cones[j, :3]) + 1e-12)
                dist = 1.0 - float(np.dot(an, cj))
                if dist < best:
                    best, bj = dist, j
            used.add(bj)
            acc[ai] += cones[bj]
        cnt += 1
    return acc / max(cnt, 1), float(np.median([c["twist_range_deg"] for c in kcand])), cnt


def main():
    rows = [json.loads(line) for line in _open_lines(SRC) if line.strip()]
    rows = [r for r in rows if r.get("bones")]
    template = {}
    for sinew, godot in BONE_MAP.items():
        cand = [r["bones"][sinew] for r in rows if sinew in r["bones"]]
        if not cand:
            continue
        cones, twist_deg, nsub = aggregate_bone(cand)  # averaged across subjects
        centers = cones[:, :3]
        mean = centers.mean(0)
        mean /= np.linalg.norm(mean) + 1e-12
        R = muscle_frame(centers, mean)  # forward -> +Y, flexion -> +X
        rc = (R @ centers.T).T
        rc /= np.linalg.norm(rc, axis=1, keepdims=True)
        baked = [(rc[i, 0], rc[i, 1], rc[i, 2], np.radians(cones[i, 3])) for i in range(len(cones))]
        template[godot] = dict(cones=baked, twist=np.radians(twist_deg))

    # bone -> (swing_deg, twist_deg); clinical normatives for joints with no model ROM.
    CLIN_DEG = {
        "Hips": (45, 90),
        "Neck": (45, 140),
        "Head": (30, 90),
        "Chest": (25, 60),
        "UpperChest": (20, 60),
        "LeftShoulder": (25, 30),
        "RightShoulder": (25, 30),
        "LeftToes": (40, 0),
        "RightToes": (40, 0),
        "LeftEye": (28, 0),
        "RightEye": (28, 0),
        "Jaw": (18, 0),
    }
    # Fingers (both hands): per-segment flexion ROM as a single clinical cone + small twist.
    _FINGER_SEG = {  # finger -> {segment: (swing_deg, twist_deg)}
        "Thumb": {"Metacarpal": (45, 12), "Proximal": (30, 6), "Distal": (40, 0)},
        "Index": {"Proximal": (50, 6), "Intermediate": (55, 0), "Distal": (40, 0)},
        "Middle": {"Proximal": (50, 5), "Intermediate": (55, 0), "Distal": (40, 0)},
        "Ring": {"Proximal": (50, 6), "Intermediate": (55, 0), "Distal": (40, 0)},
        "Little": {"Proximal": (52, 8), "Intermediate": (55, 0), "Distal": (40, 0)},
    }
    for side in ("Left", "Right"):
        for finger, segs in _FINGER_SEG.items():
            for seg, sw_tw in segs.items():
                CLIN_DEG[side + finger + seg] = sw_tw
    CLINICAL = {b: (np.radians(s), np.radians(t)) for b, (s, t) in CLIN_DEG.items()}

    # Anatomical minimal cone fans for ALL 44 clinical humanoid joints, fitted on the unit
    # sphere (max-IoU, minimal cone count) and Plausible-verified by kusudama_lean/JointRom.lean:
    # round joints -> 1 covering cone, hinges -> tapered fans offset toward flexion. Replaces
    # the crude single symmetric cones that over-covered (the wrist "bulge up").
    # BONE_FANS[bone] = (twist_deg, [(swing_x, swing_z, radius_deg), ...]); y derived (|c|=1).
    from humanoid_joint_fans import BONE_FANS

    def _fan_cones(axes):
        out = []
        for sx, sz, rdeg in axes:
            y = float(np.sqrt(max(0.0, 1.0 - sx * sx - sz * sz)))
            out.append((sx, y, sz, float(np.radians(rdeg))))
        return out

    JOINT_FANS = {b: (float(np.radians(tw)), _fan_cones(axes)) for b, (tw, axes) in BONE_FANS.items()}

    L = []
    P = L.append
    P("/* humanoid_kusudama_rom_data.h - GENERATED by addb-extract/gen_humanoid_rom_header.py. */")
    P("/* Per-bone NORMAL-range swing cones (Kusudama canonical frame, +Y=forward) at the */")
    P(f"/* ANNY REFERENCE phenotype, aggregated over {len(rows)} AddBiomechanics With_Arm subjects. */")
    P("/* HumanoidKusudamaRom interpolates this over the ANNY phenotype. Do not edit by hand. */")
    P("#ifndef HUMANOID_KUSUDAMA_ROM_DATA_H")
    P("#define HUMANOID_KUSUDAMA_ROM_DATA_H")
    P("")
    P("namespace HumanoidKusudamaRomData {")
    P("struct ConeF { float x, y, z, radius; };")
    P("struct BoneRom { const char *bone; const ConeF *cones; int cone_count; float twist; bool clinical; };")
    P("")
    names = []
    for godot, t in template.items():
        arr = ", ".join("{ %.6ff, %.6ff, %.6ff, %.6ff }" % c for c in t["cones"])
        P(f"static const ConeF TPL_{godot}[] = {{ {arr} }};")
        names.append((godot, f"TPL_{godot}", len(t["cones"]), t["twist"], "false"))
    for bone, (twist_rad, cones) in JOINT_FANS.items():
        arr = ", ".join("{ %.6ff, %.6ff, %.6ff, %.6ff }" % c for c in cones)
        P(f"static const ConeF TPL_{bone}[] = {{ {arr} }};")
        names.append((bone, f"TPL_{bone}", len(cones), twist_rad, "true"))
    for bone, (sw, tw) in CLINICAL.items():
        if bone in template or bone in JOINT_FANS:
            continue
        P(f"static const ConeF TPL_{bone}[] = {{ {{ 0.0f, 1.0f, 0.0f, %.6ff }} }};" % sw)
        names.append((bone, f"TPL_{bone}", 1, tw, "true"))
    P("")
    P("static const BoneRom TEMPLATE[] = {")
    for nm, arr, cnt, tw, cl in names:
        P('\t{ "%s", %s, %d, %.6ff, %s },' % (nm, arr, cnt, tw, cl))
    P("};")
    P(f"static const int TEMPLATE_COUNT = {len(names)};")
    P("")
    P("// ROM is INTERPOLATED over the ANNY phenotype axes (each 0..1, neutral 0.5):")
    P("//   scale = clamp(1 + sum ANNY_*_COEF*(axis-0.5), SCALE_MIN, SCALE_MAX).")
    P("// Magnitudes are literature-guided (ROM declines with age and weight); the model")
    P("// EXTREMES are ~phenotype-invariant in the source data, so the interpolation is smooth")
    P("// but gentle. The template above is the ANNY REFERENCE-phenotype ROM (subjects aggregated).")
    P("static const float ANNY_AGE_COEF = -0.30f;")
    P("static const float ANNY_WEIGHT_COEF = -0.20f;")
    P("static const float ANNY_MUSCLE_COEF = -0.10f;")
    P("static const float ANNY_GENDER_COEF = -0.06f;")
    P("static const float ANNY_HEIGHT_COEF = 0.0f;")
    P("static const float ANNY_PROPORTIONS_COEF = 0.0f;")
    P("static const float SCALE_MIN = 0.6f;")
    P("static const float SCALE_MAX = 1.15f;")
    P(
        "static const float MIN_CONE_RADIUS = 0.095993f; // 5.5 deg floor (~1.6x SOFT_BAND): tighter hinges, never zero-ROM"
    )
    P("} // namespace HumanoidKusudamaRomData")
    P("")
    P("#endif // HUMANOID_KUSUDAMA_ROM_DATA_H")
    open(OUT, "w").write("\n".join(L) + "\n")
    print(
        f"wrote {OUT}\n  template bones: {list(template.keys())}\n  clinical: "
        f"{[b for b in CLINICAL if b not in template]}\n  total baked bones: {len(names)} from {len(rows)} subjects"
    )


if __name__ == "__main__":
    main()
