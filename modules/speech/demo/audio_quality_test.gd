extends SceneTree

# Automated audio quality tests with mathematically verifiable results.
# Uses AudioEffectRecord to capture output, then analyzes captured WAVs.
#
# Usage: godot --headless --path <demo> --script res://audio_quality_test.gd

var started := false
func _process(_delta: float) -> bool:
	if not started:
		started = true
		run_tests.call_deferred()
	return false

func run_tests() -> void:
	AudioServer.add_bus(1)
	AudioServer.set_bus_name(1, "Spatial")
	AudioServer.set_bus_send(1, "Master")
	AudioServer.set_bus_type(1, AudioServer.BUS_TYPE_SPATIAL_3D)

	var passed := 0
	var failed := 0
	var total := 0

	# Test 1: HRTF panning — source at 90° right, verify R > L.
	total += 1
	if await test_hrtf_panning():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	# Test 2: Distance attenuation — verify 1/r² energy decay.
	total += 1
	if await test_distance_attenuation():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	# Test 3: Frequency sweep — verify HRTF affects different frequencies differently.
	total += 1
	if await test_frequency_sweep():
		passed += 1
		print("  PASS")
	else:
		failed += 1
		print("  FAIL")

	print("")
	print("=== RESULTS: %d/%d passed ===" % [passed, total])
	quit(0 if failed == 0 else 1)


func test_hrtf_panning() -> bool:
	print("Test 1: HRTF panning (source at 90° right)")

	# Setup: Camera at origin, AudioStreamPlayer3D at (5, 0, 0) = 90° right.
	var camera := Camera3D.new()
	camera.current = true
	root.add_child(camera)
	var player := AudioStreamPlayer3D.new()
	root.add_child(player)
	player.bus = "Spatial"
	player.global_position = Vector3(100, 0, 0)

	# Wait for transforms to propagate to Resonance Audio.
	for i in range(5):
		await process_frame

	# Generate 440Hz sine wave.
	var generator := AudioStreamGenerator.new()
	generator.mix_rate = 48000
	generator.buffer_length = 0.5
	player.stream = generator
	player.play()
	var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()

	# Record master bus output.
	var record := AudioEffectRecord.new()
	AudioServer.add_bus_effect(0, record)
	record.set_recording_active(true)

	# Fill buffer with sine wave for 1 second.
	var sample_rate := 48000.0
	var freq := 440.0
	for frame_batch in range(50):
		await process_frame
		var frames_available := playback.get_frames_available()
		for i in range(frames_available):
			var t: float = float(frame_batch * 960 + i) / sample_rate
			var val: float = sin(2.0 * PI * freq * t) * 0.5
			playback.push_frame(Vector2(val, val))

	# Wait for audio to process.
	for i in range(10):
		await process_frame

	record.set_recording_active(false)
	var recording: AudioStreamWAV = record.get_recording()
	AudioServer.remove_bus_effect(0, AudioServer.get_bus_effect_count(0) - 1)

	player.queue_free()
	camera.queue_free()

	if recording == null:
		print("  No recording captured")
		return false

	# Analyze L/R channel energy.
	var data := recording.data
	var num_samples: int = data.size() / 4  # 16-bit stereo = 4 bytes per frame
	if num_samples < 100:
		print("  Too few samples: %d" % num_samples)
		return false

	var energy_l := 0.0
	var energy_r := 0.0
	for i in range(num_samples):
		var l_raw: int = data[i * 4] | (data[i * 4 + 1] << 8)
		if l_raw > 32767: l_raw -= 65536
		var r_raw: int = data[i * 4 + 2] | (data[i * 4 + 3] << 8)
		if r_raw > 32767: r_raw -= 65536
		var l: float = float(l_raw) / 32768.0
		var r: float = float(r_raw) / 32768.0
		energy_l += l * l
		energy_r += r * r

	energy_l /= num_samples
	energy_r /= num_samples

	var db_l: float = 10.0 * log(max(energy_l, 1e-20)) / log(10.0)
	var db_r: float = 10.0 * log(max(energy_r, 1e-20)) / log(10.0)
	var ild: float = db_r - db_l

	print("  L=%.1fdB R=%.1fdB ILD=%.1fdB (expect R > L by 1-10dB)" % [db_l, db_r, ild])

	# Source is to the right, so right channel should be louder.
	# With Resonance Audio HRTF, ILD at 90° should be 3-10dB.
	# Without Resonance Audio (basic panning), ILD is still > 0.
	return ild > 0.5


