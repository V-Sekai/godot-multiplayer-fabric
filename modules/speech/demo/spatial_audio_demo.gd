extends SceneTree

# Animated spatial audio demo: HRTF + reverb probe walkthrough.
# Two connected rooms with different materials. A sound source orbits.
# The listener moves between rooms to hear reverb change.
#
# Usage: godot --path <demo> --script res://spatial_audio_demo.gd

var started := false
var time := 0.0

var audio_player: AudioStreamPlayer3D
var probe_gi: ReverbProbeGI
var camera: Camera3D
var listener_path: Array[Vector3] = []
var source_node: Node3D

func _process(delta: float) -> bool:
	if not started:
		started = true
		setup.call_deferred()
		return false

	time += delta
	_update_scene(delta)
	_fill_audio()

	# Run for 30 seconds then quit.
	if time > 30.0:
		print("Demo complete.")
		quit(0)
	return false

func setup() -> void:
	# Set up spatial audio bus.
	AudioServer.add_bus(1)
	AudioServer.set_bus_name(1, "Spatial")
	AudioServer.set_bus_send(1, "Master")
	AudioServer.set_bus_type(1, AudioServer.BUS_TYPE_SPATIAL_3D)

	# Build two rooms: marble hall (left) and carpeted room (right).
	var world := Node3D.new()
	root.add_child(world)

	# Create distinct visual materials so the material map can differentiate rooms.
	var marble_mat := StandardMaterial3D.new()
	marble_mat.albedo_color = Color(0.9, 0.85, 0.8)
	marble_mat.resource_name = "marble_surface"

	var carpet_mat := StandardMaterial3D.new()
	carpet_mat.albedo_color = Color(0.4, 0.2, 0.15)
	carpet_mat.resource_name = "carpet_surface"

	var corridor_mat := StandardMaterial3D.new()
	corridor_mat.albedo_color = Color(0.7, 0.7, 0.65)
	corridor_mat.resource_name = "corridor_plaster"

	_build_room(world, Vector3(-8, 2.5, 0), Vector3(10, 5, 8), marble_mat)
	_build_room(world, Vector3(8, 2.5, 0), Vector3(10, 5, 8), carpet_mat)
	_build_room(world, Vector3(0, 2.5, 0), Vector3(6, 5, 3), corridor_mat)

	# Camera starts in marble hall, will move to carpeted room.
	camera = Camera3D.new()
	camera.current = true
	camera.position = Vector3(-8, 1.5, 0)
	world.add_child(camera)

	# Reverb probe GI attached to camera.
	probe_gi = ReverbProbeGI.new()
	probe_gi.wall_material = ReverbProbeGI.MATERIAL_PLASTER_SMOOTH

	# Map visual materials to acoustic materials.
	var mat_map := ResonanceAudioMaterialMap.new()
	mat_map.set_material_mapping("marble_surface", ReverbProbeGI.MATERIAL_MARBLE)
	mat_map.set_material_mapping("carpet_surface", ReverbProbeGI.MATERIAL_CURTAIN_HEAVY)
	mat_map.set_material_mapping("corridor_plaster", ReverbProbeGI.MATERIAL_PLASTER_SMOOTH)
	probe_gi.material_map = mat_map

	camera.add_child(probe_gi)

	# Sound source — orbits in the center.
	source_node = Node3D.new()
	world.add_child(source_node)

	audio_player = AudioStreamPlayer3D.new()
	audio_player.bus = "Spatial"
	source_node.add_child(audio_player)

	var gen := AudioStreamGenerator.new()
	gen.mix_rate = 48000
	gen.buffer_length = 1.0
	audio_player.stream = gen
	audio_player.play()

	# Place probes across both rooms.
	var probes := PackedVector3Array()
	for x in range(-12, 13, 3):
		for y in [1, 3]:
			for z in range(-4, 5, 3):
				probes.append(Vector3(x, y, z))
	print("Baking %d reverb probes..." % probes.size())
	var err := probe_gi.bake(world, probes, 10000, 50)
	if err == ReverbProbeGI.BAKE_ERROR_OK:
		print("Bake complete.")
		_print_probe_stats()
	else:
		print("Bake error: %d" % err)

	# Define listener path: marble hall → corridor → carpeted room → back.
	listener_path = [
		Vector3(-8, 1.5, 0),   # Marble hall center
		Vector3(-4, 1.5, 0),   # Marble hall edge
		Vector3(0, 1.5, 0),    # Corridor
		Vector3(4, 1.5, 0),    # Carpeted room edge
		Vector3(8, 1.5, 0),    # Carpeted room center
		Vector3(4, 1.5, 0),    # Back through corridor
		Vector3(0, 1.5, 0),
		Vector3(-4, 1.5, 0),
		Vector3(-8, 1.5, 0),
	]
	print("Listener moves through: marble hall → corridor → carpeted room → back")
	print("Sound source orbits at the origin with HRTF spatialization")

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
		box.material = mat
		mi.mesh = box
		mi.position = pos + face["off"]
		p_parent.add_child(mi)

