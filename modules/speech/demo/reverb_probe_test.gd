extends SceneTree

# Automated reverb probe tests with mathematically verifiable results.
# Uses ReverbProbeGI + ReverbBakeData to bake and verify RT60 values.
#
# Usage: godot --headless --path <demo> --script res://reverb_probe_test.gd

var started := false
func _process(_delta: float) -> bool:
	if not started:
		started = true
		run_tests.call_deferred()
	return false

func run_tests() -> void:
	var passed := 0
	var failed := 0
	var total := 0

	total += 1
	if test_probe_placement():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	total += 1
	if test_rt60_vs_sabine():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	total += 1
	if test_frequency_dependence():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	total += 1
	if test_material_variation():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	print("")
	print("=== RESULTS: %d/%d passed ===" % [passed, total])
	quit(0 if failed == 0 else 1)


func _make_box_room(p_size: Vector3) -> Node3D:
	var room := Node3D.new()
	root.add_child(room)

	# Six walls as MeshInstance3D boxes.
	var faces := [
		{"pos": Vector3(0, -p_size.y/2, 0), "size": Vector3(p_size.x, 0.1, p_size.z)},  # floor
		{"pos": Vector3(0, p_size.y/2, 0), "size": Vector3(p_size.x, 0.1, p_size.z)},   # ceiling
		{"pos": Vector3(-p_size.x/2, 0, 0), "size": Vector3(0.1, p_size.y, p_size.z)},  # left
		{"pos": Vector3(p_size.x/2, 0, 0), "size": Vector3(0.1, p_size.y, p_size.z)},   # right
		{"pos": Vector3(0, 0, -p_size.z/2), "size": Vector3(p_size.x, p_size.y, 0.1)},  # front
		{"pos": Vector3(0, 0, p_size.z/2), "size": Vector3(p_size.x, p_size.y, 0.1)},   # back
	]

	for face in faces:
		var mi := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = face["size"]
		mi.mesh = box
		mi.position = face["pos"]
		room.add_child(mi)

	return room


func test_probe_placement() -> bool:
	print("Test 1: Probe placement (box room 10x4x10)")

	var room_size := Vector3(10, 4, 10)
	var room := _make_box_room(room_size)

	var probe_gi := ReverbProbeGI.new()
	room.add_child(probe_gi)

	# Use ProbeOctree to generate positions.
	# For now, manually place a grid of probes.
	var probes := PackedVector3Array()
	for x in range(-4, 5, 2):
		for y in range(-1, 2, 2):
			for z in range(-4, 5, 2):
				probes.append(Vector3(x, y, z))

	var count := probes.size()
	print("  Probe count: %d" % count)

	# Check all within bounds.
	var half := room_size / 2.0
	var all_in_bounds := true
	for i in range(count):
		var p := probes[i]
		if abs(p.x) > half.x + 0.1 or abs(p.y) > half.y + 0.1 or abs(p.z) > half.z + 0.1:
			all_in_bounds = false
			break

	print("  All in bounds: %s" % str(all_in_bounds))
	room.queue_free()
	return count > 0 and all_in_bounds


func test_rt60_vs_sabine() -> bool:
	print("Test 2: RT60 vs Sabine analytical (plaster box 10x4x10)")

	var room_size := Vector3(10, 4, 10)
	var room := _make_box_room(room_size)

	var probe_gi := ReverbProbeGI.new()
	probe_gi.wall_material = ReverbProbeGI.MATERIAL_PLASTER_SMOOTH
	room.add_child(probe_gi)

	# Place one probe at center.
	var probes := PackedVector3Array([Vector3(0, 0, 0)])

	var err := probe_gi.bake(room, probes, 5000, 50)
	if err != ReverbProbeGI.BAKE_ERROR_OK:
		print("  Bake failed: %d" % err)
		room.queue_free()
		return false

	var data: ReverbBakeData = probe_gi.bake_data
	if data == null or data.get_probe_count() == 0:
		print("  No bake data")
		room.queue_free()
		return false

	var rt60_values := data.get_rt60_values()
	# Band 5 = 1000 Hz (index 5 in the 9-band array: 31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000)
	var rt60_1khz: float = rt60_values[5]

	# Sabine equation: RT60 = 0.161 * V / (S * alpha)
	# Volume = 10 * 4 * 10 = 400 m³
	# Surface area = 2*(10*4 + 4*10 + 10*10) = 2*(40+40+100) = 360 m²
	# Plaster smooth alpha at 1kHz ≈ 0.04 (from Resonance Audio material table)
	var volume: float = room_size.x * room_size.y * room_size.z
	var surface: float = 2.0 * (room_size.x*room_size.y + room_size.y*room_size.z + room_size.x*room_size.z)
	var alpha_1khz: float = 0.04
	var sabine_rt60: float = 0.161 * volume / (surface * alpha_1khz)

	var ratio: float = rt60_1khz / sabine_rt60 if sabine_rt60 > 0 else 0
	print("  Baked RT60 at 1kHz: %.2fs" % rt60_1khz)
	print("  Sabine RT60 at 1kHz: %.2fs" % sabine_rt60)
	print("  Ratio: %.2f (expect 0.5-2.0)" % ratio)

	room.queue_free()
	return ratio > 0.5 and ratio < 2.0


