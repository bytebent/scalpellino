# Third-party components

| Component | License | Use |
|---|---|---|
| raylib | zlib | window, input, drawing |
| Dear ImGui | MIT | user interface |
| rlImGui | zlib | raylib ↔ ImGui glue |
| Roboto (Medium), via Dear ImGui `misc/fonts` | Apache License 2.0 | UI font, embedded in the app |

All are fetched at build time at pinned versions (see `cmake/Deps.cmake`).

## Code

| Files | Source | License |
|---|---|---|
| `third_party/cgltf/cgltf.h` | cgltf v1.15 (commit 85cd62382dfea638278962690cf515023f33ed00), Johannes Kuhlmann | MIT (the license text is at the top of the file) |

## Bundled textures

| Files | Source | License |
|---|---|---|
| `assets/textures/Tile_19.png`, `assets/textures/Bricks_24.png`, `assets/textures/Tile_13.png`, `assets/textures/Grass_20.png` | Screaming Brain Studios (provided by the project owner) | CC0 1.0 / public domain — see `assets/textures/LICENSE-ScreamingBrainStudios.txt` |

| `assets/skies/Panorama_Sky_04.png`, `Panorama_Sky_15.png`, `Panorama_Sky_21.png` | Screaming Brain Studios (provided by the project owner) | CC0 1.0 / public domain — see `assets/skies/LICENSE-ScreamingBrainStudios.txt` |
| `tests/ui/assets/skies/Cubemap_Sky_21.png` (test only, scaled down) | Screaming Brain Studios (provided by the project owner) | CC0 1.0 / public domain |

`assets/textures/nodraw.png` is our own (CC0), text set in Roboto.

Tile_19 and Bricks_24 were converted from 16-bit RGBA to 8-bit RGB PNG, and Tile_13 and Grass_20
from RGBA to RGB (fully opaque, no visual change), to keep the app small. The skies are
stored as 8-bit RGB PNG.
