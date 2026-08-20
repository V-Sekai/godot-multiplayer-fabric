# CineForm module demo

Records a deliberately hard scene: smooth gradients, hard edged bars, a one pixel checker,
rotating arcs and a travelling block. A flat colour compresses to almost nothing and reports
a bitrate no real recording would reach.

    godot --path modules/cineform/demo --rendering-driver opengl3 \
        --write-movie out.cfhd --fixed-fps 30 --quit-after 900

The writer claims the `.cfhd` extension. The container is AVI, so the output remuxes to
Matroska without re-encoding:

    ffmpeg -i out.cfhd -c copy out.mkv

Running the scene without `--write-movie` checks only that the module is present and prints
its settings. The class is absent on non x86 builds, which `config.py` excludes.
