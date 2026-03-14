# OYNAME2 Engine — Architecture Description

*C++ · DirectX 11 · ECS · Multi-Backend*

## 1. Architectural Overview

The OYNAME2 Engine is a C++ real-time rendering engine based on the Entity Component System (ECS) paradigm. It is currently built on DirectX 11, with a clear abstraction layer for future backends such as DirectX 12, Vulkan, and OpenGL.

The core design goal is to keep three concerns strictly separated:

- **What exists in the world**  
  ECS — registry, components, entities
- **What should be rendered**  
  `RenderGatherSystem`, `RenderQueue`, `RenderCommand`
- **How execution happens on the GPU**  
  `IGDXRenderBackend` → DX11 / DX12 / OpenGL

### Layer Model

1. **Application Layer**  
   App code, examples, game loop  
   `GDXEngine`, `GDXEventQueue`, `GDXFrameTimer`, sample apps such as `HelloCube` and `UV1DetailDemo`

2. **ECS Core**  
   Data storage only — no render logic  
   `Registry`, `ComponentPool<T>`, `EntityID`, `Handle<Tag>`, `ResourceStore<T, Tag>`, component structs

3. **Systems + Renderer Frontend**  
   Orchestration and frame construction  
   `TransformSystem`, `CameraSystem`, `HierarchySystem`, `RenderGatherSystem`, `RenderQueue`, `RenderCommand`, `GDXECSRenderer`

4. **Backend Abstraction Layer**  
   GPU API isolation  
   `IGDXRenderBackend`, `GDXDX11RenderBackend`, `GDXDX12RenderBackend` (stub), `GDXOpenGLRenderBackend` (stub), DX11 executor, pipeline cache, shadow map support

5. **Platform Layer**  
   Windowing, context creation, input  
   `IGDXWindow`, `GDXWin32Window`, `GDXWin32DX11ContextFactory`, `GDXWin32OpenGLContextFactory`, `IGDXPlatform`, `GDXWin32Platform`, `GDXInput`

### Design Rules

- Layers communicate only through interfaces and opaque handles.
- No direct DX11 type escapes beyond `IGDXRenderBackend`.
- The frontend (application + ECS + renderer frontend) is backend-agnostic at compile time.

---

## 2. ECS Core

The ECS model consists of three orthogonal concepts that remain strictly separated.

### Entity

An entity is only a **32-bit ID** (`EntityID`).

- No data
- No behavior
- 24-bit slot index
- 8-bit generation counter to detect use-after-destroy
- `NULL_ENTITY = EntityID{0}`
- Maximum: about 16.7 million simultaneous entities

### Component

Components are plain data structs.

- No business logic
- No virtual dispatch
- One responsibility per component

Examples:

- `TransformComponent` — geometry
- `VisibilityComponent` — render flags
- `LightComponent` — light data

### 2.1 Registry

The registry is the only class that knows all component pools.

Key properties:

- Uses `std::type_index` as key
- No compile-time registration required
- `View<T1, T2, ...>` iterates the smallest pool first (pivot iteration), then checks membership in the remaining pools

Example:

```cpp
EntityID cube = reg.CreateEntity();
reg.Add<TransformComponent>(cube, tc);
reg.Add<MeshRefComponent>(cube, hMesh, 0u);

reg.View<TransformComponent, MeshRefComponent>(
    [](EntityID id, TransformComponent& t, MeshRefComponent& m) { ... });
```

### 2.2 ResourceStore — Generation-Safe Resource Management

Shared GPU resources live in typed `ResourceStore<T, Tag>` instances inside `GDXECSRenderer`.

Access is strictly handle-based:

- `MeshHandle`
- `MaterialHandle`
- `TextureHandle`
- `ShaderHandle`
- `RenderTargetHandle`

Example resource types:

- `MeshAssetResource`
- `MaterialResource`
- `GDXTextureResource`

Rules:

- `Handle{0}` is invalid
- Generation mismatch returns `nullptr` instead of crashing
- `Release()` increments the generation immediately
- Handles are trivially copyable and safe to cache in render commands

---

