extends SceneTree

# Headless CLI VoIP test — CONCERT-accurate topology.
# Star topology: clients → server (SFU) → selective forward to clients.
# Audio plays through Godot's AudioStreamPlayer3D + mixer bus.
# Output captured from Godot's Master bus via AudioEffectRecord.

var mode := ""
var address := "127.0.0.1"
var port := 7777
var player_name := "Player"
var wav_path := ""
var test_duration := 10.0
const MAX_SOURCES_PER_LISTENER := 16
const FRAME_INTERVAL_MS := 20

var speech: Speech = null
var processor: SpeechProcessor = null
var peer: ENetMultiplayerPeer = null
var connected := false
var running_time := 0.0
var packets_sent := 0
var packets_received := 0
var packets_forwarded := 0
var wav_samples: PackedFloat32Array
var wav_offset := 0
var voice_id := 0
var last_send_time := 0
var peer_positions: Dictionary = {}
var my_position := Vector3.ZERO
var record_effect: AudioEffectRecord = null

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		match args[i]:
			"--host":
				mode = "host"
				if i + 1 < args.size(): port = int(args[i + 1]); i += 1
			"--join":
				mode = "join"
				if i + 1 < args.size(): address = args[i + 1]; i += 1
				if i + 1 < args.size(): port = int(args[i + 1]); i += 1
			"--name":
				if i + 1 < args.size(): player_name = args[i + 1]; i += 1
			"--wav":
				if i + 1 < args.size(): wav_path = args[i + 1]; i += 1
			"--duration":
				if i + 1 < args.size(): test_duration = float(args[i + 1]); i += 1
			"--pos":
				if i + 3 < args.size():
					my_position = Vector3(float(args[i+1]), float(args[i+2]), float(args[i+3])); i += 3
		i += 1
	if mode == "":
		print("Usage: --host <port> | --join <addr> <port> [--wav <path>] [--name <n>] [--duration <s>] [--pos <x> <y> <z>]")
		quit(1); return
	if wav_path == "":
		wav_path = OS.get_environment("USERPROFILE").replace("\\", "/") + "/Downloads/speech_long_input.wav"
		if not FileAccess.file_exists(wav_path):
			wav_path = wav_path.replace("/", "\\")
	print("[%s] Mode=%s Port=%d Pos=%s Duration=%.0fs" % [player_name, mode, port, my_position, test_duration])

var started := false
func _process(_delta: float) -> bool:
	if not started:
		started = true
		run.call_deferred()
	return false

