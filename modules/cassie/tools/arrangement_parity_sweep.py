"""arrangement_parity_sweep.py — 65-dataset planar-arrangement parity test.

For every cassie-data session that has both raw_data/NN-X-Y.json and a
sketch_graph/NN-X-Y.json reference (the 65 sessions exported in Armature
and Patch modes), flatten the recorded strokes to polylines, run our
arrangement_probe Lean exe, and diff the resulting (nodes, edges) counts
against Unity's reference.

Prox per session is derived from the recorded `canvasScale` median in
systemStates following Unity's rule
(`ProximityThreshold = 0.02 × 2 × canvasScale = 0.04 × canvasScale`).
NO per-mesh prox search — there is one rule, one value per session,
end of story. See feedback_no-prox-tuning.md in auto-memory.

Writes modules/cassie/lean/_bench/arrangement_parity_65.json with:
  {
    "recorded": "<iso date>",
    "n_total": 65, "n_ok": ..., "n_failed": ...,
    "per_mesh": [
      { "id": "01-1-1",
        "prox": 0.04, "canvas_scale_median": 1.0,
        "lean": {"nodes": .., "edges": .., "sharp": .., "strokes": ..},
        "ref":  {"nodes": .., "segments": .., "strokes": ..},
        "delta": {"nodes": .., "edges": ..} },
      ...
    ],
    "aggregate": { "lean_nodes": .., "ref_nodes": .., ... }
  }

Run from anywhere:
  python modules/cassie/tools/arrangement_parity_sweep.py
"""

import datetime as _dt
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

# Unity's CASSIEParameters defaults at unit canvas scale, per
# E:/cassie/Assets/Parameters/CASSIEParameters.cs:
#   ProximityThreshold           = SmallDistance × 2   = 0.04
#   MergeConstraintsThreshold    = SmallDistance × 0.5 = 0.01
#   SnapToExistingNodeThreshold  = SmallDistance × 1.0 = 0.02
# Prox is for intersection detection. Merge_eps for the planar
# arrangement's node consolidation uses SnapToExistingNode — Unity's
# rule for "is this snap close enough to an existing graph node".
# (MergeConstraints is for constraint-event coalescing during
# beautification, not for graph nodes.) All scale linearly with
# canvasScale at runtime.
UNITY_PROX_BASE = 0.04
UNITY_MERGE_EPS_BASE = 0.02
# Samples per cubic Bezier piece produced by flatten_ctrl_pts. Lets the
# Lean side recover cubic boundaries and emit at most one split per
# cubic-cubic pair (Unity's intersection unit) instead of one per
# sub-sample pair.
SAMPLES_PER_CUBIC = 8

DATA = Path("E:/cassie-data/data")
RAW = DATA / "raw_data"
REF = DATA / "sketch_graph"
LEAN_DIR = Path("E:/multiplayer-fabric-godot/modules/cassie/lean")
BENCH_OUT = LEAN_DIR / "_bench/arrangement_parity_65.json"


def v3(d):
    return (d.get("x", 0.0), d.get("y", 0.0), d.get("z", 0.0))


def flatten_ctrl_pts(ctrl, spp=8):
    n = len(ctrl)
    if n < 2:
        return []
    if n == 2:
        return [v3(ctrl[0]), v3(ctrl[1])]
    segs = (n - 1) // 3
    if segs < 1:
        return []
    out = []
    for s in range(segs):
        p0 = v3(ctrl[3 * s])
        p1 = v3(ctrl[3 * s + 1])
        p2 = v3(ctrl[3 * s + 2])
        p3 = v3(ctrl[3 * s + 3])
        t_count = spp + 1 if s == segs - 1 else spp
        for i in range(t_count):
            t = i / spp
            mt = 1.0 - t
            out.append((
                mt * mt * mt * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t * t * t * p3[0],
                mt * mt * mt * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t * t * t * p3[1],
                mt * mt * mt * p0[2] + 3 * mt * mt * t * p1[2] + 3 * mt * t * t * p2[2] + t * t * t * p3[2],
            ))
    return out


def session_ids():
    return sorted(p.stem for p in REF.glob("*.json"))


def session_canvas_scale(raw_doc):
    """Median canvasScale.x across systemStates — single value per session,
    derived from the data not tuned. canvasScale is Vector3 in newer
    exports and scalar in older ones; handle both."""
    scales = []
    for s in raw_doc.get("systemStates", []):
        cs = s.get("canvasScale")
        if isinstance(cs, dict):
            x = cs.get("x")
            if isinstance(x, (int, float)) and x > 0:
                scales.append(float(x))
        elif isinstance(cs, (int, float)) and cs > 0:
            scales.append(float(cs))
    if not scales:
        return 1.0
    return statistics.median(scales)


