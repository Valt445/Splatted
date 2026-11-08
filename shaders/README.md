# shaders/

Drop GLSL here with these extensions; CMake picks them up automatically on the next build:

| File | Becomes | Load with |
|---|---|---|
| `splat.vert` | `build/<preset>/shaders/splat.vert.spv` | `base_load_shader(base, "splat.vert")` |
| `splat.frag` | `build/<preset>/shaders/splat.frag.spv` | `base_load_shader(base, "splat.frag")` |
| `sort.comp`  | `build/<preset>/shaders/sort.comp.spv`  | `base_load_shader(base, "sort.comp")` |

Compiled with `glslc --target-env=vulkan1.3 -g` (debug info on, so RenderDoc shows your source).
`#include` works, and edits to included files trigger a rebuild.

Keep them free of vendor-specific extensions so they run the same on NVIDIA and AMD.
