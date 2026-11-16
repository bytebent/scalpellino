# Third-party components

| Component | License | Use |
|---|---|---|
| raylib | zlib | window, input, drawing |
| Dear ImGui | MIT | user interface |
| rlImGui | zlib | raylib ↔ ImGui glue |
| Roboto (Medium), via Dear ImGui `misc/fonts` | Apache License 2.0 | UI font, embedded in the app |

All are fetched at build time at pinned versions (see `cmake/Deps.cmake`).

## Bundled textures

| Files | Source | License |
|---|---|---|
| `assets/textures/Tile_19.png`, `assets/textures/Bricks_24.png` | Screaming Brain Studios (provided by the project owner) | CC0 1.0 / public domain — see `assets/textures/LICENSE-ScreamingBrainStudios.txt` |

Converted from 16-bit RGBA to 8-bit RGB PNG (no visual change) to keep the app small.
