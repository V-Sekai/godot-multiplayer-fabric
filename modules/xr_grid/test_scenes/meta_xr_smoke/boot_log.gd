extends Node3D
# Boot-time smoke log. Prints whether OpenXR initialized, which runtime
# resolved, and confirms the native XRGrid classes loaded. Auto-quits
# after 5 seconds so a CI / scripted run terminates cleanly.

const SMOKE_DURATION_SEC := 5.0

var _elapsed: float = 0.0


func _ready() -> void:
	var xr := XRServer.find_interface("OpenXR")
	if xr == null:
		print("[smoke] XRServer.find_interface(OpenXR) returned null")
	elif not xr.is_initialized():
		print("[smoke] OpenXR interface found but is_initialized() = false")
	else:
		print("[smoke] OpenXR initialized")
		var vp := get_viewport()
		if vp:
			vp.use_xr = true
		print("[smoke]   tracking status = ", xr.get_tracking_status())

	# Confirm the native XRGrid classes resolved.
	print("[smoke] XRGridCapsulePersona exists: ", ClassDB.class_exists("XRGridCapsulePersona"))
	print("[smoke] XRGridXROrigin exists: ", ClassDB.class_exists("XRGridXROrigin"))
	print("[smoke] XRGridHand exists: ", ClassDB.class_exists("XRGridHand"))
	print("[smoke] XRGridFabricManager exists: ", ClassDB.class_exists("XRGridFabricManager"))
	print("[smoke] XRGridZoneSceneTree exists: ", ClassDB.class_exists("XRGridZoneSceneTree"))
	print("[smoke] CassieSketcher exists: ", ClassDB.class_exists("CassieSketcher"))

	print("[smoke] auto-quit in %.1fs" % SMOKE_DURATION_SEC)


func _process(delta: float) -> void:
	_elapsed += delta
	if _elapsed >= SMOKE_DURATION_SEC:
		print("[smoke] done")
		get_tree().quit()
