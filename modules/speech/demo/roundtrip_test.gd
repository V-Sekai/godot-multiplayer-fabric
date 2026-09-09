extends SceneTree

var downloads_dir: String

func _init() -> void:
	downloads_dir = OS.get_environment("USERPROFILE").replace("\\", "/") + "/Downloads"

var started := false
func _process(_delta: float) -> bool:
	if not started:
		started = true
		run_tests.call_deferred()
	return false

func run_tests() -> void:
	var wav_names: Array[String] = [
		"speech_long_input.wav",
		"music_jolson_input.wav",
		"music_tallis_input.wav",
		"music_electronic_input.wav",
	]

	var speech := Speech.new()
	root.add_child(speech)
	await process_frame
	await process_frame

	var processor: SpeechProcessor = null
	for child in speech.get_children():
		if child is SpeechProcessor:
			processor = child
			break
	if processor == null:
		printerr("SpeechProcessor not found")
		quit(1)
		return

	var decoder = speech.get_speech_decoder()
	var tested := 0
	var failed := 0

	for wav_name in wav_names:
		var path: String = downloads_dir + "/" + wav_name
		if not FileAccess.file_exists(path):
			print("SKIP: %s" % wav_name)
			continue
		print("Testing: %s" % wav_name)
		var ok: bool = await roundtrip(path, wav_name, processor, decoder, speech)
		tested += 1
		if not ok:
			failed += 1

	speech.queue_free()
	print("")
	if tested == 0:
		print("No WAVs found.")
		quit(1)
	elif failed > 0:
		print("FAIL: %d/%d" % [failed, tested])
		quit(1)
	else:
		print("PASS: %d/%d — listen to output WAVs in Downloads" % [tested, tested])
		quit(0)


func roundtrip(path: String, wav_name: String, processor: SpeechProcessor, decoder: SpeechDecoder, speech: Speech) -> bool:
	var wav := load_wav_mono(path)
	if wav.is_empty():
		printerr("  Bad WAV")
		return false

	var samples: PackedFloat32Array = wav["samples"]
	var sr: int = wav["sample_rate"]
	print("  %d samples @ %dHz (%.2fs)" % [samples.size(), sr, samples.size() / float(sr)])

	var frame_count: int = SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT
	var pcm_size: int = SpeechProcessor.SPEECH_SETTING_PCM_BUFFER_SIZE
	var num_packets: int = samples.size() / frame_count
	var output := PackedFloat32Array()
	var corrupted := 0

	for pkt_i in range(num_packets):
		var pcm := PackedByteArray()
		pcm.resize(pcm_size)
		for i in range(frame_count):
			var val: int = clampi(int(samples[pkt_i * frame_count + i] * 32767.0), -32768, 32767)
			pcm.encode_s16(i * 2, val)

		var out_buf := PackedByteArray()
		out_buf.resize(pcm_size)
		var result: Dictionary = processor.encode_buffer(pcm, out_buf)
		var comp_size: int = result.get("buffer_size", -1)
		if comp_size <= 0:
			corrupted += 1
			continue

		var comp: PackedByteArray = result["byte_array"]
		var uncompressed := PackedVector2Array()
		uncompressed.resize(frame_count)
		var decoded: PackedVector2Array = speech.decompress_buffer(decoder, comp, comp_size, uncompressed)
		if decoded.size() != frame_count:
			corrupted += 1
			continue

		for i in range(decoded.size()):
			var l: float = decoded[i].x
			if is_nan(l) or is_inf(l):
				printerr("  NaN/Inf at pkt %d sample %d" % [pkt_i, i])
				corrupted += 1
				break
			output.append(l)

	if corrupted > 0:
		printerr("  %d/%d corrupted" % [corrupted, num_packets])
		return false

	var out_name: String = wav_name.replace("_input.wav", "_output.wav")
	write_wav(downloads_dir + "/" + out_name, output, 48000)
	print("  %d packets → %d output samples (%.2fs) → %s" % [num_packets, output.size(), output.size() / 48000.0, out_name])

	var energy := 0.0
	for s in output:
		energy += s * s
	var avg: float = energy / max(output.size(), 1)
	print("  Energy: %.6f" % avg)
	return true


func load_wav_mono(path: String) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {}
	var riff := f.get_buffer(4).get_string_from_ascii()
	f.get_32()
	var wave := f.get_buffer(4).get_string_from_ascii()
	if riff != "RIFF" or wave != "WAVE":
		return {}

	var sr := 0
	var nch := 0
	var bps := 0
	var samples := PackedFloat32Array()

	while f.get_position() < f.get_length():
		var cid := f.get_buffer(4).get_string_from_ascii()
		var csz := f.get_32()
		var cstart := f.get_position()
		if cid == "fmt ":
			f.get_16()
			nch = f.get_16()
			sr = f.get_32()
			f.get_32()
			f.get_16()
			bps = f.get_16()
		elif cid == "data":
			var n: int = csz / (bps / 8) / nch
			samples.resize(n)
			for i in range(n):
				var raw: int = f.get_16()
				if raw > 32767:
					raw -= 65536
				samples[i] = float(raw) / 32768.0
				if nch == 2:
					f.get_16()
		f.seek(cstart + csz)
	return {"samples": samples, "sample_rate": sr}


func write_wav(path: String, samples: PackedFloat32Array, sr: int) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return
	var dsz := samples.size() * 2
	f.store_buffer("RIFF".to_utf8_buffer())
	f.store_32(36 + dsz)
	f.store_buffer("WAVE".to_utf8_buffer())
	f.store_buffer("fmt ".to_utf8_buffer())
	f.store_32(16)
	f.store_16(1)
	f.store_16(1)
	f.store_32(sr)
	f.store_32(sr * 2)
	f.store_16(2)
	f.store_16(16)
	f.store_buffer("data".to_utf8_buffer())
	f.store_32(dsz)
	for i in range(samples.size()):
		f.store_16(clampi(int(samples[i] * 32767.0), -32768, 32767))
