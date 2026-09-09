extends Node3D
# Cassie raw_data → CassieSketcher replay runner.
#
# Reads a session JSON in the cassie-data raw_data shape
# (E:/cassie-data/data/raw_data/<name>.json — allSketchedStrokes with
# poly-Bezier ctrlPts), flattens each stroke's cubic-Bezier ctrl points
# into a polyline at spp=8, and streams them through CassieSketcher one
# stroke at a time over several frames so the user sees the canvas
# fill in as if someone were drawing.
#
# CLI overrides:
#   --replay-json=<path>       defaults to E:/cassie-data/data/raw_data/01-1-1.json
#   --replay-strokes-per-frame=<n>   defaults to 1
#   --replay-auto-quit-sec=<n>       defaults to 30
#
# Prints:
#   [boot] smoke status + ClassDB checks
#   [replay] per-stroke begin/commit + per-patch arrivals
# Auto-quits after replay-auto-quit-sec seconds OR when all strokes
# have been committed plus a 3 s settle window (whichever comes first).

const DEFAULT_JSON := "E:/cassie-data/data/raw_data/01-1-1.json"
const DEFAULT_AUTO_QUIT_SEC := 30.0
const DEFAULT_STROKES_PER_FRAME := 1

@export var sketcher_path: NodePath
@export var patches_mesh_path: NodePath

var _sketcher: CassieSketcher
var _patches_mesh: MeshInstance3D
var _strokes: Array = []
var _stroke_idx: int = 0
var _elapsed: float = 0.0
var _commits_total: int = 0
var _patches_total: int = 0
var _strokes_per_frame: int = DEFAULT_STROKES_PER_FRAME
var _auto_quit_sec: float = DEFAULT_AUTO_QUIT_SEC
var _replay_done_at: float = -1.0


func _ready() -> void:
	_log_boot()
	_resolve_nodes()
	if _sketcher == null:
		printerr("[replay] no CassieSketcher at path '%s' — aborting" % sketcher_path)
		get_tree().quit(1)
		return

	var json_path := DEFAULT_JSON
	for arg in OS.get_cmdline_args():
		if arg.begins_with("--replay-json="):
			json_path = arg.split("=", true, 1)[1]
		elif arg.begins_with("--replay-strokes-per-frame="):
			_strokes_per_frame = max(1, int(arg.split("=", true, 1)[1]))
		elif arg.begins_with("--replay-auto-quit-sec="):
			_auto_quit_sec = float(arg.split("=", true, 1)[1])

	_strokes = _load_strokes(json_path)
	print("[replay] loaded %d strokes from %s" % [_strokes.size(), json_path])
	if _strokes.is_empty():
		printerr("[replay] no strokes — aborting")
		get_tree().quit(1)
		return

	_sketcher.set_async_triangulation(false)
	_sketcher.patch_added.connect(_on_patch_added)
	_sketcher.patch_removed.connect(_on_patch_removed)


func _process(delta: float) -> void:
	_elapsed += delta

	# Stream strokes at _strokes_per_frame per frame.
	for _i in range(_strokes_per_frame):
		if _stroke_idx >= _strokes.size():
			break
		_drive_stroke(_strokes[_stroke_idx])
		_stroke_idx += 1

	if _stroke_idx >= _strokes.size() and _replay_done_at < 0.0:
		_replay_done_at = _elapsed
		print("[replay] all %d strokes committed; patches so far: %d (waiting 3s for settle)"
			% [_commits_total, _patches_total])

	# Quit when all strokes settled, or hard cap.
	if _replay_done_at > 0.0 and (_elapsed - _replay_done_at) >= 3.0:
		print("[replay] done. commits=%d patches=%d" % [_commits_total, _patches_total])
		get_tree().quit()
	elif _elapsed >= _auto_quit_sec:
		print("[replay] hard auto-quit @%.1fs. commits=%d patches=%d strokes_left=%d"
			% [_auto_quit_sec, _commits_total, _patches_total,
			   max(0, _strokes.size() - _stroke_idx)])
		get_tree().quit()


# ── helpers ─────────────────────────────────────────────────────────────

