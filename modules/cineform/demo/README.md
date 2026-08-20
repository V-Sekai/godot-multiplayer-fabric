# CineForm module demo

Records a deliberately hard scene: smooth gradients, hard edged bars, a one pixel checker,
rotating arcs and a travelling block. A flat colour compresses to almost nothing and reports
a bitrate no real recording would reach.

    godot --path modules/cineform/demo --write-movie out.mkv --fixed-fps 60 --quit-after 240

The writer claims `.mkv` and `.cfhd`, and writes Matroska either way. The video track is
CineForm and the audio track is PCM, so the file opens in anything that reads Matroska.

Match the movie size to a window the screen can actually hold. Movie Maker takes the movie
size from `display/window/size/viewport_*`, while the window is whatever the OS grants, so
asking for 3840x2160 on a display that yields 3840x2130 makes every frame a CPU crop and
resize of 8.3 million pixels. Measured over 180 frames, that cost 222 ms for each frame
against 39 ms once the two matched, and the encoder itself was 8 ms of it.

Recording needs a driver with a RenderingDevice. The OpenGL driver has none, and the writer
falls back to a blocking viewport readback there.

Running the scene without `--write-movie` checks only that the module is present and prints
its settings. The class is absent on non x86 builds, which `config.py` excludes.

Settings live under `editor/movie_writer/cineform/`: `quality`, `thread_count` and
`keep_alpha`. Alpha encodes RGBA 4:4:4:4 and costs about a third more time and a fifth more
space, so it is off unless the viewport is actually transparent.