func run() -> void:
	wav_samples = load_wav_mono(wav_path)
	if wav_samples.size() == 0:
		printerr("[%s] ERROR: No WAV at %s" % [player_name, wav_path])
		quit(1); return

	# Add AudioEffectRecord to Master bus to capture mixed output.
	record_effect = AudioEffectRecord.new()
	AudioServer.add_bus_effect(0, record_effect)
	record_effect.set_recording_active(true)
	print("[%s] Recording Master bus output" % player_name)

	# Add Camera3D as the listener for 3D audio spatialization.
	var camera := Camera3D.new()
	camera.position = my_position
	camera.current = true
	root.add_child(camera)

	# Create Speech node (creates SpeechProcessor child on READY).
	speech = Speech.new()
	root.add_child(speech)
	await process_frame
	await process_frame
	for child in speech.get_children():
		if child is SpeechProcessor:
			processor = child; break
	if processor == null:
		printerr("[%s] ERROR: SpeechProcessor not found" % player_name)
		quit(1); return

	# Networking.
	peer = ENetMultiplayerPeer.new()
	if mode == "host":
		if peer.create_server(port, 32) != OK:
			printerr("[%s] ERROR: Cannot bind :%d" % [player_name, port]); quit(1); return
		root.get_tree().get_multiplayer().multiplayer_peer = peer
		peer.host.compress(ENetConnection.COMPRESS_RANGE_CODER)
		connected = true; peer_positions[1] = my_position
		print("[%s] Hosting (SFU, max %d sources/listener)" % [player_name, MAX_SOURCES_PER_LISTENER])
	else:
		if peer.create_client(address, port) != OK:
			printerr("[%s] ERROR: Cannot connect %s:%d" % [player_name, address, port]); quit(1); return
		root.get_tree().get_multiplayer().multiplayer_peer = peer
		peer.host.compress(ENetConnection.COMPRESS_RANGE_CODER)
		print("[%s] Connecting to %s:%d..." % [player_name, address, port])

	root.get_tree().get_multiplayer().peer_connected.connect(_on_peer_connected)
	root.get_tree().get_multiplayer().peer_disconnected.connect(_on_peer_disconnected)
	root.get_tree().get_multiplayer().connected_to_server.connect(_on_connected)
	root.get_tree().get_multiplayer().peer_packet.connect(_on_peer_packet)

	# Main loop — rate-limited sending.
	var start_time := Time.get_ticks_msec()
	var last_status := 0.0
	last_send_time = start_time
	while running_time < test_duration:
		await process_frame
		running_time = (Time.get_ticks_msec() - start_time) / 1000.0
		var now := Time.get_ticks_msec()
		if connected and now - last_send_time >= FRAME_INTERVAL_MS:
			last_send_time = now
			if mode == "host":
				send_audio_to_all_clients()
			else:
				send_audio_to_server()
		if running_time - last_status >= 2.0:
			last_status = running_time
			if mode == "host":
				print("[%s] t=%.0fs sent=%d recv=%d fwd=%d clients=%d" % [
					player_name, running_time, packets_sent, packets_received, packets_forwarded, peer_positions.size()-1])
			else:
				print("[%s] t=%.0fs sent=%d recv=%d" % [player_name, running_time, packets_sent, packets_received])

	# Stop recording and save.
	record_effect.set_recording_active(false)
	var recording: AudioStreamWAV = record_effect.get_recording()
	var downloads := OS.get_environment("USERPROFILE").replace("\\", "/") + "/Downloads"
	var out_path: String = downloads + "/voip_%s_mixed.wav" % player_name.to_lower()
	if recording != null:
		recording.save_to_wav(out_path)
		print("[%s] Saved mixed output: %s (%.1fs)" % [player_name, out_path, recording.get_length()])
	else:
		print("[%s] WARNING: No recording captured" % player_name)

	print("[%s] === RESULTS ===" % player_name)
	print("[%s] Sent: %d (%.1f/s) | Recv: %d (%.1f/s)" % [
		player_name, packets_sent, packets_sent/max(running_time,0.1),
		packets_received, packets_received/max(running_time,0.1)])
	if mode == "host":
		print("[%s] Forwarded: %d" % [player_name, packets_forwarded])
	var rate: float = packets_sent / max(running_time, 0.1)
	if packets_sent > 0 and packets_received > 0:
		print("[%s] PASS (%.1f pkt/s)" % [player_name, rate])
	else:
		print("[%s] FAIL" % player_name)

	speech.queue_free()
	quit(0)


func _on_peer_connected(p_id: int) -> void:
	print("[%s] +peer %d" % [player_name, p_id])
	var pos := Vector3(randf_range(-5, 5), 0, randf_range(-5, 5))
	peer_positions[p_id] = pos
	var asp := AudioStreamPlayer3D.new()
	asp.name = str(p_id)
	asp.position = pos
	print("[%s]   placed at %s (listener at %s, dist=%.1fm)" % [player_name, pos, my_position, pos.distance_to(my_position)])
	root.add_child(asp)
	speech.add_player_audio(p_id, asp)

func _on_peer_disconnected(p_id: int) -> void:
	print("[%s] -peer %d" % [player_name, p_id])
	peer_positions.erase(p_id)

func _on_connected() -> void:
	connected = true
	print("[%s] Connected (id=%d)" % [player_name, root.get_tree().get_multiplayer().get_unique_id()])

func _on_peer_packet(p_id: int, packet: PackedByteArray) -> void:
	if packet.size() <= 5: return
	if mode == "host":
		packets_received += 1
		forward_selectively(p_id, packet)
	else:
		packets_received += 1
		var idx := packet[0] | (packet[1] << 8) | (packet[2] << 16)
		var sz := packet[3] | (packet[4] << 8)
		if sz <= 0 or packet.size() < 5 + sz: return
		var audio_data := packet.slice(5, 5 + sz)
		var sender_id: int = packet.decode_s32(5 + sz) if packet.size() >= 5 + sz + 4 else p_id
		speech.on_received_audio_packet(sender_id, idx, audio_data)


