# GIDX³ Engine

A custom C++ 3D game engine built on **DirectX 11**, designed with clean architecture, backend abstraction, and a modern ECS-based rendering pipeline.

---

## Features

### Rendering
- **DirectX 11** backend via abstracted `IGDXRenderBackend` interface
- **ECS-driven render pipeline** — `GDXECSRenderer` + `GDXDX11RenderExecutor`
- Opaque queue + transparent queue with back-to-front sorting
- **Render-to-Texture (RTT)** with per-pass camera support
- **Shadow Mapping** — PCF 3×3 soft shadows for directional lights
- **Layer system** — per-entity layer masks + per-camera cull masks
- Borderless window by default, opt-in framed window via `WindowDesc::borderless`

### Materials & Shading
- **Two shading models** switchable per material at runtime:
  - Legacy Blinn-Phong (diffuse, specular, shininess)
  - **PBR Cook-Torrance GGX** (metallic/roughness workflow)
- **Normal Mapping** — TBN reconstructed from screen-space derivatives (no tangent stream required)
- **ORM maps** (Occlusion / Roughness / Metallic combined) or separate textures
- Emissive color + emissive maps
- **Alpha Test** (`MF_ALPHA_TEST`) with configurable cutoff
- **Double-Sided** rendering (`MF_DOUBLE_SIDED`) — per draw call `CULL_NONE` in both main and shadow pass
- Transparent pass with alpha blending
- UV tiling & offset per material
- 5 texture blend modes (Multiply, Multiply×2, Additive, Alpha-Lerp, Luminance)

### Lighting
- Up to **32 simultaneous lights**
- Directional Light (shadow-casting, orthographic projection)
- Point Light with radius-based falloff
- Global ambient color
- Per-light diffuse and ambient color

### Transform & Camera
- **Quaternion-based** transform (no gimbal lock)
- Euler getters (Pitch / Yaw / Roll in degrees)
- Move / Rotate in Local or World space
- LookAt, Distance helpers
- Perspective and orthographic camera
- Per-camera cull mask

### Asset & Resource Management
- `ResourceStore<T>` — typed handle-based resource registry
- `GDXTextureLoader` — loads PNG/JPG/DDS via WIC
- `MeshAssetResource` — CPU-side mesh with GPU buffer upload
- `MaterialResource` — fully CPU-side, GPU constant buffer uploaded on demand
- Built-in default textures (white, flat normal, ORM, black)

### Skeletal Animation
- Bone palette up to **128 bones**
- Dedicated skinning vertex shader
- `SkinComponent` per entity

### Platform & Architecture
- Win32 + DXGI window and context creation
- `IGDXWindow` / `IGDXRenderBackend` interfaces — prepared for OpenGL and DX12 backends
- `GDXEventQueue` — decoupled input/window events
- `GDXECSRenderer` — tick callback + event callback pattern
- Forward declarations throughout — no `windows.h` leaking into public headers

---

## Requirements

| Tool | Version |
|------|---------|
| Visual Studio | 2022 |
| Windows SDK | 10.0+ |
| C++ Standard | C++17 |
| DirectX | 11 |

---

## Getting Started

```cpp
GDXEventQueue events;

WindowDesc desc;
desc.width      = 1280;
desc.height     = 720;
desc.title      = "My Game";
desc.borderless = false;   // true = borderless (default)

auto window  = std::make_unique<GDXWin32Window>(desc, events);
window->Create();

auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
GDXWin32DX11ContextFactory factory;
auto dxContext = factory.Create(*window,
    GDXWin32DX11ContextFactory::FindBestAdapter(adapters),
    desc.borderless);

auto backend  = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
auto renderer = std::make_unique<GDXECSRenderer>(std::move(backend));

GDXEngine engine(std::move(window), std::move(renderer), events);
engine.Initialize();
engine.Run();
```

---

## Creating a PBR Material

```cpp
MaterialResource mat;
mat.data.flags     = MF_SHADING_PBR | MF_DOUBLE_SIDED;
mat.data.metallic  = 0.0f;
mat.data.roughness = 0.5f;
mat.SetTexture(MaterialTextureSlot::Albedo,  renderer.LoadTexture(L"albedo.png",  true));
mat.SetTexture(MaterialTextureSlot::Normal,  renderer.LoadTexture(L"normal.png",  false));
mat.SetTexture(MaterialTextureSlot::ORM,     renderer.LoadTexture(L"orm.png",     false));

MaterialHandle hMat = renderer.CreateMaterial(mat);
```

---

## Project Structure

```
include/        — Public headers (interfaces, components, resources)
src/            — Engine implementation
  GDXDX11RenderBackend.cpp    — DX11 backend
  GDXDX11RenderExecutor.cpp   — Draw call execution, state batching
  GDXECSRenderer.cpp          — High-level renderer (ECS facade)
  GDXShadowMap.cpp            — Shadow map resources + passes
  GDXWin32Window.cpp          — Win32 window
  GDXWin32DX11ContextFactory.cpp — Device + swap chain creation
  TransformSystem.cpp         — World matrix propagation
  CameraSystem.cpp            — View/Proj matrix updates
  RenderGatherSystem.cpp      — ECS → RenderQueue gather
shaders/        — HLSL vertex + pixel shaders
```

---

## Roadmap

- [ ] HDR pipeline + Tone Mapping (ACES Filmic / Reinhard)
- [ ] Image-Based Lighting (IBL) — irradiance, prefiltered env map, BRDF-LUT
- [ ] Cascaded Shadow Maps
- [ ] Frustum Culling (AABB vs frustum)
- [ ] SSAO
- [ ] Bloom
- [ ] FXAA
- [ ] OpenGL backend
- [ ] ECS full migration (RenderSystem, ResourceManager)

---

## License

Private project. All rights reserved.
