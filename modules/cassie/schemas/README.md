# CASSIE JSON-LD contexts

Six `@context` documents covering every JSON shape under `e:/cassie-data`
plus the in-tree dumper outputs. Attach a context to make the file
JSON-LD without rewriting its body.

**Source-format contexts** (one per directory under `cassie-data/data/`):

- **`cassie-raw.context.jsonld`** — `cassie-data/raw_data/*.json`. Full
  session export: `sketchSystem`, `sketchModel`, `interactionMode`,
  `systemStates` event log, `allSketchedStrokes`, `allCreatedPatches`,
  plus the nested `Vec3`/`Quat` and `PositionConstraint` records.

- **`cassie-sketch-history.context.jsonld`** —
  `cassie-data/sketch_history/*.json`. Per-stroke history with
  `input_samples`, `curve_type` (`polybezier` or `line`),
  `ctrl_pts` as a list of Bézier segments, plus
  `creation_time`/`deletion_time`.

- **`cassie-sketch-graph.context.jsonld`** —
  `cassie-data/sketch_graph/*.json`. Final-state connectivity: `strokes`
  (id + segment refs), `segments` (id + stroke_id + ctrl_pts + node
  refs), `nodes` (id + position + neighbor_edges).

- **`cassie-curves.context.jsonld`** — JSON-LD overlay for the
  `cassie-data/curves/*.curves` text format once parsed into JSON
  (`{polylines: [{count: N, points: [[x,y,z], ...]}]}`). The `.curves`
  files themselves are a simple line-oriented text format; see
  *Curves text → JSON* below.

**In-tree dumper contexts**:

- **`cassie-strokes.context.jsonld`** — standalone tessellated-stroke
  exports produced by the Blender overlay helpers (`hat_strokes.json`).
  Per-stroke `pts`/`mirrored`/`sourceId`/`mirrorPlaneX`.

- **`cassie-patches.context.jsonld`** — patch-mesh exports produced by
  `lake exe cycle_patch` (driven through `CASSIE_DUMP_PATCHES_JSON`)
  and `_dump_our_patches_json` on the C++ side.
  Per-patch `verts`/`faces`/`sids`.

## Vocabulary IRI

All terms resolve under `https://v-sekai.github.io/cassie/vocab#`. The
contexts also pull `xsd:` (`http://www.w3.org/2001/XMLSchema#`) for
scalar typing.

The vocabulary IRI is a placeholder — replace it with a hosted one
before publishing. Until then, contexts can be referenced inline or
served locally.

## Use

Attach the context to an existing payload without rewriting it. For the
raw export:

```json
{
  "@context": "modules/cassie/schemas/cassie-raw.context.jsonld",
  "sketchSystem": 2,
  "sketchModel": 0,
  "allSketchedStrokes": [...],
  "allCreatedPatches": [...],
  "systemStates": [...]
}
```

For the patch export:

```json
{
  "@context": "modules/cassie/schemas/cassie-patches.context.jsonld",
  "label": "lean-cycle_patch mode=8 prox=0.0017",
  "patches": [{ "verts": [[x,y,z], ...], "faces": [a,b,c, ...] }]
}
```

For the strokes export:

```json
{
  "@context": "modules/cassie/schemas/cassie-strokes.context.jsonld",
  "strokes": [
    { "pts": [[x,y,z], ...], "mirrored": false, "id": 4 },
    { "pts": [[x,y,z], ...], "mirrored": true,  "id": 4, "sourceId": 4, "mirrorPlaneX": 0.125 }
  ]
}
```

For the sketch history (one stroke object per file element):

```json
[
  { "@context": "modules/cassie/schemas/cassie-sketch-history.context.jsonld",
    "id": 3, "curve_type": "polybezier", "creation_time": 17.5,
    "input_samples": [[x,y,z], ...],
    "ctrl_pts": [ [[x0,y0,z0],[x1,y1,z1],[x2,y2,z2],[x3,y3,z3]], ... ] }
]
```

For the sketch graph:

```json
{
  "@context": "modules/cassie/schemas/cassie-sketch-graph.context.jsonld",
  "strokes": [{ "id": 3, "segments": [0,1,2] }],
  "segments": [{ "id": 0, "stroke_id": 3,
                 "ctrl_pts": [[[x0,y0,z0],[x1,y1,z1],[x2,y2,z2],[x3,y3,z3]]],
                 "nodes": [0, 1] }],
  "nodes": [{ "id": 0, "position": [x,y,z], "neighbor_edges": [0,5] }]
}
```

## Curves text → JSON

The `.curves` format is plain text: `v <count>` lines introducing each
polyline, followed by `count` `x y z` lines. Convert with:

```python
def curves_to_jsonld(path, ctx="modules/cassie/schemas/cassie-curves.context.jsonld"):
    polys = []
    with open(path) as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines):
        toks = lines[i].split()
        if toks[0] == "v":
            n = int(toks[1])
            pts = [list(map(float, lines[i+1+k].split())) for k in range(n)]
            polys.append({"count": n, "points": pts})
            i += n + 1
        else:
            i += 1
    return {"@context": ctx, "polylines": polys}
```

Validation:

```bash
pip install pyld
python -c "import json, pyld; print(pyld.jsonld.expand(json.load(open('hat.json'))))"
```

## Source-format quirks

- `nodes` is reused inside `sketch_graph` — top-level it's a list of
  `GraphNode` objects, on a segment it's a 2-element list of integer
  node IDs. JSON-LD expansion flattens both into the same IRI; consumers
  that need the distinction should reach into segments by structure
  rather than by property name.
- `position` appears both as a top-level field on `GraphNode` and inside
  every `PositionConstraint`. The IRIs differ
  (`cassie:nodePosition` vs `cassie:position`), so cross-document
  reasoners that load both contexts get distinct properties.
- `inputSamples` is recorded as `[x, y, z]` arrays in newer files and
  `{x, y, z}` objects in older ones — both contexts type it as
  `cassie:Vec3FlatTriple` and downstream tooling normalizes the shape.
- `.curves` text format is not JSON; convert with the snippet above
  before attaching the JSON-LD context.

## Property renames

Some JSON property names are friendlier than the runtime field names
they map to. The `@id` field of each term records the rename:

- `appliedPositionConstraints` → `cassie:appliedConstraints`
- `rejectedPositionConstraints` → `cassie:rejectedConstraints`
- `strokesID` → `cassie:strokeRefs`
- `headPos`/`headRot` → `cassie:headPosition`/`cassie:headRotation`
- `canvasPos`/`canvasRot` → `cassie:canvasPosition`/`cassie:canvasRotation`
- `primaryHandPos` → `cassie:primaryHandPosition`
- `elementID` → `cassie:elementRef`

Expansion produces a single fully-qualified graph; field-name drift
across older datasets (e.g. `inputSamples` as `[x,y,z]` arrays vs
`{x,y,z}` dicts noted in `raw_data/README.md`) is normalized away.