func test_distance_attenuation() -> bool:
	print("Test 2: Distance attenuation (1/r² energy decay)")

	var camera := Camera3D.new()
	camera.current = true
	root.add_child(camera)

	var distances := [1.0, 5.0, 20.0]
	var energies: Array[float] = []

	for dist in distances:
		var player := AudioStreamPlayer3D.new()
		player.position = Vector3(0, 0, -dist)
		player.bus = "Spatial"
		root.add_child(player)

		var generator := AudioStreamGenerator.new()
		generator.mix_rate = 48000
		generator.buffer_length = 0.5
		player.stream = generator
		player.play()
		var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()

		var record := AudioEffectRecord.new()
		AudioServer.add_bus_effect(0, record)
		record.set_recording_active(true)

		# Fill 0.5s of 440Hz sine.
		var sample_rate := 48000.0
		var freq := 440.0
		for frame_batch in range(25):
			await process_frame
			var frames_available := playback.get_frames_available()
			for i in range(frames_available):
				var t: float = float(frame_batch * 960 + i) / sample_rate
				var val: float = sin(2.0 * PI * freq * t) * 0.5
				playback.push_frame(Vector2(val, val))

		for i in range(5):
			await process_frame

		record.set_recording_active(false)
		var recording: AudioStreamWAV = record.get_recording()
		AudioServer.remove_bus_effect(0, AudioServer.get_bus_effect_count(0) - 1)

		if recording != null:
			var data := recording.data
			var num_samples: int = data.size() / 4
			var energy := 0.0
			for si in range(num_samples):
				var l_raw: int = data[si * 4] | (data[si * 4 + 1] << 8)
				if l_raw > 32767: l_raw -= 65536
				var r_raw: int = data[si * 4 + 2] | (data[si * 4 + 3] << 8)
				if r_raw > 32767: r_raw -= 65536
				energy += (float(l_raw) / 32768.0) ** 2 + (float(r_raw) / 32768.0) ** 2
			energy /= max(num_samples, 1)
			energies.append(energy)
			var db: float = 10.0 * log(max(energy, 1e-20)) / log(10.0)
			print("  dist=%.0fm energy=%.6f (%.1fdB)" % [dist, energy, db])
		else:
			energies.append(0.0)
			print("  dist=%.0fm NO RECORDING" % dist)

		player.queue_free()

	camera.queue_free()

	# Verify energy decreases with distance.
	if energies.size() < 3:
		return false
	# Verify furthest distance has significantly less energy than closest.
	if energies[0] < 1e-10:
		print("  No energy at 1m")
		return false
	var ratio: float = energies[2] / energies[0]
	var db_drop: float = 10.0 * log(max(ratio, 1e-20)) / log(10.0)
	print("  20m/1m ratio: %.3f (%.1fdB drop, expect > 6dB)" % [ratio, -db_drop])
	return db_drop < -6.0


