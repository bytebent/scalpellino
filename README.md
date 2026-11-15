# Scalpellino

A small, fast, Hammer-style brush map editor for macOS, for Half-Life / CS-style levels
with an optional Y2K render look, exporting to Godot and other engines.

Personal project. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design and
[`docs/PLAN.md`](docs/PLAN.md) for current progress.

## Getting a build

Every push is built on GitHub for Intel and Apple Silicon Macs.

1. Open the repo's **Actions** tab, pick the latest green **build** run.
2. Under **Artifacts**, download `Scalpellino-intel` (or `Scalpellino-apple-silicon`).
3. Unzip it to get `Scalpellino.app`.
4. The app isn't signed with an Apple developer certificate, so macOS will block it the
   first time. Either:
   - try to open it, then go to **System Settings → Privacy & Security** and click
     **Open Anyway**, or
   - in Terminal: `xattr -dr com.apple.quarantine /path/to/Scalpellino.app`

## Controls (so far)

| Action | Keys |
|---|---|
| Fly mode in 3D view (toggle) | **Z** while hovering the 3D view; **Esc** also exits |
| Move while flying | **W A S D**, **Q/E** down/up, hold **Shift** for fast |
| Look around without fly mode | hold **right mouse** in the 3D view (+ WASD) |
| Pan a 2D view | hold **middle mouse**, or **Space + left mouse** |
| Zoom a 2D view | mouse wheel (zooms toward the cursor) |
| Smaller / larger grid | **è** / **+** (the two keys right of P) |
| Show grid / snap to grid | **Shift+R** / **Shift+W** |

Keys are read by physical position, so they work the same on any keyboard layout.

## Building locally

Needs CMake 3.24+ and Apple's command line tools (`xcode-select --install`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/Scalpellino.app
```

Dependencies (raylib, Dear ImGui, rlImGui) are downloaded automatically at pinned versions.
