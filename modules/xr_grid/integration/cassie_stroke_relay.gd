# Copyright (c) 2026 K. S. Ernest (iFire) Lee
# SPDX-License-Identifier: MIT
#
# CassieStrokeRelay — autoload that owns the per-session CassieSketcher
# and bridges between:
#   1. Local CassieSketchTool instances (one per controller hand) pushing
#      stroke samples into the sketcher.
#   2. xr-grid's WebTransport fabric — encoded stroke packets from
#      commit_stroke broadcast out to peers via the native
#      XRGridStrokeChannel; incoming packets with the 'CSP1' magic header
#      replay locally via apply_remote_samples.
#
# Add to project.godot as an autoload AFTER FabricManager so its _ready
# can wire the entity_received signal.
#
# This file is part of the integration kit for the vendored xr-grid
# subtree at modules/xr_grid/project/. It does NOT modify the upstream
# subtree — drop this autoload + cassie_sketch_tool.gd into your
# xr-grid checkout (or boot main_with_cassie.tscn directly) to wire
# CassieSketcher into the existing canvas.

extends Node

const CSP_MAGIC := 0x31_50_53_43  # 'CSP1' little-endian as int

# Shared sketcher node for the scene. Created lazily on first request.
var _sketcher: CassieSketcher = null

# Native reliable stroke channel. When the FabricManager-managed
# WebTransportPeer is available, we attach it here so stroke packets
# go out on a dedicated reliable transfer channel (separate from the
# unreliable transform fan-out).
var _stroke_channel: XRGridStrokeChannel = null

signal patch_added(patch)
signal patch_removed(patch)
signal stroke_committed(final_stroke)


func _ready() -> void:
	get_sketcher()

	_stroke_channel = XRGridStrokeChannel.new()
	_stroke_channel.stroke_packet_received.connect(_on_stroke_packet_received)

	# Wire into FabricManager if it's there. The integration tolerates the
	# autoload not being present (e.g. flatscreen-only smoke run).
	var fabric := get_node_or_null("/root/FabricManager")
	if fabric != null:
		if fabric.has_signal("entity_received"):
			fabric.entity_received.connect(_on_entity_received)
		# When FabricManager exposes its WebTransportPeer, hand it to the
		# native stroke channel so reliable-mode stroke broadcasts skip
		# the GDScript send_entity path entirely.
		if "_peer" in fabric and fabric._peer != null:
			_stroke_channel.peer = fabric._peer


func _process(_delta: float) -> void:
	if _stroke_channel != null:
		_stroke_channel.poll()


func get_sketcher() -> CassieSketcher:
	if _sketcher == null:
		_sketcher = CassieSketcher.new()
		_sketcher.name = "CassieSketcher"
		# Determinism: multiplayer peers rely on patches landing on the
		# same frame everywhere; the async path defers materialization.
		_sketcher.set_async_triangulation(false)
		_sketcher.stroke_committed.connect(_on_stroke_committed)
		_sketcher.patch_added.connect(_on_patch_added)
		_sketcher.patch_removed.connect(_on_patch_removed)
		add_child(_sketcher)
	return _sketcher


# Called by CassieSketchTool after commit_stroke succeeds. Forwards the
# packet through the native reliable channel when one is attached, with
# a GDScript-side fallback for early-init scenarios.
func broadcast_stroke_packet(packet: PackedByteArray) -> void:
	if packet.size() == 0:
		return
	if _stroke_channel != null and _stroke_channel.peer != null:
		var err := _stroke_channel.send_stroke(packet)
		if err == OK:
			return
	# Fallback: ask FabricManager to push the bytes via its existing
	# send_entity (unreliable). Stroke delivery is best-effort if the
	# native channel isn't attached yet — still better than dropping.
	var fabric := get_node_or_null("/root/FabricManager")
	if fabric != null and fabric.has_method("send_entity"):
		fabric.send_entity(packet)


func _on_entity_received(packet: PackedByteArray) -> void:
	# When the native channel is polling we don't see entity_received for
	# stroke packets at all — XRGridStrokeChannel.poll consumes them. This
	# path handles the GDScript-only fabric loop where the relay is the
	# sole arbiter of incoming CSP1 frames.
	if packet.size() < 4:
		return
	var magic: int = (int(packet[3]) << 24) | (int(packet[2]) << 16) | (int(packet[1]) << 8) | int(packet[0])
	if magic == CSP_MAGIC:
		get_sketcher().apply_remote_samples(packet)


func _on_stroke_packet_received(packet: PackedByteArray) -> void:
	get_sketcher().apply_remote_samples(packet)


func _on_stroke_committed(final_stroke) -> void:
	stroke_committed.emit(final_stroke)


func _on_patch_added(patch) -> void:
	patch_added.emit(patch)


func _on_patch_removed(patch) -> void:
	patch_removed.emit(patch)
