# Scalpellino

A small, fast, Hammer-style brush map editor for macOS, for Half-Life / CS-style levels
where you build a small scene, place a camera and render a picture in a Source / Y2K look.

Personal project. The docs, in the order to read them:

- [`CLAUDE.md`](CLAUDE.md): the hard rules for AI sessions (privacy, product, engineering).
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): the agreed design: data model, tools, file formats, rendering, roadmap.
- [`docs/PLAN.md`](docs/PLAN.md): where the project stands: status, roadmap, parked questions.
- [`docs/DEV.md`](docs/DEV.md): how to build, test, run CI and release.
- [`docs/HANDOFF.md`](docs/HANDOFF.md): how the user likes to work and decisions to keep (for a new AI session).
- [`docs/LOG.md`](docs/LOG.md) and [`docs/LOG-archive.md`](docs/LOG-archive.md): what was done, step by step.
- [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md): licenses of dependencies, font and textures.

## Getting a build

Open the repo's **Releases** page (right-hand side on GitHub):

- **Latest build** — the newest working build, updated automatically after every
  successful push.
- **Versions** (v0.1.0, ...) — milestone snapshots that don't change.

Download `Scalpellino-intel.zip` (Apple Silicon builds are made on demand: Actions tab →
**apple-silicon** → Run workflow), unzip it, and on first launch allow it in **System Settings → Privacy & Security → Open Anyway** (the app
isn't signed with an Apple developer certificate). Or in Terminal:
`xattr -dr com.apple.quarantine /path/to/Scalpellino.app`

## Controls (so far)

Everything is also in the menus (File, Edit, Tools, Transform, Texture, Terrain, View, Help), and
**Help → Controls** (F1) lists every shortcut.

| Action | Keys |
|---|---|
| Select / Block / Vertex / Texture / Clip / Curve / Terrain tool | **Shift+S** / **Shift+B** / **Shift+V** / **Shift+A** / **Shift+X** / **Shift+C** / **Shift+G** |
| Draw a block | Block tool: drag in a 2D view, adjust handles in any 2D view, **Return** creates, **Esc** cancels; shape (box, wedge, cylinder, cone, pyramid, sphere) and sides in the **Properties** panel |
| Select | click a brush (2D or 3D); **Cmd+click** adds/removes; **Cmd+A** all; **Esc** none. In the 3D view, **drag a rectangle** to select everything wholly inside it (brushes, primitives, props, lights); **Cmd+drag** adds |
| Move / resize | drag a selection / drag its handles or edges (snaps to the grid) |
| Clone | **Shift+drag** a selection, or **Cmd+D** (Duplicate) |
| Rotate by dragging | **Option+drag** a corner handle (15° steps, add **Shift** for free rotation) |
| Exact move / rotate / scale | **Cmd+T** — type values, pick axis and pivot |
| Rotate 90° / flip | Transform menu |
| Vertex tool | **Shift+V**: click a corner (stacked corners move together), **Cmd+click** adds, drag moves (snapped); impossible shapes turn magenta and aren't applied |
| Move vertices exactly | **Cmd+Shift+T** (vertex tool): move by an offset, or set a corner's position |
| Snap vertices to grid | **Cmd+B** (Transform menu) |
| Pivot for rotate/scale | Transform → Pivot (selection centre, world origin, typed point) |
| Grid-friendly rotation | Transform → Drag rotation snapping → 3:4:5-style angles (corners stay on whole numbers) |
| Grid size / microgrid | View → Set grid size... (any size, e.g. 80) / Microgrid (0.125–0.5) |
| Choose a texture | **Textures** panel (right): search by name or folder, click a thumbnail. **Cmd+click** adds / removes one, **Shift+click** a range, **Select all shown** after a search (also Texture → Select all shown textures, and Texture → Select the textures of the picked faces). A small mark on a thumbnail: **V** a variant, **\*** it has a `.vmt`, **H** a helper map |
| Materials | Every texture is a material. **Texture tool → Properties → Material** edits the selected ones (picking faces selects their materials): the shader (LightmappedGeneric, UnlitGeneric, VertexLitGeneric, or your own GLSL files in the Bottega's `shaders/`), glow (`$selfillum`), chrome (`$envmap env_cubemap`: the 4 nearest reflection probes, blended; without probes, or with another `$envmap` value such as `sky`, the sky: cheap chrome; with mask, tint, contrast, saturation, Fresnel), cutouts (`$alphatest`, cut-out shadows too), see-through (`$translucent`, `$alpha`: no shadow), detail textures, `$reflectivity`, texlights (`%texlight`: the surface lights the map). Written as Source `.vmt` files beside the textures (editable as text); several selected = all changed at once, mixed values shown; **Cmd+Z** undoes. The editor shows the base texture; renders and the Camera Preview show the materials |
| Bump maps, Phong, rim light | A bump map (`$bumpmap`, or a picture named `<texture>_normal` / `_bump`) makes the bake fill 3 more lightmaps for the face, as Source does (radiosity normal mapping); `_ssbump` pictures are read as Source's self-shadowed bump maps. Source's green convention; tick **%flipgreen** for maps made the other way. Props use their glTF model's own normal map by themselves; a `.vmt` named like the model's material in its folder adds `$phong`, `$rimlight` and the rest of VertexLitGeneric's settings |
| Variants and parents | **Make a variant...** (Material panel or Texture menu): a copy of one material under a new name, the same picture (e.g. a wet version), its own entry in the Textures panel. **Parent**: materials can share a parent `.vmt` (Source's `patch`); **New parent from these...** makes one from the selected materials, **Edit the parent** changes all of them. Helper maps named `<texture>_normal`, `_ssbump`, `_mask`, `_selfillum` hook onto their texture by themselves and are hidden (**helper maps** in the Textures panel shows them) |
| Apply current texture | **Shift+T** — to selected brushes, or to faces picked with the Texture tool |
| Pick faces | Texture tool, 3D view: click; **Shift+click** adds/removes a face; **Cmd+click** whole brush; **Option+click** picks up that face's texture |
| Clip | Clip tool: drag a line in a 2D view; drag points 1–3 in any 2D view (3 tilts the plane); **Shift+X** again cycles keep both/front/back; **Return** clips; Tools → Clip points... types them |
| Carve | **Cmd+Shift+C**: cut the selection's shape out of the brushes it touches |
| Hollow | **Cmd+Shift+H**: turn brushes into walls of a typed thickness |
| Curves | Curve tool: pick a shape in the **Properties** panel (arch, ring/pipe/cylinder, curved wall, spiral stairs with optional column, arched opening, dome, helix ramp, straight stairs), drag a box in a 2D view (curved wall: start → end, then drag the bend point), adjust with handles or typed values, **Return** creates |
| Edit / bake a curve | select it (one click selects the whole shape), Tools → Edit curve / Bake curve into plain brushes (a curve that was only moved can be edited; one turned, mirrored or reshaped since it was made can't: undo that, or bake it) |
| Make terrain | Terrain → Make terrain: click any faces (3 or 4 corners) in the 3D view (**Shift+click** adds or removes), or click brushes in a 2D view to pick their face toward you; pick the grid size (2–32 cells per side) in **Properties**, **Return** makes them terrain, **Esc** stops. Terrain → Terrain grid size… changes the selected terrain's grid |
| Sculpt / paint terrain | Terrain tool (**Shift+G**): drag over terrain in the 3D view or a 2D view facing it. Modes raise, lower, smooth, flatten, noise, paint (current texture, up to 8 textures per patch; painting the face's own texture uncovers it), erase paint; radius, amount/strength and falloff typed in **Properties**. One drag = one undo step, **Esc** cancels a drag. Neighbouring patches stay stitched |
| Terrain points | Vertex tool (**Shift+V**) on selected terrain: click a point (small blue handle; **Cmd+click** adds), drag it up or down in the 3D view or a side view; **Cmd+Shift+T** types a height. Moving a terrain brush's corners keeps the terrain while its face stays flat |
| Math primitives | Primitive tool (**Shift+M**, Primitives menu): choose sphere, cube, cylinder, cone, torus, capsule or disc in **Properties**, drag a box in a 2D view (or type it), **Return** creates, **Esc** cancels. Drawn exactly (perfectly round at any zoom), next to brushes |
| Edit primitives | Click to select (Select, Light or Primitive tool), drag in a 2D view to move, drag the rings to turn (15° steps; hold **Shift** to turn freely), drag a size handle (scales evenly; Shift: one axis), or type in **Properties**. **Delete**, **Cmd+D** |
| Decals | **Shift+D** (Decals menu): pick a picture in the **Textures** panel, click a surface in the 3D view (it shows there at its picture's size: texels × `$decalscale`, as Source), drag to slide it, **Return** places it, **Esc** cancels. A decal belongs to the brush it is on: it moves, turns and copies with it. It lands on every surface in its box that faces it (brushes and terrain). Click one where its picture shows to select it (Option+click with the Decal tool puts a new one over it); in 3D drag its corners to size it (**Shift**: keep proportions), its ring to turn it, the decal itself to slide it. **Properties** or **Decals → Decal...**: position, width, height, turn, reach, order; **Bring forward / Send back**; **Delete**; **View → Show decals**. Renders light them with the surface's baked light; **DecalModulate** materials darken or lighten what is under them (dirt, cracks) |
| Props (models) | **Shift+P** (Props menu): put `.glb` models in the Bottega's `models` folder (File → Bottega folder..., then Rescan), pick one in the **Props** panel (search by name), click a surface in 3D or a spot in a 2D view, **Return** places it, **Esc** cancels. Select, move, turn (rings: 15° steps; hold **Shift** to turn freely), scale (even; Shift: one axis), **Delete**, **Cmd+D**, Snap to land and Multi-Replicate work on props too; type exact values in **Properties** |
| Blend primitives | **Properties**, in a combination: blend (units) melts a piece smoothly into the others (0 = sharp) |
| Snap to land / ground | **Cmd+J** (Transform menu): selected primitives and props drop onto the brushes, terrain, primitives or props below them (not onto a shelf above or a neighbour beside them); several selected ones land on each other only if they were stacked to begin with; Snap to ground: onto a typed height |
| Multi-Replicate | **Cmd+Shift+D** (Edit menu): copies, each one step further: offset, turn and scale per copy (brushes only move; props turn and scale like primitives); the preview stays while you look around the views; **Return** creates, **Esc** cancels |
| Random Replicate | **Cmd+Shift+R** (Edit menu): copies scattered around the selection, each with a random offset, turn and size inside typed ranges (brushes only move, by whole grid steps while snapping is on); **New seed** shakes them up, the same seed gives the same copies; **Return** creates, **Esc** cancels. The copies stay selected, so **Cmd+J** drops them onto the land |
| Combine primitives | Primitives menu or **Properties**: Combine selected; each piece adds, subtracts or intersects (Bryce-style); Separate |
| One piece of a combination | **Option+click** it (or pick it in **Properties**): a click selects the whole combination, this selects just the piece to drag, turn or size on its own |
| Lights | Light tool (**Shift+L**, World menu): choose point, spot or sun and its colour, brightness, reach and direction in **Properties**, click in a view to place it, **Return** creates, **Esc** cancels. Click a light's marker to select it (Select or Light tool), drag it in a 2D view to move it, **Delete** removes, **Cmd+D** duplicates |
| Aim a light | Drag the rotation rings of a selected spot or sun (3D view: X red, Y green, Z blue; a 2D view: the ring around the axis you look along), 15° steps (hold **Shift** to turn freely), or type pitch and yaw in **Properties** |
| Cameras | Camera tool (**Shift+K**, World menu): set the lens (degrees or mm, presets 14–135 mm), near / far, picture size (presets or typed), orthographic in **Properties**, click in a view to place it, **Return** creates, **Esc** cancels. The first camera is the **active** one (the one Render and the Camera Preview will use; white dot in its marker); tick **active** in Properties or World → Make selected camera active to switch |
| Turn / aim a camera | Click its marker (a diamond) in any tool that selects, drag it in a 2D view to move it; drag the rings to turn it (15° steps; hold **Shift** to turn freely), the yellow ring rolls it; or type pitch / yaw / roll. **Aim at a point**: type it in Properties, or World → Aim selected cameras at a clicked point, then click a surface in 3D or a spot in a 2D view. **Delete**, **Cmd+D** |
| Reflection probes | Probe tool (**Shift+E**, World menu; Hammer's `env_cubemap`): click a floor in the 3D view (the probe goes 170 cm above it) or a wall (81 cm out from it), or a spot in a 2D view; **Return** creates, **Esc** cancels. Size in **Properties** (Hammer's cubemapsize, default 32 x 32; the render quality halves it for Draft, doubles it for High). Click a probe's marker (a small ball) to select it, drag it in a 2D view or type its position, **Delete**, **Cmd+D**. Renders and the Camera Preview **build cubemaps** from each probe (2 passes at Normal and High, so shiny things reflect each other; 1 at Draft), kept in memory while nothing they show changes; shiny materials (`$envmap env_cubemap`) reflect the 4 nearest probes, blended. **View → 3D view: reflection probes as mirror balls** shows the last built pictures; **View → 3D view: where the selected probe reflects** tints the shiny surfaces it shows on |
| Camera Preview | The panel under Properties (while the map has a camera) shows a quick **draft render** of the active camera's view when you ask: **Cmd+K**, its **Update** button or Render → Update Camera Preview. Like a render it shows the map as it is (the View menu doesn't change it, hidden things are in it). It doesn't follow every change, so the editor stays quick; it tells you when the map has changed since |
| Render | **Render** button at the left of the status bar, Render → Render..., or **Cmd+Shift+K**: the active camera's picture at the camera's size, saved in the Bottega's `renders/` as `<map>_<camera>_<date-time>.png` with its log beside it. A terminal window shows the steps; the editor waits; **Esc** stops it. The map as it is: the View menu is ignored, hidden brushes are drawn, nodraw faces left out. The light is **baked** as part of the render (see Baked light) |
| Baked light | Every render bakes the lights the way Source's VRAD does: lightmaps on brush faces and terrain, a 3D lightmap around each primitive, shadows from brushes, terrain, primitives and props, sky light from the sky picture, smooth light across faces less than 45° apart, and **bounced light** (VRAD's radiosity: light passed between surfaces, coloured by their textures; primitives and props reflect too). The terminal shows `0...1...2...10` while it bakes; the bake is kept in memory while nothing that lights the map changes (moving the camera doesn't count). Lights (**Properties**): falloff (inverse-square as Hammer, 50% / 0% distance with hard falloff, or custom constant / linear / quadratic), the spot's exponent, the sun's spread (soft shadows). **World → Environment**: sky light (%), minimum light (the ambient colour everywhere, off), bounce strength (1 = as Source, 0 = none), smoothing angle. **Texture tool, Face panel**: lightmap scale (units per lightmap pixel, default 40 = Hammer's 16 units). Primitives: lightmap scale in **Properties**. Props (**Properties**): casts shadows, and **lighting**: baked per vertex (default; shadows, sky and bounce at every vertex) or like a Source model (the light grid's nearest ambient cube + its 2 strongest lights). The bake also makes Half-Life 2's **light grid** of ambient cubes; **View → 3D view: light grid** shows the last bake's samples as small cubes coloured by the light from each side |
| Render options | Render menu: **Quality** (Draft / Normal / High: sets the anti-aliasing and how finely the light is baked), **Anti-aliasing** (off, 2x–4x: drawn larger and scaled down), **Texture filtering** (smooth or crisp pixels; the editor's own is Texture → Filtering), **Prop light** (full, or quick: props baked per vertex with a quarter of the rays). Remembered between runs |
| Camera presets | In Properties (camera tool or a selected camera): **Save preset** keeps the lens, near / far, picture size, orthographic and exposure in the Bottega's `presets/camera_presets`; **Load a preset** puts them into the selected camera (one undo step) or the next camera |
| Sky, ambient light, fog | World → Environment… (built-in skies, or your own folders of 2:1 panoramas or 4:3 cube crosses) |
| Sky Lab | World → Sky Lab…: make your own sky (sky mode, sun position, colour, disk, glow, size, horizon illusion, stratus and cumulus clouds, haze, ground colour, the moon with phases and NASA's moon map, solar and lunar eclipses, halo rings, Earth's stars or a random field, comets, a rainbow, fog lying in the sky, Blend with Sun). **Save sky** / Load keeps skies in the Bottega's `presets/skies`; the list also has 13 **built-in** skies (sunset, clouds, moonlit night, eclipses, rainbow…); **Randomize sky** makes a new one. The 3D view shows it while the window is open; **Apply** bakes it into the Bottega's `skies/skylab/` and makes it the map's sky (one undo step), **OK** applies and closes, **Cancel** (Esc) drops the changes. With a sun light in the map, the lock keeps the sky's sun on it (Apply turns the light too); if the light turns later, World → Rebake Sky Lab sky |
| Editor preview | View menu: 3D view sky (on), rough light preview (off; no shadows, only computed while on), fog (off), scale figure (1.75 m) |
| Camera bookmarks / scale figure position | World menu |
| Heightmap / stitch / remove terrain | Terrain menu (Import heightmap: grey image, black/white heights typed) |
| Wrap a texture around a corner | Texture tool: click a face, **Option+Shift+click** the neighbouring face (repeat to go around); or pick several and use **Wrap around** (Face panel / Texture menu) |
| Hidden faces | Texture → Apply nodraw: faces with `nodraw` show in the editor but are never rendered or exported |
| Adjust a face's texture | **Face** panel (right): shift, scale, rotation, fit, justify, align to world/face |
| Bottega (shared folder) | File → Bottega folder...: choose the folder shared with Mazzuola and other tools once (it offers to make the sub-folders). Its `textures/` and `skies/` show up in Scalpellino; maps store them by path (`textures/walls/Brick_Red`) |
| Rescan the Bottega | **Cmd+R** (File menu), or **Rescan** in the Textures panel. Never automatic: new files appear when you ask |
| Your own textures | Texture → Add texture folder... (PNG, TGA, JPG, BMP; subfolders included): extra folders next to the Bottega |
| Texture lock, default scale, crisp/smooth | Texture menu |
| Textured / flat 3D view | View menu |
| Delete | **Delete** or **Backspace** (in any tool) |
| Undo / redo | **Cmd+Z** / **Cmd+Shift+Z** |
| New / open / save / save as | **Cmd+N** / **Cmd+O** / **Cmd+S** / **Cmd+Shift+S** (Mac file dialogs; File → Open by typing a path also works) |
| Center 2D views on selection | **Cmd+E** |
| Back to the origin | **Cmd+0** (all views), or the **origin** button in a view's corner |
| Hide selected / hide others / show all | **H** / **Shift+H** / **Cmd+U** |
| Show / hide all primitives or props | **View → Show primitives**, **View → Show props**: hidden ones can't be clicked, selected or landed on; **Cmd+U** or adding a new one shows them again |
| Visgroups | **Visgroups** tab next to Textures: name groups of brushes, show/hide them with a checkbox |
| Find brushes that cut into each other | **Cmd+Shift+O** — a list; **Next / Prev** jump the camera to each pair |
| Fly mode in 3D view (toggle) | **Z** while hovering the 3D view; **Esc** also exits |
| Move while flying | **W A S D**, **Q/E** down/up, hold **Shift** for fast |
| Look around without fly mode | hold **right mouse** in the 3D view (+ WASD) |
| Pan a 2D view | hold **middle mouse**, or **Space + left mouse** |
| Zoom a 2D view | mouse wheel (zooms toward the cursor) |
| Smaller / larger grid | **è** / **+** (the two keys right of P) |
| Show grid / snap to grid | **Shift+R** / **Shift+W** |

Keys are read by physical position, so they work the same on any keyboard layout.
The 2D grid is endless; the red square only marks the classic ±4096 area.
Built-in textures: Tile_19 and Bricks_24 (Screaming Brain Studios, CC0) and `nodraw`.
Maps are saved as Valve 220 `.map` files (readable by TrenchBroom and func_godot).

## Building locally

Needs CMake 3.24+ and Apple's command line tools (`xcode-select --install`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/Scalpellino.app
```

Dependencies (raylib, Dear ImGui, rlImGui) are downloaded automatically at pinned versions.
