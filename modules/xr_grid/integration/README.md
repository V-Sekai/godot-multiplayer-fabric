# xr-grid integration kit (legacy GDScript shims)

> **Status (2026-06-03):** these GDScript drop-ins are kept for reference
> only. The behaviors they used to provide are now native C++ classes in
> `modules/xr_grid/src/`. Wire CassieSketcher and the fabric directly
> through the registered classes — no GDScript autoload required.

## Native classes that replace these shims

| Old GDScript shim | Native replacement |
|---|---|
| `cassie_sketch_tool.gd` (CassieSketcher pump) | Instantiate `CassieSketcher` directly under your XR controller's node. The native `XRGridHand` C++ class also resolves its sketch tool via `sketch_tool_path` and drives `set_active` / `set_pressure` from the trigger. |
| `cassie_stroke_relay.gd` (autoload owning sketcher + fabric dispatch) | `XRGridFabricManager` owns the peer (`set_peer` or `connect_to_zone` for the http3 `WebTransportPeer` path) and emits `entity_received` for 100-byte packets. Connect that signal to `CassieSketcher.apply_remote_samples` directly, or via `XRGridStrokeChannel` for the dedicated reliable channel. |
| `cassie_patch_canvas.gd` (patch-add → mesh aggregation) | Still useful as scene glue — there's no native canvas-aggregation class. Connect the `patch_added` / `patch_removed` signals from `CassieSketcher` to whatever MeshInstance3D you want to render the union into. The kept-here file shows the pattern. |
| `main_with_cassie.tscn` (overlay that swaps SketchTool scripts) | The native `XRGridSketchTool` Node3D drops directly into a scene without script swapping. Add `CassieSketcher` as a sibling and wire the signals. |

## Determinism contract (unchanged)

xr-grid syncs **raw stroke samples** per peer; each peer reruns Beautify
locally on the broadcast samples and is expected to land on a
byte-identical FinalStroke + SketchGraph + patch mesh. The native side
enforces:

1. `creation_time` is `sample_index * sample_dt`, never wall-clock.
2. `CassieSketcher.set_async_triangulation(false)` so patches
   materialize on the same call on every peer.
3. The encoded packet's f32 quantization (in `CassieStrokePacket`) is
   bit-stable across encode → decode → encode cycles.
4. `CassieSketchGraph::find_cycles` emits cycles in canonical sorted
   order — order-invariant across HashMap rehashings or stroke
   insertion order.
5. Strict floating-point flags in `modules/cassie/SCsub` (`/fp:strict`
   on MSVC, `-ffp-model=strict` on Clang, `-frounding-math` on GCC).

## Wiring without GDScript autoload

Minimum native-only wiring for two-peer drawing:

```gdscript
# Run-once setup. No autoload needed.
var fm := XRGridFabricManager.new()
fm.name = "FabricManager"
add_child(fm)

# Hook the http3 WebTransport peer when the module is built.
var err = fm.connect_to_zone("127.0.0.1", 9000)
if err != OK:
    push_warning("WebTransport not registered — set_peer manually with ENet.")

# Per-session sketcher.
var sketcher := CassieSketcher.new()
sketcher.async_triangulation = false
add_child(sketcher)

# Route incoming stroke packets through apply_remote_samples (the
# 'CSP1' magic is what differentiates them from 100-byte entity packets).
fm.entity_received.connect(func(pkt: PackedByteArray):
    if pkt.size() >= 4 and pkt.decode_u32(0) == 0x31_50_53_43:
        sketcher.apply_remote_samples(pkt))

# Broadcast locally committed strokes back through the fabric.
sketcher.stroke_committed.connect(func(_sid, _fs, packet):
    fm.send_entity(packet))
```

## Determinism test suite

The engine module ships two doctest headers that exercise the
guarantees above:

- `modules/cassie/tests/test_cassie_beautify_determinism.h` —
  Beautify byte-identity, `CassieStrokePacket` round-trip, local-vs-
  remote replay byte-identity, `find_cycles` insertion-order invariance,
  malformed packet rejection.
- `modules/xr_grid/tests/test_xr_grid_wire.h` — Entity packet 100-byte
  round-trip, swing-twist Vector3 ↔ Quaternion ↔ i16x3 round-trip
  bound, BoolTimer wall-clock semantics.

Run with:

```
bin/godot.<plat>.editor.<arch>.console --headless --test \
    --test-case="*BeautifyDeterminism*"
bin/godot.<plat>.editor.<arch>.console --headless --test \
    --test-case="*XRGrid*"
```

## Out of scope

- Quest 3 ARM bit-determinism (x64 desktop first).
- Voice chat, undo/redo, brush picker, persistence.
- Persona is `XRGridCapsulePersona` (CapsuleMesh, settable color +
  height + radius). The upstream Sophia GLB is not vendored — drop a
  capsule into the scene anywhere the upstream referenced `sophia`.