func encode_opus_frame() -> PackedByteArray:
	var fc: int = SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT
	var ps: int = SpeechProcessor.SPEECH_SETTING_PCM_BUFFER_SIZE
	if wav_offset + fc > wav_samples.size(): wav_offset = 0
	var pcm := PackedByteArray(); pcm.resize(ps)
	for i in range(fc):
		pcm.encode_s16(i * 2, clampi(int(wav_samples[wav_offset + i] * 32767.0), -32768, 32767))
	wav_offset += fc
	var result: Dictionary = processor.encode_buffer(pcm, PackedByteArray())
	var cs: int = result.get("buffer_size", -1)
	if cs <= 0: return PackedByteArray()
	var comp: PackedByteArray = result["byte_array"]
	var pkt := PackedByteArray()
	pkt.append(voice_id & 0xFF); pkt.append((voice_id >> 8) & 0xFF); pkt.append((voice_id >> 16) & 0xFF)
	pkt.append(cs & 0xFF); pkt.append((cs >> 8) & 0xFF)
	pkt.append_array(comp.slice(0, cs))
	return pkt

func send_audio_to_server() -> void:
	var pkt := encode_opus_frame()
	if pkt.size() == 0: return
	root.get_tree().get_multiplayer().send_bytes(pkt, 1, MultiplayerPeer.TRANSFER_MODE_UNRELIABLE, 1)
	voice_id += 1; packets_sent += 1

func send_audio_to_all_clients() -> void:
	var pkt := encode_opus_frame()
	if pkt.size() == 0: return
	var tagged := PackedByteArray(); tagged.append_array(pkt)
	tagged.resize(tagged.size() + 4); tagged.encode_s32(pkt.size(), 1)
	for cid in peer_positions:
		if cid == 1: continue
		root.get_tree().get_multiplayer().send_bytes(tagged, cid, MultiplayerPeer.TRANSFER_MODE_UNRELIABLE, 1)
		packets_forwarded += 1
	voice_id += 1; packets_sent += 1

func forward_selectively(sender_id: int, packet: PackedByteArray) -> void:
	var sender_pos: Vector3 = peer_positions.get(sender_id, Vector3.ZERO)
	var tagged := PackedByteArray(); tagged.append_array(packet)
	tagged.resize(tagged.size() + 4); tagged.encode_s32(packet.size(), sender_id)
	for lid in peer_positions:
		if lid == sender_id or lid == 1: continue
		var d: float = sender_pos.distance_to(peer_positions.get(lid, Vector3.ZERO))
		if d < 100.0:
			root.get_tree().get_multiplayer().send_bytes(tagged, lid, MultiplayerPeer.TRANSFER_MODE_UNRELIABLE, 1)
			packets_forwarded += 1
	# Server also receives.
	var idx := packet[0] | (packet[1] << 8) | (packet[2] << 16)
	var sz := packet[3] | (packet[4] << 8)
	if sz > 0 and packet.size() >= 5 + sz:
		speech.on_received_audio_packet(sender_id, idx, packet.slice(5, 5 + sz))

func load_wav_mono(path: String) -> PackedFloat32Array:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null: return PackedFloat32Array()
	var riff := f.get_buffer(4).get_string_from_ascii(); f.get_32()
	var wave := f.get_buffer(4).get_string_from_ascii()
	if riff != "RIFF" or wave != "WAVE": return PackedFloat32Array()
	var sr := 0; var nch := 0; var bps := 0; var samples := PackedFloat32Array()
	while f.get_position() < f.get_length():
		var cid := f.get_buffer(4).get_string_from_ascii()
		var csz := f.get_32(); var cs := f.get_position()
		if cid == "fmt ":
			f.get_16(); nch = f.get_16(); sr = f.get_32(); f.get_32(); f.get_16(); bps = f.get_16()
		elif cid == "data":
			var n: int = csz / (bps / 8) / nch; samples.resize(n)
			for si in range(n):
				var raw: int = f.get_16()
				if raw > 32767: raw -= 65536
				samples[si] = float(raw) / 32768.0
				if nch == 2: f.get_16()
		f.seek(cs + csz)
	print("[%s] WAV: %d samples @ %dHz (%.1fs)" % [player_name, samples.size(), sr, samples.size()/float(max(sr,1))])
	return samples
