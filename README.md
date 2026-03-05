# AttributePainter — Nuke 17 USD Vertex Colour Painter

A high-performance C++ plugin for Nuke 17 that replicates Houdini's **Attribute Paint SOP** for interactive vertex colour painting on USD geometry.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Nuke 17 Viewer (Hydra / Storm)                                     │
│                                                                     │
│  ┌────────────────────┐   mouse events    ┌───────────────────────┐ │
│  │  ViewportBrushKnob │ ◄──────────────── │  DD::Image::Knob      │ │
│  │  (GL overlay +     │                   │  handle system        │ │
│  │   ray casting)     │ ──paint callback─►│  AttributePainterOp   │ │
│  └────────────────────┘                   └───────────┬───────────┘ │
│                                                       │             │
└───────────────────────────────────────────────────────┼─────────────┘
                                                        │
             ┌──────────────────────────────────────────┼────────────┐
             │                                          │            │
             ▼                                          ▼            │
    ┌─────────────────┐                     ┌───────────────────┐   │
    │   MeshSampler   │                     │   USDColorWriter  │   │
    │                 │                     │                   │   │
    │  ┌───────────┐  │  verticesInRadius   │  primvar write    │   │
    │  │  KD-tree  │◄─┼─────────────────   │  (displayColor)   │   │
    │  └───────────┘  │                     └──────────┬────────┘   │
    │  ┌───────────┐  │                                │            │
    │  │ BVH/AABB  │◄─┼── intersect(ray)               │            │
    │  └───────────┘  │                                ▼            │
    │  color buffer   │                     ┌───────────────────┐   │
    └─────────────────┘                     │   USD Stage       │   │
             │                              │   (UsdGeomMesh)   │   │
             │    ┌──────────────────┐      └───────────────────┘   │
             └───►│   BrushSystem    │                               │
                  │ (weight/blend    │      ┌───────────────────┐   │
                  │  math, header-   │      │   UndoStack       │   │
                  │  only)           │      │   (stroke-level)  │   │
                  └──────────────────┘      └───────────────────┘   │
                                                                     │
```

### Module Summary

| File | Responsibility |
|---|---|
| `AttributePainterOp` | Main GeoOp: orchestrates everything, owns all subsystems |
| `ViewportBrushKnob` | Custom DDImage::Knob: GL brush overlay, mouse→ray casting |
| `MeshSampler` | BVH ray-intersection + KD-tree radius query + colour buffer |
| `BrushSystem` | Stateless weight/falloff/blend math (header-only, inlined) |
| `USDColorWriter` | OpenUSD primvar read/write with staged commit batching |
| `UndoStack` | Stroke-level before/after snapshot undo/redo |
| `Types.h` | Shared POD structs (Vec3f, Color3f, Ray, HitResult, …) |

---

## Build

### Prerequisites

- **Nuke 17.0+** NDK headers and `libDDImage`
- **OpenUSD** (Nuke 17 ships its own build in `<NUKE_ROOT>/usd/`)
- CMake ≥ 3.20
- GCC 11+ or Clang 14+ with C++17

### Linux / macOS

```bash
mkdir build && cd build

cmake .. \
  -DNUKE_ROOT=/usr/local/Nuke17.0 \
  -DUSD_ROOT=/usr/local/Nuke17.0/usd \
  -DCMAKE_BUILD_TYPE=Release

cmake --build . -j$(nproc)
cmake --install .    # copies to $NUKE_ROOT/plugins/
```

### Windows (MSVC)

```bat
cmake .. -G "Visual Studio 17 2022" -A x64 ^
  -DNUKE_ROOT="C:/Program Files/Nuke17.0" ^
  -DUSD_ROOT="C:/Program Files/Nuke17.0/usd"
cmake --build . --config Release
```

---

## Installation

1. Copy `AttributePainter.so` (or `.dll`) to a folder on your `NUKE_PATH`.
2. Or add to `init.py`:
   ```python
   nuke.pluginAddPath('/path/to/your/plugins')
   ```
3. The node appears under **3D → AttributePainter** in the Node Graph.

---

## Usage

1. Connect a **USD** node (e.g. `ReadGeo` pointing at a `.usd` file) to the input.
2. In the **AttributePainter** properties:
   - Set **Prim Path** to the mesh you want to paint (e.g. `/World/Geo/Body`).
   - Optionally change **Primvar Name** (default: `displayColor`).
3. Open the **3D Viewer** and click in it to activate.
4. **Paint**: hold **LMB** and drag.
5. **Resize brush**: **Ctrl + Scroll**.
6. **Undo/Redo**: standard Nuke `Ctrl+Z` / `Ctrl+Shift+Z`.

---

## Knob Reference

| Knob | Description |
|---|---|
| Prim Path | SdfPath to the UsdGeomMesh |
| Primvar Name | USD primvar to write (default `displayColor`) |
| Enable Paint | Toggle painting on/off |
| Show Brush | Toggle brush circle overlay |
| Radius | World-space brush radius |
| Strength | Paint opacity per tick [0–1] |
| Hardness | Inner ring size (1=flat top, 0=full feather) |
| Falloff | Smooth / Linear / Constant / Gaussian |
| Blend Mode | Replace / Add / Subtract / Multiply / Smooth |
| Color | Target paint colour |
| Flip Normals | Reverses surface normal direction for overlay |

---

## Extending

### Add a new Blend Mode
1. Add entry to `BlendMode` enum in `Types.h`.
2. Add entry to `kBlendNames[]` in `AttributePainterOp.cpp`.
3. Add case in `BrushSystem::blend()`.

### Paint other attributes (normals, floats, etc.)
- Subclass `USDColorWriter` → implement `read()`/`write()` for `Normal3f`, `float`, etc.
- Swap `Color3f` for your type in `MeshSampler`.

### Multi-prim painting
- Call `rebuildGeometry()` for each prim path, storing a sampler per prim.
- Merge paint callbacks.

### Pressure-sensitive tablet support
- Nuke's `ViewerContext` exposes `pressure()` (Wacom/WinTab).
  Multiply `bs.strength` by `ctx->pressure()` inside `onPaintTick`.

---

## Performance Notes

- **KD-tree** (build: O(n log n), query: O(log n + k)) handles meshes with millions of points without stutter.
- **BVH** ray-intersection uses iterative traversal (stack-allocated) — no recursion overhead.
- **USD commit** is batched: vertex writes accumulate in `staged_` and are flushed in a single `VtArray` copy per tick, avoiding USD authoring overhead per-vertex.
- All math is `float` with `fast-math` enabled; inner loop is branch-free.

---

## License

MIT — see `LICENSE`.