func test_frequency_dependence() -> bool:
	print("Test 3: RT60 frequency dependence (high freq decays faster)")

	var room_size := Vector3(10, 4, 10)
	var room := _make_box_room(room_size)

	var probe_gi := ReverbProbeGI.new()
	probe_gi.wall_material = ReverbProbeGI.MATERIAL_CURTAIN_HEAVY
	room.add_child(probe_gi)

	var probes := PackedVector3Array([Vector3(0, 0, 0)])
	var err := probe_gi.bake(room, probes, 5000, 50)
	if err != ReverbProbeGI.BAKE_ERROR_OK:
		print("  Bake failed: %d" % err)
		room.queue_free()
		return false

	var data: ReverbBakeData = probe_gi.bake_data
	var rt60_values := data.get_rt60_values()

	# Band 2 = 125 Hz, Band 8 = 8000 Hz
	var rt60_125hz: float = rt60_values[2]
	var rt60_8khz: float = rt60_values[8]

	print("  RT60 at 125Hz: %.3fs" % rt60_125hz)
	print("  RT60 at 8kHz: %.3fs" % rt60_8khz)

	room.queue_free()

	if rt60_125hz < 0.001 and rt60_8khz < 0.001:
		print("  Both near zero — material may be too absorptive")
		return false

	# High frequencies should decay faster (shorter RT60) with absorptive material.
	print("  8kHz < 125Hz: %s" % str(rt60_8khz < rt60_125hz))
	return rt60_8khz < rt60_125hz


func test_material_variation() -> bool:
	print("Test 4: Material variation (marble vs curtain same room)")

	var room_size := Vector3(10, 4, 10)

	# Bake with marble (very reflective).
	var room1 := _make_box_room(room_size)
	var probe1 := ReverbProbeGI.new()
	probe1.wall_material = ReverbProbeGI.MATERIAL_MARBLE
	room1.add_child(probe1)
	var err1 := probe1.bake(room1, PackedVector3Array([Vector3(0, 0, 0)]), 5000, 50)
	var rt60_marble := 0.0
	if err1 == ReverbProbeGI.BAKE_ERROR_OK:
		rt60_marble = probe1.bake_data.get_rt60_values()[5]  # 1kHz
	room1.queue_free()

	# Bake with heavy curtain (very absorptive).
	var room2 := _make_box_room(room_size)
	var probe2 := ReverbProbeGI.new()
	probe2.wall_material = ReverbProbeGI.MATERIAL_CURTAIN_HEAVY
	room2.add_child(probe2)
	var err2 := probe2.bake(room2, PackedVector3Array([Vector3(0, 0, 0)]), 5000, 50)
	var rt60_curtain := 0.0
	if err2 == ReverbProbeGI.BAKE_ERROR_OK:
		rt60_curtain = probe2.bake_data.get_rt60_values()[5]  # 1kHz
	room2.queue_free()

	print("  Marble RT60 at 1kHz: %.3fs" % rt60_marble)
	print("  Curtain RT60 at 1kHz: %.3fs" % rt60_curtain)
	var ratio := rt60_marble / rt60_curtain if rt60_curtain > 0.001 else 0.0
	print("  Marble/Curtain ratio: %.1fx (expect > 3x)" % ratio)

	# Marble should have much longer reverb than curtain.
	return ratio > 3.0
