# Copyright (c) 2026 K. S. Ernest (iFire) Lee
# SPDX-License-Identifier: MIT
#
# Attach to a MeshInstance3D under xr-grid's `canvas` (sibling of the
# existing `strokes` ribbon MeshInstance3D). Listens to CassieStrokeRelay
# patch_added/patch_removed signals and replaces this node's mesh with
# the beautified patch surface. The ribbon stays as the live preview;
# this node carries the finalized arrangement output.

extends MeshInstance3D


func _ready() -> void:
	if mesh == null:
		mesh = ArrayMesh.new()
	var relay := get_node_or_null("/root/CassieStrokeRelay")
	if relay == null:
		return
	relay.patch_added.connect(_on_patch_added)
	relay.patch_removed.connect(_on_patch_removed)


func _on_patch_added(patch) -> void:
	if patch == null:
		return
	var m: Mesh = patch.get_mesh()
	if m == null:
		return
	# Append surfaces from the patch mesh onto the active ArrayMesh.
	# When a patch's signature gets removed we don't track per-surface
	# ownership — for now the manager rebuilds the union by clearing
	# and re-adding on every signal, which keeps the rendering side
	# simple (and works because CassieSurfaceManager keys patches by
	# canonical cycle signature so the active set is stable across
	# redundant calls).
	if mesh is ArrayMesh and m is ArrayMesh:
		var dst: ArrayMesh = mesh as ArrayMesh
		var src: ArrayMesh = m as ArrayMesh
		for s in range(src.get_surface_count()):
			dst.add_surface_from_arrays(
				src.surface_get_primitive_type(s),
				src.surface_get_arrays(s))


func _on_patch_removed(_patch) -> void:
	if mesh is ArrayMesh:
		(mesh as ArrayMesh).clear_surfaces()
	# Re-broadcast via patch_added for every active patch would belong
	# here; CassieSurfaceManager.get_patches() supplies the live list.
	var relay := get_node_or_null("/root/CassieStrokeRelay")
	if relay == null:
		return
	var sk: CassieSketcher = relay.get_sketcher()
	if sk == null:
		return
	var mgr: CassieSurfaceManager = sk.get_surface_manager()
	if mgr == null:
		return
	for p in mgr.get_patches():
		_on_patch_added(p)
