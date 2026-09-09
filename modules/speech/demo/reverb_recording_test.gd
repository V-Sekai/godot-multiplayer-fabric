extends SceneTree

# Records audio at different listener positions to WAV files.
# Proves reverb variation is audible by capturing output in each room.
#
# Usage: godot --headless --path <demo> --script res://reverb_recording_test.gd

var started := false
func _process(_delta: float) -> bool:
	if not started:
		started = true
		run.call_deferred()
	return false

func run() -> void:
	AudioServer.add_bus(1)
	AudioServer.set_bus_name(1, "Spatial")
	AudioServer.set_bus_send(1, "Master")
	AudioServer.set_bus_type(1, AudioServer.BUS_TYPE_SPATIAL_3D)

	var world := Node3D.new()
	root.add_child(world)

	var marble_mat := StandardMaterial3D.new()
	marble_mat.resource_name = "marble_surface"
	var carpet_mat := StandardMaterial3D.new()
	carpet_mat.resource_name = "carpet_surface"

	_build_room(world, Vector3(-8, 2.5, 0), Vector3(10, 5, 8), marble_mat)
	_build_room(world, Vector3(8, 2.5, 0), Vector3(10, 5, 8), carpet_mat)
	_build_room(world, Vector3(0, 2.5, 0), Vector3(6, 5, 3), null)

	var camera := Camera3D.new()
	camera.current = true
	world.add_child(camera)

	var probe_gi := ReverbProbeGI.new()
	probe_gi.wall_material = ReverbProbeGI.MATERIAL_PLASTER_SMOOTH
	var mat_map := ResonanceAudioMaterialMap.new()
	mat_map.set_material_mapping("marble_surface", ReverbProbeGI.MATERIAL_MARBLE)
	mat_map.set_material_mapping("carpet_surface", ReverbProbeGI.MATERIAL_CURTAIN_HEAVY)
	probe_gi.material_map = mat_map
	camera.add_child(probe_gi)

	var probes := PackedVector3Array()
	for x in range(-12, 13, 3):
		for y in [1, 3]:
			for z in range(-4, 5, 3):
				probes.append(Vector3(x, y, z))
	probe_gi.bake(world, probes, 10000, 50)

	# Sound source — follows the camera (1m in front).
	var player := AudioStreamPlayer3D.new()
	player.bus = "Spatial"
	world.add_child(player)

	var gen := AudioStreamGenerator.new()
	gen.mix_rate = 48000
	gen.buffer_length = 0.5
	player.stream = gen
	player.play()

	var positions := {
		"marble_hall": Vector3(-8, 1.5, 0),
		"corridor": Vector3(0, 1.5, 0),
		"carpeted_room": Vector3(8, 1.5, 0),
	}

	print("=== Reverb Recording Test ===")
	print("")

	for room_name in positions:
		var pos: Vector3 = positions[room_name]
		camera.position = pos
		player.position = pos + Vector3(0.5, 0, 0)  # Source 0.5m to the right.
		probe_gi.position = Vector3.ZERO

		# Wait for position to propagate.
		for i in range(20):
			await process_frame

		print("Recording: %s at %s" % [room_name, str(pos)])
		var wav := await _record_impulse(player, 5.0)
		if wav:
			var path := "C:/Users/ernest.lee/Downloads/reverb_%s.wav" % room_name
			wav.save_to_wav(path)
			var energy := _measure_energy(wav)
			print("  Saved: %s" % path)
			print("  Energy: %.6f (%.1f dB)" % [energy, 10.0 * log(max(energy, 1e-20)) / log(10.0)])
			var tail_energy := _measure_tail_energy(wav, 0.1)
			print("  Tail energy (after 100ms): %.6f (%.1f dB)" % [tail_energy, 10.0 * log(max(tail_energy, 1e-20)) / log(10.0)])
			var ratio: float = tail_energy / max(energy, 1e-20)
			print("  Tail/Total ratio: %.4f" % ratio)
		print("")

	print("=== Done ===")
	quit(0)

func _record_impulse(player: AudioStreamPlayer3D, duration: float) -> AudioStreamWAV:
	var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()

	var record := AudioEffectRecord.new()
	AudioServer.add_bus_effect(0, record)
	record.set_recording_active(true)

	var sample_rate := 48000.0
	var total_frames := int(duration * sample_rate)
	var frame := 0
	var rng := RandomNumberGenerator.new()
	rng.seed = 42

	while frame < total_frames:
		await process_frame
		var avail := playback.get_frames_available()
		for i in range(avail):
			var val := 0.0
			if frame < 480:
				# 10ms white noise burst at full volume (like a handclap).
				val = rng.randf_range(-1.0, 1.0) * 0.9
				val *= 1.0 - float(frame) / 480.0
			playback.push_frame(Vector2(val, val))
			frame += 1

	# Extra silence for tail.
	for i in range(50):
		await process_frame
		for j in range(playback.get_frames_available()):
			playback.push_frame(Vector2(0, 0))

	record.set_recording_active(false)
	var wav: AudioStreamWAV = record.get_recording()
	AudioServer.remove_bus_effect(0, AudioServer.get_bus_effect_count(0) - 1)
	return wav

func _measure_energy(wav: AudioStreamWAV) -> float:
	var data := wav.data
	var num_samples: int = data.size() / 4
	var energy := 0.0
	for i in range(num_samples):
		var l_raw: int = data[i * 4] | (data[i * 4 + 1] << 8)
		if l_raw > 32767: l_raw -= 65536
		var r_raw: int = data[i * 4 + 2] | (data[i * 4 + 3] << 8)
		if r_raw > 32767: r_raw -= 65536
		energy += (float(l_raw) / 32768.0) ** 2 + (float(r_raw) / 32768.0) ** 2
	return energy / max(num_samples, 1)

func _measure_tail_energy(wav: AudioStreamWAV, after_seconds: float) -> float:
	var data := wav.data
	var num_samples: int = data.size() / 4
	var start_sample: int = int(after_seconds * 48000)
	if start_sample >= num_samples:
		return 0.0
	var energy := 0.0
	var count := 0
	for i in range(start_sample, num_samples):
		var l_raw: int = data[i * 4] | (data[i * 4 + 1] << 8)
		if l_raw > 32767: l_raw -= 65536
		var r_raw: int = data[i * 4 + 2] | (data[i * 4 + 3] << 8)
		if r_raw > 32767: r_raw -= 65536
		energy += (float(l_raw) / 32768.0) ** 2 + (float(r_raw) / 32768.0) ** 2
		count += 1
	return energy / max(count, 1)

func _build_room(p_parent: Node3D, pos: Vector3, size: Vector3, mat: StandardMaterial3D) -> void:
	var faces := [
		{"off": Vector3(0, -size.y/2, 0), "sz": Vector3(size.x, 0.1, size.z)},
		{"off": Vector3(0, size.y/2, 0), "sz": Vector3(size.x, 0.1, size.z)},
		{"off": Vector3(-size.x/2, 0, 0), "sz": Vector3(0.1, size.y, size.z)},
		{"off": Vector3(size.x/2, 0, 0), "sz": Vector3(0.1, size.y, size.z)},
		{"off": Vector3(0, 0, -size.z/2), "sz": Vector3(size.x, size.y, 0.1)},
		{"off": Vector3(0, 0, size.z/2), "sz": Vector3(size.x, size.y, 0.1)},
	]
	for face in faces:
		var mi := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = face["sz"]
		if mat:
			box.material = mat
		mi.mesh = box
		mi.position = pos + face["off"]
		p_parent.add_child(mi)
