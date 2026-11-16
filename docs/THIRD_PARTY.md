# Third-party components

| Component | License | Use |
|---|---|---|
| raylib | zlib | window, input, drawing |
| Dear ImGui | MIT | user interface |
| rlImGui | zlib | raylib ↔ ImGui glue |
| Roboto (Medium), via Dear ImGui `misc/fonts` | Apache License 2.0 | UI font, embedded in the app |

All are fetched at build time at pinned versions (see `cmake/Deps.cmake`).