func _log_boot() -> void:
	var xr := XRServer.find_interface("OpenXR")
	if xr != null and xr.is_initialized():
		print("[boot] OpenXR runtime: %s" % str(xr.get_runtime_name()) if xr.has_method("get_runtime_name") else "[boot] OpenXR initialized")
	else:
		print("[boot] OpenXR not initialized (flatscreen mode)")
	for cls in ["CassieSketcher", "XRGridXROrigin", "XRGridHand",
				"XRGridFabricManager", "XRGridZoneSceneTree",
				"XRGridCapsulePersona"]:
		print("[boot] %s exists: %s" % [cls, ClassDB.class_exists(cls)])


func _resolve_nodes() -> void:
	if sketcher_path != NodePath():
		_sketcher = get_node_or_null(sketcher_path) as CassieSketcher
	if patches_mesh_path != NodePath():
		_patches_mesh = get_node_or_null(patches_mesh_path) as MeshInstance3D


func _load_strokes(p_path: String) -> Array:
	if not FileAccess.file_exists(p_path):
		printerr("[replay] file not found: %s" % p_path)
		return []
	var f := FileAccess.open(p_path, FileAccess.READ)
	if f == null:
		printerr("[replay] cannot open: %s" % p_path)
		return []
	var raw := f.get_as_text()
	f.close()
	var parsed = JSON.parse_string(raw)
	if typeof(parsed) != TYPE_DICTIONARY:
		printerr("[replay] not a JSON object: %s" % p_path)
		return []
	return parsed.get("allSketchedStrokes", [])


func _drive_stroke(stroke: Dictionary) -> void:
	var ctrl: Array = stroke.get("ctrlPts", [])
	var polyline := _flatten_ctrl_pts(ctrl, 8)
	if polyline.size() < 2:
		return
	var sid := _sketcher.begin_stroke(polyline[0], 0.5)
	for i in range(1, polyline.size()):
		_sketcher.add_sample(sid, polyline[i], 0.5)
	var result: Dictionary = _sketcher.commit_stroke(sid)
	_commits_total += 1
	if not result.get("ok", false):
		print("[replay] stroke %d rejected: %s"
				% [_commits_total, str(result.get("reason", ""))])


func _flatten_ctrl_pts(ctrl: Array, spp: int) -> PackedVector3Array:
	var n := ctrl.size()
	var out := PackedVector3Array()
	if n < 2:
		return out
	if n == 2:
		out.push_back(_v3(ctrl[0]))
		out.push_back(_v3(ctrl[1]))
		return out
	var segs := int((n - 1) / 3)
	if segs < 1:
		return out
	for s in range(segs):
		var p0 := _v3(ctrl[3 * s])
		var p1 := _v3(ctrl[3 * s + 1])
		var p2 := _v3(ctrl[3 * s + 2])
		var p3 := _v3(ctrl[3 * s + 3])
		var t_count := spp + 1 if s == segs - 1 else spp
		for i in range(t_count):
			var t: float = float(i) / float(spp)
			var mt: float = 1.0 - t
			var mt2 := mt * mt
			var t2 := t * t
			out.push_back(Vector3(
				mt*mt2*p0.x + 3.0*mt2*t*p1.x + 3.0*mt*t2*p2.x + t*t2*p3.x,
				mt*mt2*p0.y + 3.0*mt2*t*p1.y + 3.0*mt*t2*p2.y + t*t2*p3.y,
				mt*mt2*p0.z + 3.0*mt2*t*p1.z + 3.0*mt*t2*p2.z + t*t2*p3.z))
	return out


func _v3(d) -> Vector3:
	if d is Dictionary:
		return Vector3(d.get("x", 0.0), d.get("y", 0.0), d.get("z", 0.0))
	return Vector3.ZERO


func _on_patch_added(patch) -> void:
	_patches_total += 1
	if _patches_mesh == null or patch == null:
		return
	if not patch.has_method("get_mesh"):
		return
	var src_mesh = patch.get_mesh()
	if src_mesh == null:
		return
	var dst: ArrayMesh = _patches_mesh.mesh as ArrayMesh
	if dst == null:
		dst = ArrayMesh.new()
		_patches_mesh.mesh = dst
	if src_mesh is ArrayMesh:
		for surf in range(src_mesh.get_surface_count()):
			dst.add_surface_from_arrays(
				src_mesh.surface_get_primitive_type(surf),
				src_mesh.surface_get_arrays(surf))


func _on_patch_removed(_patch) -> void:
	pass
