extends SceneTree

# GPU bake profiling script — generates a dense scene targeting ~5s dispatch.
# Usage: godot --path <demo> --script res://gpu_bake_profile.gd

var started := false
func _process(_delta: float) -> bool:
	if not started:
		started = true
		run_profile.call_deferred()
	return false

func run_profile() -> void:
	print("=== GPU Bake Profile ===")

	# Build a room with subdivided walls for ~5000 triangles.
	var room := Node3D.new()
	root.add_child(room)

	var room_size := Vector3(20, 8, 20)

	# Floor and ceiling: high-res PlaneMesh for dense geometry.
	for y in [-room_size.y / 2.0, room_size.y / 2.0]:
		var mi := MeshInstance3D.new()
		var plane := PlaneMesh.new()
		plane.size = Vector2(room_size.x, room_size.z)
		plane.subdivide_width = 30
		plane.subdivide_depth = 30
		mi.mesh = plane
		mi.position = Vector3(0, y, 0)
		if y > 0:
			mi.rotation_degrees.x = 180
		room.add_child(mi)

	# Four walls: subdivided quads.
	var wall_configs := [
		{"pos": Vector3(-room_size.x / 2, 0, 0), "size": Vector2(room_size.z, room_size.y), "rot": Vector3(0, 90, 0)},
		{"pos": Vector3(room_size.x / 2, 0, 0), "size": Vector2(room_size.z, room_size.y), "rot": Vector3(0, -90, 0)},
		{"pos": Vector3(0, 0, -room_size.z / 2), "size": Vector2(room_size.x, room_size.y), "rot": Vector3(90, 0, 0)},
		{"pos": Vector3(0, 0, room_size.z / 2), "size": Vector2(room_size.x, room_size.y), "rot": Vector3(-90, 0, 0)},
	]
	for cfg in wall_configs:
		var mi := MeshInstance3D.new()
		var plane := PlaneMesh.new()
		plane.size = cfg["size"]
		plane.subdivide_width = 20
		plane.subdivide_depth = 20
		mi.mesh = plane
		mi.position = cfg["pos"]
		mi.rotation_degrees = cfg["rot"]
		room.add_child(mi)

	# Add interior objects (pillars, boxes) for geometric complexity.
	for x in range(-3, 4, 3):
		for z in range(-3, 4, 3):
			if x == 0 and z == 0:
				continue
			var mi := MeshInstance3D.new()
			var box := BoxMesh.new()
			box.size = Vector3(0.5, room_size.y * 0.6, 0.5)
			box.subdivide_width = 4
			box.subdivide_height = 8
			box.subdivide_depth = 4
			mi.mesh = box
			mi.position = Vector3(x, 0, z)
			room.add_child(mi)

	# Count triangles.
	var tri_count := 0
	for child in room.get_children():
		if child is MeshInstance3D:
			var mesh: Mesh = child.mesh
			for s in range(mesh.get_surface_count()):
				var arrays := mesh.surface_get_arrays(s)
				var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
				if idx.size() > 0:
					tri_count += idx.size() / 3
				else:
					var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
					tri_count += verts.size() / 3
	print("Scene: %d triangles, room %s" % [tri_count, room_size])

	# Place probes in a grid.
	var probes := PackedVector3Array()
	for x in range(-6, 7, 4):
		for y in range(-2, 3, 4):
			for z in range(-6, 7, 4):
				probes.append(Vector3(x, y, z))
	print("Probes: %d" % probes.size())

	var probe_gi := ReverbProbeGI.new()
	probe_gi.wall_material = ReverbProbeGI.MATERIAL_PLASTER_SMOOTH
	room.add_child(probe_gi)

	# Target ~5s: start with 50000 rays, 100 bounces.
	var ray_count := 185000
	var max_bounces := 100
	print("Rays: %d, Bounces: %d" % [ray_count, max_bounces])
	print("")

	var err := probe_gi.bake(room, probes, ray_count, max_bounces)
	if err != ReverbProbeGI.BAKE_ERROR_OK:
		print("Bake failed: %d" % err)
	else:
		var data: ReverbBakeData = probe_gi.bake_data
		var rt60 := data.get_rt60_values()
		print("")
		print("Sample RT60 at 1kHz (band 5):")
		for i in range(min(probes.size(), 5)):
			print("  Probe %d at %s: %.3fs" % [i, probes[i], rt60[i * 9 + 5]])

	room.queue_free()
	quit()
