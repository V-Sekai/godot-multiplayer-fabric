# RFD 1175: game

**State:** discussion
**Flight level:** L3

## What the player sees

A native window on the operator's desktop. Inside, a VRM avatar
framed like a video call — eye-level camera, ~1.52 m off the
floor, portrait 4:5 aspect. LookAt tracks the camera every frame
with a 300 ms glance-away every 4–7 s to break the death-stare.
Ambient breathing sway at 0.02 rad on the spine, 4 s sinusoid.
Blink cycle 4 s / 100 ms closed. The player IS the caller.

## Loop

At each decision point, the Taskweft planner supplies the legal
moves (Starforged rules baked into `starforged.sqlite`, ~433 rows).
Player picks one from a Godot Control-node VN layout. The chosen
move's outcome (`strong-hit` / `weak-hit` / `miss`) fires a VRM
expression blendshape (`happy` / `neutral` / `sad`) that holds
~1.5 s then decays over ~100 ms — total envelope ~1.6 s.
Motion-bricks (ggml, 183 M params, NVIDIA Open Model License,
Q4 GGUF) generates body motion between decisions.

## Runtime stack

- **Godot** as the runtime, Vulkan renderer (MoltenVK on macOS).
- **VRM 1.0** loads via `godot-vrm` compiled to RISC-V ELF and
  run in the `modules/sandbox` (libriscv) sandbox — no forked C++,
  no unsandboxed GDScript addon.
- **ggml** with Vulkan backend for every inference call
  (motion-bricks, Kimodo text-to-motion, EditScore judgment).
- **Model bundle**: ZSTD-compressed SQLite on local disk, loaded
  via `sqlite3_open()` on install path.
- **CineForm** encoder (ffmpeg blocklisted).
- **Nord palette** for demo chrome.

## Video head

Same binary invoked with `--headless --write-movie shot<NN>.<ext>`
walking a shot list. Same `.tscn` / `.tres` assets. The video is
the game recorded — not a second deliverable.
