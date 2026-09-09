extends Node2D

# Records a scene that is hard for the codec, so the bitrate reported is not optimistic.
#
# WHAT IS DRAWN, AND WHY IT IS NOT A COLORED SQUARE. CineForm is a wavelet codec and a flat
# field costs it almost nothing, so a simple scene reports a bitrate no real recording would
# ever hit. This draws smooth gradients, hard edges, fine high frequency detail and continuous
# motion at once, which is roughly what a real capture contains. An honest file size needs an
# honest picture.

# RETRACTED: these were constants at 1920 by 1080. Rendering at 3840 by 2160 then drew the
# whole scene into the top left quarter and left the rest black, which is not a stretched
# picture, it is a wrong one. The viewport is the authority on its own size.
const BARS := 64

var W := 1920.0
var H := 1080.0

var t := 0.0
var frames := 0


func _ready() -> void:
	var vp := get_viewport_rect().size
	W = vp.x
	H = vp.y
	print("scene follows the viewport at %dx%d" % [int(W), int(H)])

	# The module is compiled into the engine, so a missing class means the build excluded it.
	# config.py does that on non x86 architectures.
	if not ClassDB.class_exists("MovieWriterCineForm"):
		printerr("FAIL MovieWriterCineForm is not registered")
		get_tree().quit(1)
		return
	print("ok   MovieWriterCineForm registered")
	print("     quality        %s" % ProjectSettings.get_setting("editor/movie_writer/cineform/quality"))
	print("     thread_count   %s" % ProjectSettings.get_setting("editor/movie_writer/cineform/thread_count"))
	print("     keep_alpha     %s" % ProjectSettings.get_setting("editor/movie_writer/cineform/keep_alpha"))
	print("     async_readback %s" % ProjectSettings.get_setting("editor/movie_writer/async_readback"))


func _process(delta: float) -> void:
	# Movie Maker drives time from the fixed frame rate, so this advances by exactly one frame
	# regardless of how long encoding took. Using wall clock here would make the animation
	# speed depend on the encoder, and a slower quality setting would produce a shorter video.
	t += delta
	frames += 1
	queue_redraw()


func _draw() -> void:
	# 1. A smooth two axis gradient. Cheap for a wavelet, and the thing that would flatter it
	#    if it were the only content.
	# Only the lower half is painted, so the upper half stays transparent and the alpha
	# channel carries something a decoder can check.
	var steps := 48
	for i in steps:
		if float(i) / float(steps - 1) < 0.5:
			continue  # TRANSPARENT band
		var f := float(i) / float(steps - 1)
		var col := Color.from_ok_hsl(fposmod(0.58 + f * 0.12 + t * 0.02, 1.0), 0.55,
			lerp(0.10, 0.34, f))
		draw_rect(Rect2(0.0, H * f / 1.0 * 0.0 + H * f, W, H / float(steps) + 2.0), col)

	# 2. A spectrum of bars with hard edges, moving continuously. Hard edges are where a
	#    wavelet spends its bits, and where the earlier depth probe measured its worst error.
	var bw := W / float(BARS)
	for i in BARS:
		var p := float(i) / float(BARS)
		var amp: float = 0.5 + 0.5 * sin(t * 1.7 + p * TAU * 2.0) * cos(t * 0.9 + p * TAU)
		var bh: float = 60.0 + amp * (H * 0.55)
		var col := Color.from_ok_hsl(fposmod(p + t * 0.05, 1.0), 0.95, 0.62)
		draw_rect(Rect2(i * bw + 2.0, H - bh, bw - 4.0, bh), col)

	# 3. Fine detail: a one pixel checker over a band. This is the worst case for any codec
	#    and it is what stops the bitrate figure being optimistic.
	var band_y := H * 0.06
	for x in range(0, int(W), 2):
		var on := (x / 2 + int(t * 30.0)) % 2 == 0
		draw_rect(Rect2(float(x), band_y, 2.0, 26.0),
			Color.WHITE if on else Color(0.05, 0.05, 0.07))

	# 4. Rotating rings, so motion is not purely horizontal. A codec that only ever sees
	#    sideways movement is not being asked much.
	var c := Vector2(W * 0.5, H * 0.44)
	for i in 5:
		var r: float = 120.0 + float(i) * 78.0
		var a: float = t * (0.6 + float(i) * 0.17)
		draw_arc(c, r, a, a + TAU * 0.62, 96,
			Color.from_ok_hsl(fposmod(0.02 + float(i) * 0.11, 1.0), 0.9, 0.68), 7.0, true)

	# 5. A moving hard edged block, the simplest thing to spot if frames are duplicated or
	#    dropped. If this stops travelling in the recording, the writer is repeating a frame.
	var bx: float = fposmod(t * 260.0, W + 240.0) - 120.0
	draw_rect(Rect2(bx, H * 0.30, 120.0, 120.0), Color(0.97, 0.97, 0.98))
	draw_rect(Rect2(bx + 18.0, H * 0.30 + 18.0, 84.0, 84.0), Color(0.05, 0.06, 0.09))