def stroke_cubics(ctrl):
    """Slice ctrlPts into per-cubic control-point quads. CASSIE raw_data
    stores poly-Bezier control points as a flat sequence (P0, P1, P2,
    P3, P4, ...) where each cubic shares its endpoint with the next, so
    (3*segs + 1) control points encode `segs` cubics."""
    n = len(ctrl)
    if n < 4:
        return []
    segs = (n - 1) // 3
    if segs < 1:
        return []
    cubs = []
    for s in range(segs):
        p0 = v3(ctrl[3 * s])
        p1 = v3(ctrl[3 * s + 1])
        p2 = v3(ctrl[3 * s + 2])
        p3 = v3(ctrl[3 * s + 3])
        cubs.append([list(p0), list(p1), list(p2), list(p3)])
    return cubs


def project_to_polyline(cp, pts, cum):
    """Find the arc-length on `pts` where the linear projection of `cp`
    onto the nearest polyline segment lands. Returns the refined
    arc-length, which can be anywhere within a segment — not pinned to
    sample indices. The split's `pos` stays as the original constraint
    world position so two strokes whose constraints share a world
    point produce nodes that mergeEps-merge into one."""
    best_d2 = float("inf")
    best_arc = 0.0
    for i in range(len(pts) - 1):
        ax, ay, az = pts[i]
        bx, by, bz = pts[i + 1]
        dx, dy, dz = bx - ax, by - ay, bz - az
        seg_len2 = dx * dx + dy * dy + dz * dz
        if seg_len2 < 1e-20:
            continue
        ux = cp[0] - ax
        uy = cp[1] - ay
        uz = cp[2] - az
        s = (ux * dx + uy * dy + uz * dz) / seg_len2
        if s < 0.0:
            s = 0.0
        elif s > 1.0:
            s = 1.0
        px = ax + s * dx
        py = ay + s * dy
        pz = az + s * dz
        d2 = (cp[0] - px) ** 2 + (cp[1] - py) ** 2 + (cp[2] - pz) ** 2
        if d2 < best_d2:
            best_d2 = d2
            seg_len = seg_len2**0.5
            best_arc = cum[i] + s * seg_len
    return best_arc


def stroke_splits(stroke, pts):
    """Convert each appliedPositionConstraint to a (arcLen, pos) split
    on the stroke's polyline. These are Unity's draw-time intersection
    snaps — the temporal record we trust over re-derived geometry.

    arc-length is sub-sample-precise via `project_to_polyline` so two
    constraints at the same world point on a curving stroke produce
    arc-lengths that match within polyline approximation error,
    avoiding the spurious node duplication that nearest-sample snap
    causes.

    Schema variants:
      old (e.g. 01-1-1): constraint is just `{x, y, z}` (no flags)
      new (e.g. hat):    `{position: {x,y,z}, isIntersection, ...}`
    """
    constraints = stroke.get("appliedPositionConstraints", [])
    if not constraints or len(pts) < 2:
        return []
    cum = [0.0]
    for i in range(1, len(pts)):
        dx = pts[i][0] - pts[i - 1][0]
        dy = pts[i][1] - pts[i - 1][1]
        dz = pts[i][2] - pts[i - 1][2]
        cum.append(cum[-1] + (dx * dx + dy * dy + dz * dz) ** 0.5)
    splits = []
    for c in constraints:
        if "position" in c:
            pos = c["position"]
        elif "x" in c:
            pos = c
        else:
            continue
        cp = (pos.get("x", 0.0), pos.get("y", 0.0), pos.get("z", 0.0))
        arc = project_to_polyline(cp, pts, cum)
        splits.append({"arcLen": arc, "pos": [cp[0], cp[1], cp[2]]})
    return splits


def stroke_polylines(raw_doc):
    polys = []
    for s in raw_doc.get("allSketchedStrokes", []):
        ctrl = s.get("ctrlPts", [])
        pts = flatten_ctrl_pts(ctrl, spp=8)
        if len(pts) >= 2:
            polys.append({
                "id": int(s["id"]),
                "pts": [[p[0], p[1], p[2]] for p in pts],
                "cubics": stroke_cubics(ctrl),
                "splits": stroke_splits(s, pts),
            })
    return polys


def run_probe(polylines_path):
    # Use `lake exe` so Lean's runtime DLL path is set correctly on
    # Windows; calling the .exe directly hits STATUS_DLL_NOT_FOUND.
    res = subprocess.run(
        ["lake", "exe", "arrangement_probe", f"--input={polylines_path}"],
        capture_output=True,
        text=True,
        timeout=180,
        cwd=str(LEAN_DIR),
        shell=True,
    )
    if res.returncode != 0:
        return None, (res.stderr.strip() or res.stdout.strip())[:200]
    # `lake exe` may interleave build/info messages on stdout; the probe
    # writes its single JSON line LAST, so take the final non-empty line
    # that parses as JSON.
    for line in reversed(res.stdout.strip().splitlines()):
        try:
            return json.loads(line), None
        except Exception:
            continue
    return None, f"no JSON line in stdout: {res.stdout!r}"


