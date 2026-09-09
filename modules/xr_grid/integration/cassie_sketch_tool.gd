# Copyright (c) 2026 K. S. Ernest (iFire) Lee
# SPDX-License-Identifier: MIT
#
# Drop-in replacement for xr-grid's
# addons/procedural_3d_grid/core/simple_sketcher/sketch_tool.gd.
#
# Differences from the upstream version (kept compatible at the Node3D /
# @export property surface so main.tscn's NodePath / color binding still
# works):
#  - Pumps samples into a CassieSketcher (resolved via SKETCHER NodePath
#    or the autoloaded CassieStrokeRelay if unset) so the multiplayer
#    bit-deterministic Beautify chain runs per stroke.
#  - creation_time = sample_index * sample_dt — never Time.get_ticks_msec.
#  - On stroke end, asks the sketcher to commit + emits the encoded
#    PackedByteArray for the CassieStrokeRelay to broadcast.
#  - The local SimpleSketch ribbon preview is preserved as in-progress
#    visual until Beautify completes; the relay's patch_added handler
#    eventually replaces it with the canonical surface.

class_name CassieSketchTool extends Node3D

@export var CANVAS: NodePath
@onready var canvas: Node3D = get_node(CANVAS)

@export var active: bool = false
@export var pressure: float = 0.0
@export var color: Color = Color.BLACK

# Optional explicit path. If unset, we ask the CassieStrokeRelay autoload
# for the scene's shared sketcher.
@export var SKETCHER: NodePath

# Deterministic per-stroke sampling clock. Must equal the dt used by
# every other peer in the same session.
@export var sample_dt: float = 1.0 / 60.0

@onready var simple_sketch = SimpleSketch.new()

var _sketcher: CassieSketcher = null
var _current_stroke_id: int = -1
var _sample_index: int = 0
var prev_active: bool = false


func _ready() -> void:
	if canvas == null:
		printerr("CassieSketchTool: canvas missing")
		return
	simple_sketch.target_mesh = canvas.get_node("strokes").mesh
	_sketcher = get_node_or_null(SKETCHER) as CassieSketcher
	if _sketcher == null:
		var relay := get_node_or_null("/root/CassieStrokeRelay")
		if relay != null and relay.has_method("get_sketcher"):
			_sketcher = relay.get_sketcher()


func _process(_delta: float) -> void:
	if active and not prev_active:
		# Stroke begin — first sample goes in via begin_stroke so the
		# sketcher's per-stroke monotonic clock starts at index 0.
		simple_sketch.stroke_begin()
		_sample_index = 0
		var pos := _local_pos()
		if _sketcher != null:
			_current_stroke_id = _sketcher.begin_stroke(pos, pressure)
		_sample_index = 1

	if active and prev_active:
		var pos := _local_pos()
		simple_sketch.stroke_add(pos, pressure / canvas.scale.x, color)
		if _sketcher != null and _current_stroke_id >= 0:
			_sketcher.add_sample(_current_stroke_id, pos, pressure)
			_sample_index += 1

	if not active and prev_active:
		simple_sketch.stroke_end()
		if _sketcher != null and _current_stroke_id >= 0:
			var result: Dictionary = _sketcher.commit_stroke(_current_stroke_id)
			if result.get("ok", false):
				var packet: PackedByteArray = _sketcher.encode_stroke_packet(_current_stroke_id)
				var relay := get_node_or_null("/root/CassieStrokeRelay")
				if relay != null and relay.has_method("broadcast_stroke_packet"):
					relay.broadcast_stroke_packet(packet)
			_current_stroke_id = -1

	prev_active = active


func _local_pos() -> Vector3:
	return canvas.to_local(global_transform.origin)
