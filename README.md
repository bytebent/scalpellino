# Scalpellino

A small, fast, Hammer-style brush map editor for macOS, for Half-Life / CS-style levels
with an optional Y2K render look, exporting to Godot and other engines.

Personal project. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design and
[`docs/PLAN.md`](docs/PLAN.md) for current progress.

## Getting a build

Open the repo's **Releases** page (right-hand side on GitHub):

- **Latest build** — the newest working build, updated automatically after every
  successful push.
- **Versions** (v0.1.0, ...) — milestone snapshots that don't change.

Download `Scalpellino-intel.zip` (or `Scalpellino-apple-silicon.zip`), unzip it, and on
first launch allow it in **System Settings → Privacy & Security → Open Anyway** (the app
isn't signed with an Apple developer certificate). Or in Terminal:
`xattr -dr com.apple.quarantine /path/to/Scalpellino.app`

## Controls (so far)

Everything is also in the menus (File, Edit, Tools, Transform, View, Help), and
**Help → Controls** (F1) lists every shortcut.

| Action | Keys |
|---|---|
| Select tool / Block tool | **Shift+S** / **Shift+B** |
| Draw a block | Block tool: drag in a 2D view, adjust handles in any 2D view, **Return** creates, **Esc** cancels |
| Select | click a brush (2D or 3D); **Cmd+click** adds/removes; **Cmd+A** all; **Esc** none |
| Move / resize | drag a selection / drag its handles or edges (snaps to the grid) |
| Clone | **Shift+drag** a selection, or **Cmd+D** (Duplicate) |
| Rotate by dragging | **Option+drag** a corner handle (15° steps, add **Shift** for free rotation) |
| Exact move / rotate / scale | **Cmd+T** — type values, pick axis and pivot |
| Rotate 90° / flip | Transform menu |
| Delete | **Delete** or **Backspace** |
| Undo / redo | **Cmd+Z** / **Cmd+Shift+Z** |
| New / open / save / save as | **Cmd+N** / **Cmd+O** / **Cmd+S** / **Cmd+Shift+S** |
| Center 2D views on selection | **Cmd+E** |
| Fly mode in 3D view (toggle) | **Z** while hovering the 3D view; **Esc** also exits |
| Move while flying | **W A S D**, **Q/E** down/up, hold **Shift** for fast |
| Look around without fly mode | hold **right mouse** in the 3D view (+ WASD) |
| Pan a 2D view | hold **middle mouse**, or **Space + left mouse** |
| Zoom a 2D view | mouse wheel (zooms toward the cursor) |
| Smaller / larger grid | **è** / **+** (the two keys right of P) |
| Show grid / snap to grid | **Shift+R** / **Shift+W** |

Keys are read by physical position, so they work the same on any keyboard layout.
Maps are saved as Valve 220 `.map` files (readable by TrenchBroom and func_godot).
File open/save currently use a path field; native Mac file dialogs come later.

## Building locally

Needs CMake 3.24+ and Apple's command line tools (`xcode-select --install`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/Scalpellino.app
```

Dependencies (raylib, Dear ImGui, rlImGui) are downloaded automatically at pinned versions.