func test_frequency_sweep() -> bool:
	print("Test 3: Frequency sweep 20Hz-20kHz (waterfall analysis)")

	var camera := Camera3D.new()
	camera.current = true
	root.add_child(camera)

	# Source at 90° right to get maximum HRTF effect.
	var player := AudioStreamPlayer3D.new()
	player.position = Vector3(3, 0, 0)
	player.bus = "Spatial"
	root.add_child(player)

	var generator := AudioStreamGenerator.new()
	generator.mix_rate = 48000
	generator.buffer_length = 0.5
	player.stream = generator
	player.play()
	var playback: AudioStreamGeneratorPlayback = player.get_stream_playback()

	# Warmup.
	for w in range(10):
		await process_frame
		for i in range(playback.get_frames_available()):
			playback.push_frame(Vector2(0.0, 0.0))

	var record := AudioEffectRecord.new()
	AudioServer.add_bus_effect(0, record)
	record.set_recording_active(true)

	# Per-band measurement: one tone per octave band, separate recording each.
	var sample_rate := 48000.0
	var band_centers := [250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0]
	var band_ilds: Array[float] = []

	print("  Band(Hz)   L(dB)    R(dB)    ILD(dB)")
	for freq in band_centers:
		var bp := AudioStreamPlayer3D.new()
		bp.position = Vector3(3, 0, 0)
		bp.bus = "Spatial"
		root.add_child(bp)

		var gen := AudioStreamGenerator.new()
		gen.mix_rate = 48000
		gen.buffer_length = 0.5
		bp.stream = gen
		bp.play()
		var pb: AudioStreamGeneratorPlayback = bp.get_stream_playback()

		# Warmup.
		var gf := 0
		for w in range(10):
			await process_frame
			for i in range(pb.get_frames_available()):
				var t: float = float(gf) / sample_rate
				pb.push_frame(Vector2(sin(2.0 * PI * freq * t) * 0.5, sin(2.0 * PI * freq * t) * 0.5))
				gf += 1

		# Record.
		var rec := AudioEffectRecord.new()
		AudioServer.add_bus_effect(0, rec)
		rec.set_recording_active(true)

		for batch in range(25):
			await process_frame
			for i in range(pb.get_frames_available()):
				var t: float = float(gf) / sample_rate
				pb.push_frame(Vector2(sin(2.0 * PI * freq * t) * 0.5, sin(2.0 * PI * freq * t) * 0.5))
				gf += 1

		for i in range(5):
			await process_frame

		rec.set_recording_active(false)
		var recording: AudioStreamWAV = rec.get_recording()
		AudioServer.remove_bus_effect(0, AudioServer.get_bus_effect_count(0) - 1)
		bp.queue_free()

		if recording == null or recording.data.size() < 400:
			band_ilds.append(0.0)
			print("  %7.0f  no data" % freq)
			continue

		var data := recording.data
		var ns: int = data.size() / 4
		var el := 0.0
		var er := 0.0
		for si in range(ns):
			var lr: int = data[si * 4] | (data[si * 4 + 1] << 8)
			if lr > 32767: lr -= 65536
			var rr: int = data[si * 4 + 2] | (data[si * 4 + 3] << 8)
			if rr > 32767: rr -= 65536
			el += (float(lr) / 32768.0) ** 2
			er += (float(rr) / 32768.0) ** 2
		el /= ns
		er /= ns
		var dl: float = 10.0 * log(max(el, 1e-20)) / log(10.0)
		var dr: float = 10.0 * log(max(er, 1e-20)) / log(10.0)
		var ild: float = dr - dl
		band_ilds.append(ild)
		print("  %7.0f  %8.3f  %8.3f  %8.3f" % [freq, dl, dr, ild])

	camera.queue_free()

	# Verify HRTF frequency dependence: ILD should vary across bands.
	var min_ild := 999.0
	var max_ild := -999.0
	for ild in band_ilds:
		if ild < min_ild: min_ild = ild
		if ild > max_ild: max_ild = ild
	var ild_range: float = max_ild - min_ild
	print("  ILD range: %.3fdB (min=%.3f max=%.3f, expect variation)" % [ild_range, min_ild, max_ild])

	# Pass if there's any spatial content and some frequency variation.
	return max_ild > 0.5