## 3. Frame Loop and System Order

The frame loop runs in `GDXEngine::Step()`. Each frame follows this order:

1. **Process events**  
   `GDXEventQueue` → application event callback

2. **Tick**  
   `TickCallback(dt)` — application update logic

3. **BeginFrame**  
   `FrameContext.Begin()` and backend `BeginFrame()`

4. **EndFrame work**
   - `TransformSystem.Update()`
   - RTT passes
   - `CameraSystem` for RTT camera
   - `UpdateLights` + `UpdateFrameConstants`
   - `GatherShadow` → `ExecuteShadowPass`
   - `Gather` → `ExecutePass` to offscreen target
   - `CameraSystem.Update()` for main camera
   - `UpdateLights` + `UpdateFrameConstants`
   - main shadow pass
   - main pass
   - `Backend.Present(vsync)`

5. **FrameContext.End()**

### 3.1 TransformSystem

Computes world matrices from local transforms.

- Processes root entities first, then child entities
- Iterative hierarchy processing
- Resets `dirty = false` after update
- Must run first because all other systems read `WorldTransformComponent`

### 3.2 CameraSystem

Reads the entity tagged with `ActiveCameraTag` and computes:

- View matrix
- Projection matrix
- View-projection matrix

Writes exclusively to `FrameData`, never back into components.

For RTT passes, `BuildFrameDataForCamera()` is called with an explicit camera entity.

### 3.3 FrameData

`FrameData` is a per-frame data struct that is rebuilt every frame.

It contains:

- Camera matrices
- Light data (up to 32 lights)
- Shadow matrix
- Viewport dimensions

It is passed to backend calls by const reference.

### 3.4 FrameContext and Frames in Flight

- `GDXMaxFramesInFlight = 2`
- Each frame owns a `FrameContext`
- Each frame also owns a `FrameTransientResources` instance with:
  - Upload arena (64 KB)
  - Deferred release queue

Current state:

- **DX11:** structure only, no real GPU synchronization
- **DX12 / Vulkan:** intended to use `MarkSubmitted(fenceValue)` and wait before reusing frame resources

The infrastructure already exists; only fence logic remains to be implemented in future backends.

---

## 4. Render Pipeline

### 4.1 RenderGatherSystem — From ECS to RenderCommands

`RenderGatherSystem` bridges ECS and GPU execution.

For each visible entity it produces a `RenderCommand`.

It performs:

- Visibility checks
- Layer-mask and `cullMask` checks
- Shader variant resolution via `ShaderResolver`
- Construction of a `ResourceBindingSet` from the material
- Sort key generation  
  `Pass (2 bits) | Shader (14 bits) | Material (16 bits) | Depth (32 bits)`
- Filtering of self-referential RTT draws

### 4.2 RenderCommand — The Atomic Render Unit

Each `RenderCommand` is a self-describing GPU instruction.

Command types:

- **Draw**  
  Mesh + material + shader + world matrix + texture bindings + pipeline state
- **BindPipeline**  
  Shader switch + pipeline state (blend / cull / depth)
- **BindResources**  
  Material texture bindings + constant buffer
- **ClearCurrentTarget**  
  Clear backbuffer or RTT
- **TransitionTexture**  
  Resource state transition  
  - DX11: no-op or validation/tracking only  
  - DX12/Vulkan: real barriers

Important:

- Texture bindings use `ShaderResourceSemantic` such as `BaseColor`, `Normal`, `ORM`, `ShadowMap`
- The backend maps semantics to API-specific slots
- Resource state fields describe intended usage for future barrier generation

### 4.3 Record-then-Submit — DX12/Vulkan-Compatible Execution Model

The backend follows a record-then-submit model, even though DX11 still executes immediately.

**Phase 1 — Record:** `RecordMainPassCommandStream(inputQueue)`  
Transforms draw commands into an explicit sequence:

`BindPipeline → BindResources → Draw`

State batching removes redundant binds.

**Phase 2 — Submit:** `SubmitMainPassCommandStream(recordedQueue)`  
Processes opaque commands first, then transparent commands.

Outcome:

