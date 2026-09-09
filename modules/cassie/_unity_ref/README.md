# CASSIE Unity reference — read-only snapshots

These are verbatim copies of the canonical CASSIE Unity scripts, pulled
via the Unity MCP from the `VRSketching` scene's project on
`2026-06-02`. They are the **authoritative reference** for the Lean
forward pipeline port (Phase B.0 in
`C:\Users\ernest.lee\.claude\plans\playful-marinating-harp.md`).

Do not edit. Re-pull via `mcp__ai-game-developer__assets-get-data` if
the Unity project changes.

## File map — forward pipeline

| File | Role | Lean target |
|---|---|---|
| `CycleDetection.cs` | The two `DetectCycle(...)` entry points: per-segment teacher walk + guided-by-input-position walk. Sharp-node branching, `ShouldReverse`, `TransportAcrossNode`. | `CassieAvbd/CycleDetect/Walk.lean` |
| `INode.cs` / `Node.cs` | Node interface + private impl. CCW-sorted `Neighbors`, `IsSharp` from `Utils.FitPlane(...)` error > 0.5, `GetNext/GetPrevious/GetInPlane`. | `CassieAvbd/CycleDetect/Graph.lean` (extend `Node` with `normal`, `isSharp`, sorted neighbors) |
| `ISegment.cs` / `Segment.cs` | Segment interface + private impl. `GetTangentAt(node)`, `Transport(v, node)` via `FinalStroke.ParallelTransport`, `ProjectInPlane(node, normal)`, `IsInReverse(node)`. | `CassieAvbd/CycleDetect/Graph.lean` (extend `Edge` with curve-tangent / parallel-transport hooks) |
| `Graph.cs` | The main graph state: nodes/segments dictionaries, cycle dedupe via `Cycle.HashCode`, segment-to-cycles inverted index, `TryFindAllCycles`/`TryFindCycleAt` driver. | `CassieAvbd/CycleDetect/Arrangement.lean` (add cycle dedupe by sorted segment-id list) |
| `Cycle.cs` / `HalfSegment.cs` / `SegmentCycles.cs` | The cycle data structure (LinkedList<HalfSegment>, hash by sorted segment IDs), per-segment cycle index, half-segment orientation flag. | new `CassieAvbd/CycleDetect/Cycle.lean` |
| `FinalStroke.cs` | Stroke owning the cubic polybezier `Curve` + its segments list. `ParallelTransport(v, fromParam, toParam)` is delegated to `Curve.ParallelTransport`. | new `CassieAvbd/CycleDetect/Stroke.lean` (curve sampling + parallel transport along bezier) |
| `SurfaceManager.cs` | Calls the `Triangulation_dll` (the CGAL+Zou-2013 wrapper) on the boundary samples of each cycle to produce the patch mesh; manages a `Dictionary<int, SurfacePatch>` of created patches. | already covered by `cycle_patch` (geogram CDT2d) + `cassie_pmp_ffi` (PMP smoothing) — the algorithms differ but the role is the same |
| `SurfacePatch.cs` | MonoBehaviour wrapping the meshed patch. Projection / closest-point / normal queries via `MeshCollider`. | not needed for the forward port — Lean side stores per-patch mesh JSON; the projection queries are a runtime-only concern. |

## Algorithm pieces missing from our current Lean port

1. **Per-step parallel transport** of the normal along each segment.
   Unity uses `segment.Transport(normal, node)` → `stroke.ParallelTransport(v, fromParam, toParam)` which uses the curve's per-arc-length Frenet-style transport.
2. **`TransportAcrossNode`** — rotation that aligns the previous segment's outbound tangent with the next segment's inbound tangent, applied to the normal. (Across a curve discontinuity at a node.)
3. **Sharp-node branch** (`IsSharp` + `GetInPlane`) — selects the next segment by best in-plane projection rather than CCW angular order. A node is sharp when `Utils.FitPlane(tangents)` returns error > 0.5 (= cos 60°, i.e. the tangents disagree with their best-fit plane by more than 60°).
4. **`ShouldReverse`** — orientation flip when transported normals at consecutive nodes disagree by < 0.5 (= cos 60°). The `reversed` bool toggles `GetNext` vs `GetPrevious` at non-trivial nodes.
5. **Eulerian / manifold guards** — `Contains(cycle, currentSegment)` and `ExistingCyclesCount(currentSegment) >= 2`. Cycles dedupe by `Cycle.HashCode = hash(sorted segment IDs)`.

Items 1–4 together produce the "extra" patches our current `Walk.lean`
misses (50 vs Unity's 234 cumulative). Item 5 we mostly have via the
union-by-edge-set step in `cycle_patch`.