func _update_scene(_delta: float) -> void:
	if not camera or listener_path.is_empty():
		return

	# Move listener along path over 24 seconds (3s per segment).
	var path_time := fmod(time, 24.0)
	var segment := int(path_time / 3.0) % (listener_path.size() - 1)
	var t := fmod(path_time, 3.0) / 3.0
	var from := listener_path[segment]
	var to := listener_path[segment + 1]
	camera.position = from.lerp(to, t)

	# Orbit the sound source around the origin.
	var angle := time * 0.5
	source_node.position = Vector3(cos(angle) * 3.0, 1.5, sin(angle) * 3.0)

	# Print position every 3 seconds.
	if int(time * 10) % 30 == 0:
		var room := "marble hall" if camera.position.x < -2 else ("corridor" if camera.position.x < 2 else "carpeted room")
		print("  t=%.0fs listener=(%.1f,%.1f,%.1f) [%s] source=(%.1f,%.1f,%.1f)" % [
			time, camera.position.x, camera.position.y, camera.position.z, room,
			source_node.position.x, source_node.position.y, source_node.position.z])

var _phase := 0.0

func _fill_audio() -> void:
	if audio_player == null or not audio_player.is_playing():
		return
	var playback := audio_player.get_stream_playback() as AudioStreamGeneratorPlayback
	if playback == null:
		return

	var frames := playback.get_frames_available()
	for i in range(frames):
		# Percussive click every 0.4s + quiet sustained tone.
		var click := 0.0
		var sample_in_period := int(_phase * 48000.0 / (2.0 * PI)) % 19200
		if sample_in_period < 96:
			click = sin(float(sample_in_period) * 2.0 * PI * 800.0 / 48000.0) * 0.4
			click *= 1.0 - float(sample_in_period) / 96.0
		# Quiet sustained tone for spatial tracking.
		var tone := sin(_phase * 220.0) * 0.05
		var val := click + tone
		playback.push_frame(Vector2(val, val))
		_phase += 2.0 * PI / 48000.0
		if _phase > 2.0 * PI:
			_phase -= 2.0 * PI

func _print_probe_stats() -> void:
	if not probe_gi or not probe_gi.bake_data:
		return
	var data: ReverbBakeData = probe_gi.bake_data
	var rt60 := data.get_rt60_values()
	var gains := data.get_gains()
	var count := data.get_probe_count()

	var min_rt60 := 999.0
	var max_rt60 := 0.0
	var min_gain := 999.0
	var max_gain := 0.0
	for i in range(count):
		var r: float = rt60[i * 9 + 5]  # 1kHz band
		var g: float = gains[i]
		if r < min_rt60: min_rt60 = r
		if r > max_rt60: max_rt60 = r
		if g < min_gain: min_gain = g
		if g > max_gain: max_gain = g

	print("  Probes: %d, RT60@1kHz: %.2f-%.2fs, Gain: %.4f-%.4f" % [count, min_rt60, max_rt60, min_gain, max_gain])