- **DX11 backend:** records and executes immediately
- **DX12 backend:** will fill real command lists
- **Frontend remains unchanged**

### 4.4 Shader Variants

`GDXECSRenderer` automatically selects shaders using a `ShaderVariantKey`:

- Pass
- Vertex flags
- Feature flags

Variants are cached. Irrelevant flags are normalized away for passes where they do not matter.

Main pass examples:

- `ECSVertexShader.hlsl`
- `ECSVertexShader_Skinned.hlsl`
- `ECSVertexShader_VertexColor.hlsl`
- `ECSPixelShader.hlsl`

Shadow pass examples:

- `ECSShadowVertexShader.hlsl`
- `ECSShadowVertexShader_Skinned.hlsl`
- `ECSShadowVertexShader_AlphaTest.hlsl`
- matching shadow pixel shaders

### 4.5 Shadow Pass

The shadow pass renders entities that either:

- have `ShadowCasterTag`, or
- set `VisibilityComponent.castShadows = true`

It renders into a depth-only shadow map.

The directional light with `castShadows = true` provides the shadow view-projection matrix, which is stored in `FrameData.shadowViewProjMatrix`.

The resulting shadow SRV is bound at **t16** in the pixel shader in the DX11 path.

### 4.6 Render Target Textures (RTT)

`RenderTargetCameraComponent` enables offscreen rendering into a texture.

Key points:

- RTT pass runs before the main pass
- The resulting `exposedTexture` can be used like any other material texture
- `skipSelfReferentialDraws` prevents a camera from rendering an RTT that it is also sampling as input

---

## 5. Material System

`MaterialResource` is the canonical description of a surface.

After an earlier transition phase with dual write paths, there is now only one canonical texture path:

```cpp
SetTexture(slot, handle, uvSet)
```

Behavior:

- Writes `textureLayers[slot].texture`
- Enables `textureLayers[slot]`
- Sets feature flags automatically  
  for example `MF_USE_NORMAL_MAP`
- Marks `cpuDirty = true`

`GetTexture(slot)` reads only from `textureLayers[]`.

### GPU Upload Behavior

`cpuDirty` controls upload of `MaterialData` to the GPU constant buffer (`b2`).

It does **not** control SRV binding. SRV binding is always derived from `GetTexture()`.

### 5.1 Texture Slots and UV Sets

| Slot     | DX11 Register | UV Set | Purpose |
|----------|---------------|--------|---------|
| Albedo   | t0            | UV0    | Base color texture (sRGB) |
| Normal   | t1            | UV0    | Normal map (linear) |
| ORM      | t2            | UV0    | Occlusion / Roughness / Metallic |
| Emissive | t3            | UV0    | Emissive texture (sRGB) |

---

## 6. Backend Structure

### 6.1 DX11 Backend Internals

The DX11 backend is split internally into specialized parts:

- backend wrapper
- render executor
- pipeline cache
- sampler cache
- shadow map support

This keeps frontend orchestration separate from API-specific command execution.

### 6.2 Vertex Streams (Multi-Stream Architecture)

The engine supports a multi-stream vertex architecture so that attributes can be separated cleanly.

Typical streams include:

- position
- normal
- UV
- tangent
- vertex color
- skinning data

This reduces unnecessary coupling in mesh data and makes variant-specific layouts easier to support.

---

## 7. Remaining Steps for DX12 / Vulkan

### 7.1 Still Open

The current architecture already contains the groundwork for explicit APIs, but the following items still remain:

- real fence-based synchronization
- real resource barriers
- descriptor heap / descriptor set binding model
- full command-list recording
- full backend implementations for DX12 and Vulkan

---

## 8. Backlog and Future Extensions

### 8.1 Multi-Material per Entity

Planned support for rendering one entity with multiple materials or submesh/material pairs.

### 8.2 Planned RenderGatherSystem Split

Further decomposition of gather responsibilities to improve clarity and extensibility.

### 8.3 Rendering Features (Backlog)

Additional rendering features are planned but not yet final in the current architecture.

Examples include broader backend support, richer pass handling, and more advanced material or lighting options.