def ref_counts(ref_path):
    d = json.loads(ref_path.read_text())
    return {
        "nodes": len(d.get("nodes", [])),
        "segments": len(d.get("segments", [])),
        "strokes": len(d.get("strokes", [])),
    }


def main():
    probe_exe = LEAN_DIR / ".lake/build/bin/arrangement_probe.exe"
    if not probe_exe.exists():
        sys.exit(f"missing {probe_exe} — run `lake build arrangement_probe`")
    BENCH_OUT.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    n_ok = n_fail = 0
    print(f"sweeping {len(session_ids())} sessions…")
    with tempfile.TemporaryDirectory(prefix="cassie_arrangement_") as td:
        td_path = Path(td)
        for sid in session_ids():
            raw_path = RAW / f"{sid}.json"
            if not raw_path.exists():
                print(f"  {sid}: SKIP (no raw_data)")
                continue
            raw_doc = json.loads(raw_path.read_text())
            scale = session_canvas_scale(raw_doc)
            prox = UNITY_PROX_BASE * scale
            merge_eps = UNITY_MERGE_EPS_BASE * scale
            polys = stroke_polylines(raw_doc)
            tmp = td_path / f"{sid}.polylines.json"
            tmp.write_text(
                json.dumps({
                    "prox": prox,
                    "merge_eps": merge_eps,
                    "samples_per_cubic": SAMPLES_PER_CUBIC,
                    "strokes": polys,
                })
            )
            lean, err = run_probe(tmp)
            ref = ref_counts(REF / f"{sid}.json")
            if lean is None:
                n_fail += 1
                row = {
                    "id": sid,
                    "prox": prox,
                    "merge_eps": merge_eps,
                    "canvas_scale_median": scale,
                    "lean": None,
                    "ref": ref,
                    "err": err,
                }
                print(f"  {sid}: FAIL — {err}")
            else:
                n_ok += 1
                delta = {
                    "nodes": lean["nodes"] - ref["nodes"],
                    "edges": lean["edges"] - ref["segments"],
                }
                row = {
                    "id": sid,
                    "prox": prox,
                    "merge_eps": merge_eps,
                    "canvas_scale_median": scale,
                    "lean": lean,
                    "ref": ref,
                    "delta": delta,
                }
                print(
                    f"  {sid}: scale={scale:.3f} "
                    f"lean=({lean['nodes']}n,{lean['edges']}e) "
                    f"ref=({ref['nodes']}n,{ref['segments']}s) "
                    f"Δ=({delta['nodes']},{delta['edges']})"
                )
            rows.append(row)
    agg = {
        "lean_nodes_total": sum(r["lean"]["nodes"] for r in rows if r.get("lean")),
        "lean_edges_total": sum(r["lean"]["edges"] for r in rows if r.get("lean")),
        "ref_nodes_total": sum(r["ref"]["nodes"] for r in rows if "ref" in r),
        "ref_segments_total": sum(r["ref"]["segments"] for r in rows if "ref" in r),
        "exact_node_match": sum(1 for r in rows if r.get("lean") and r["lean"]["nodes"] == r["ref"]["nodes"]),
        "exact_edge_match": sum(1 for r in rows if r.get("lean") and r["lean"]["edges"] == r["ref"]["segments"]),
    }
    out = {
        "recorded": _dt.date.today().isoformat(),
        "fixture": "cassie-data sketch_graph (65 sessions)",
        "n_total": len(rows),
        "n_ok": n_ok,
        "n_failed": n_fail,
        "per_mesh": rows,
        "aggregate": agg,
        "notes": [
            "Lean side: buildArrangementAugmented with "
            "prox = 0.04 × median(canvasScale.x) and "
            "merge_eps = 0.01 × median(canvasScale.x), per Unity's "
            "CASSIEParameters defaults (SmallDistance × 2 for "
            "ProximityThreshold, × 0.5 for MergeConstraintsThreshold).",
            "spp=8 cubic-Bezier sampling per stroke. No per-mesh prox tuning. See memory: feedback_no-prox-tuning.md.",
            "Lean strokes can differ from ref strokes due to mirror twins "
            "recorded in raw_data but pruned from sketch_graph — counts may "
            "lean high.",
        ],
    }
    BENCH_OUT.write_text(json.dumps(out, indent=2))
    print(f"\nwrote {BENCH_OUT}")
    print(f"  exact node match: {agg['exact_node_match']}/{n_ok}")
    print(f"  exact edge match: {agg['exact_edge_match']}/{n_ok}")


if __name__ == "__main__":
    main()
