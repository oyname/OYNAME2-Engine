# OYNAME2 Engine — API and Architecture Reference

*Internal classes, structs, interfaces, and systems*

## 1. Architecture Overview

The OYNAME2 Engine follows the Entity Component System (ECS) model. The core consists of four layers:

1. **ECS Core** — data storage  
   `Registry`, `ComponentPool`, `EntityID`
2. **Systems** — logic  
   `TransformSystem`, `CameraSystem`, `RenderGatherSystem`
3. **Renderer** — orchestration, resources, frame loop  
   `GDXECSRenderer`
4. **Backend** — GPU API abstraction  
   `IGDXRenderBackend` → DX11 / DX12 / OpenGL

All shared resources (mesh, material, shader, texture, render target) live in typed `ResourceStore<T, Tag>` instances and are referenced through generation-safe `Handle<Tag>` values.

---

## 2. Core Types

### 2.1 EntityID  
**File:** `include/ECSTypes.h`

Unique 32-bit identity for an entity.

- 24-bit index
- 8-bit generation counter
- detects use-after-destroy

Key members:

- `EntityID::Make(index, gen)` — construct from index and generation
- `Index()` — returns slot index
- `Generation()` — returns generation counter
- `IsValid()` — `true` if not `NULL_ENTITY`
- `NULL_ENTITY` — sentinel value for “no entity”

### 2.2 Handle<Tag>  
**File:** `include/Handle.h`

Typed, generation-safe 32-bit resource handle.

- 20-bit index
- 12-bit generation
- tag type prevents mixing resource handle types at compile time

Predefined handle types:

- `MeshHandle`
- `MaterialHandle`
- `ShaderHandle`
- `TextureHandle`
- `RenderTargetHandle`

Key members:

- `Handle::Make(index, gen)`
- `Index()`
- `Generation()`
- `IsValid()`
- `Invalid()`

### 2.3 ResourceStore<T, Tag>  
**File:** `include/ResourceStore.h`

Generic slot map for shared resources.

Responsibilities:

- owns resources via move semantics
- returns stable handles
- detects stale handles using generation counters

Key operations:

- `Add(T resource)` — store a resource and return a handle
- `Get(handle)` — pointer or `nullptr`
- `IsValid(handle)`
- `Release(handle)` — frees the slot and increments the generation
- `ForEach(func)`
- `AliveCount()`
- `Empty()`

---

## 3. ECS System

### 3.1 Registry  
**File:** `include/Registry.h`

Central ECS registry that owns all component pools.

Important behavior:

- uses `std::type_index`
- no compile-time registration
- `View<T1, T2, ...>` iterates the smallest pool first

Typical operations:

- `CreateEntity()`
- `DestroyEntity(id)`
- `IsAlive(id)`
- `Add<T>(id, ...)`
- `Emplace<T>(id, ...)`
- `Insert<T>(id, value)`
- `Get<T>(id)`
- `Remove<T>(id)`
- `Has<T>(id)`
- `View<T...>(callback)`

### 3.2 ComponentPool<T>  
**File:** `include/ComponentPool.h`

Stores all components of a single type.

Typical operations:

- `Emplace(id, args...)`
- `Insert(id, value)`
- `Get(id)`
- `Remove(id)`
- `Has(id)`
- `Count()`
- `All()`

---

## 4. Components  
**File:** `include/Components.h`

All components are plain-data structs without business logic.

### 4.1 TagComponent

Readable entity name.

- `name: std::string`

### 4.2 TransformComponent

Local transform only. World space is stored separately.

Fields and helpers:

- `localPosition`
- `localRotation`
- `localScale`
- `dirty`
- `SetEulerDeg(pitch, yaw, roll)`

### 4.3 WorldTransformComponent

Computed world transform written only by `TransformSystem`.

Main fields:

- `matrix`
- `inverseMatrix`

### 4.4 ParentComponent / ChildrenComponent

Hierarchy components used for parent/child relationships in the scene graph.

Typical data:

- parent entity reference
- child entity list

### 4.5 MeshRefComponent

References a mesh and submesh index for rendering.

### 4.6 MaterialRefComponent

References a `MaterialHandle` used for rendering.

### 4.7 VisibilityComponent

Controls render participation.

Typical fields include:

- `visible`
- `active`
- `layerMask`
- `castShadows`

This component is used together with camera `cullMask` and shadow filtering.

### 4.8 CameraComponent

Defines camera parameters.

Typical fields:

- `fovDeg`
- `nearPlane`
- `farPlane`
- `aspectRatio`
- `cullMask`

### 4.9 ActiveCameraTag

Marker identifying the active main camera.

### 4.10 LightComponent

Describes a light source.

Typical fields include:

- light kind
- diffuse color
- intensity
- `castShadows`
- shadow ortho size
- shadow near/far range
- `affectLayerMask`
- `shadowLayerMask`

### 4.11 ShadowCasterTag

Marker tag that makes an entity eligible for shadow rendering.

### 4.12 SkinComponent

Runtime bone palette for skeletal animation.

Typical data:

- `finalBoneMatrices`
- `enabled`
- `MaxBones = 64`

### 4.13 RenderTargetCameraComponent

Offscreen camera component for render-to-texture passes.

Fields:

- `target: RenderTargetHandle`
- `enabled`
- `autoAspectFromTarget`
- `renderShadows`
- `renderOpaque`
- `renderTransparent`
- `skipSelfReferentialDraws`
- `clear: RenderPassClearDesc`

---

## 5. Resources

### 5.1 MaterialResource  
**File:** `include/MaterialResource.h`

Complete description of how a surface is rendered.

Key members:

- `data: MaterialData`
- `textureLayers`
- `cpuDirty`
- `gpuConstantBuffer`

Important rule:

- `SetTexture()` is the only canonical write path for textures

Typical operations:

- `SetTexture(slot, handle, uvSet)`
- `GetTexture(slot)`

### 5.2 MeshAssetResource  
**File:** `include/MeshAssetResource.h`

CPU-side mesh asset holding one or more submeshes and associated GPU upload data.

### 5.3 SubmeshData  
**File:** `include/SubmeshData.h`

CPU-side submesh representation.

Typical arrays:

- positions
- normals
- tangents
- UV sets
- colors
- skinning streams
- indices

### 5.4 GDXTextureResource  
**File:** `include/GDXTextureResource.h`

Texture resource with SRV and image metadata.

Typical fields:

- width
- height
- mip count
- format
- SRV / backend resource pointer

### 5.5 GDXRenderTargetResource  
**File:** `include/GDXRenderTargetResource.h`

Offscreen render target resource.

Typical contents:

- color target
- depth target
- exposed texture handle
- dimensions

### 5.6 GDXShaderResource  
**File:** `include/GDXShaderResource.h`

Backend shader package used by the renderer.

---

## 6. Systems

### 6.1 TransformSystem

Computes world transforms from local transforms and hierarchy relations.

### 6.2 CameraSystem

Builds view, projection, and view-projection matrices and writes them to `FrameData`.

### 6.3 RenderGatherSystem

Converts visible ECS entities into render commands.

Responsibilities:

- visibility and layer filtering
- camera `cullMask` filtering
- RTT self-reference filtering
- shader variant resolution
- resource binding generation
- shadow gathering

### 6.4 HierarchySystem

Maintains parent/child graph behavior for entities.

---

## 7. Renderer

### 7.1 GDXECSRenderer  
**File:** `include/GDXECSRenderer.h`

Central renderer implementation and owner of the main resource stores and backend.

Key responsibilities:

- initialize backend
- manage frame loop
- gather render commands
- submit shadow and main passes
- own resources

Important methods:

- `Initialize()`
- `BeginFrame()`
- `EndFrame()`
- `Tick(dt)`
- `Resize(w, h)`
- `Shutdown()`
- `SetTickCallback(fn)`
- `GetRegistry()`
- `LoadTexture(path, isSRGB)`
- `CreateTexture(image, name, isSRGB)`
- `UploadMesh(asset)`
- `CreateMaterial(mat)`
- `CreateShader(vsFile, psFile, vertexFlags)`
- `CreateRenderTarget(w, h, name)`
- `GetRenderTargetTexture(h)`
- `SetSceneAmbient(r, g, b)`
- `SetClearColor(r, g, b, a)`

### 7.2 FrameData  
**File:** `include/FrameData.h`

Per-frame data block written by systems and consumed by the backend.

Contains:

- camera matrices
- viewport dimensions
- ambient values
- shadow data
- light array
- layer masks for current frame/shadow pass

### 7.3 FrameContext  
**File:** `include/FrameContext.h`

Per-frame execution context used for frames-in-flight.

Contains:

- frame index
- frame number
- submit/completion information

### 7.4 FrameTransientResources  
**File:** `include/FrameTransientResources.h`

Per-frame transient memory and deferred-release support.

Used for:

- temporary upload allocations
- per-frame cleanup
- future explicit-API transient resource handling

---

## 8. Render Pipeline

### 8.1 RenderCommand  
**File:** `include/RenderCommand.h`

Self-describing render command structure.

Command kinds include:

- `Draw`
- `BindPipeline`
- `BindResources`
- `ClearCurrentTarget`
- `TransitionTexture`

Important payloads:

- mesh handle
- material handle
- shader handle
- world transform
- pipeline state
- resource binding set
- resource state transitions

### 8.2 RenderQueue  
**File:** `include/RenderQueue.h`

Command container with sort and submit helpers.

Responsibilities:

- collect commands
- sort draws
- preserve ordering for non-draw commands
- expose command stream to backend

### 8.3 ICommandList  
**File:** `include/ICommandList.h`

Lightweight interface used by the backend to read command streams without depending directly on `RenderQueue`.

### 8.4 GDXPipelineState and GDXPipelineStateDesc  
**File:** `include/GDXPipelineState.h`

Backend-neutral description of pipeline state.

Typical properties:

- blend mode
- cull mode
- depth mode
- alpha test flag
- depth test flag

### 8.5 GDXDX11PipelineCache  
**File:** `include/GDXPipelineCache.h`

DX11-side pipeline cache that emulates PSO-style caching using shader and pipeline-state keys.

### 8.6 GDXResourceState  
**File:** `include/GDXResourceState.h`

Explicit resource state model.

Typical states:

- `Unknown`
- `ShaderRead`
- `RenderTarget`
- `DepthWrite`
- `CopySource`
- `CopyDest`
- `Present`

### 8.7 ResourceBindingSet and ShaderResourceBindingDesc  
**File:** `include/GDXResourceBinding.h`

Binding model used by commands and materials.

Important data:

- semantic binding description
- binding index
- required resource state
- material constant buffer pointer
- texture binding list

Typical operations:

- `ResourceBindingSet.AddTextureBinding(desc)`
- `ResourceBindingSet.FindTextureBinding(semantic)`

---

## 9. Shader Variants  
**File:** `include/ShaderVariant.h`

Shader selection system used by `GDXECSRenderer`.

Key types:

- `ShaderPassType` — main or shadow
- `ShaderVariantFeature` — feature flags such as:
  - skinned
  - alpha test
  - transparent
  - vertex color
  - normal map
  - unlit
- `ShaderVariantKey`
- `ShaderVariantKeyHash`

Selection behavior:

- main pass uses ECS vertex shader variants plus `ECSPixelShader.hlsl`
- shadow pass uses shadow vertex/pixel shader variants
- irrelevant flags are normalized away for a given pass

---

## 10. Backend Interface

### 10.1 IGDXRenderBackend  
**File:** `include/IGDXRenderBackend.h`

Abstract backend interface separating the renderer frontend from the GPU API.

Implementations:

- `GDXDX11RenderBackend`
- `GDXDX12RenderBackend` (stub)
- `GDXOpenGLRenderBackend` (stub)

Typical methods:

- `Initialize(texStore)`
- `BeginFrame(clearColor / frameContext)`
- `EndFrame(frameContext)`
- `Present(vsync)`
- `Resize(w, h)`
- `Shutdown(...)`
- `CreateShader(...)`
- `CreateTexture(...)`
- `CreateRenderTarget(...)`
- `ExecuteShadowPass(...)`
- `ExecuteMainPass(...)`
- `ExecutePass(...)`

### 10.2 GDXDX11RenderBackend

Current production backend.

Internally uses executor, pipeline cache, sampler cache, and shadow-map helpers.

### 10.3 GDXDX12RenderBackend (Stub)

Intentional skeleton backend used as the starting point for a future explicit DX12 implementation.

---

## 11. Platform and Windowing

### 11.1 GDXEngine  
**File:** `include/GDXEngine.h`

Application-side engine wrapper handling window, renderer, event queue, and frame stepping.

### 11.2 GDXWin32Window

Win32 window implementation.

### 11.3 GDXEventQueue

Event queue used for application and engine event dispatch.

---

## 12. Vertex Flags and Shader Layout  
**Files:** `include/GDXVertexFlags.h`, `include/GDXShaderLayout.h`

Defines vertex-layout capability flags and shader-side layout expectations.

Typical flags describe availability of:

- positions
- normals
- tangents
- UV sets
- vertex colors
- skinning data

---

## 13. Debug Utilities  
**File:** `include/Debug.h`

Utility logging and debug output helpers used throughout the engine.

Typical use:

- log info
- warnings
- errors
- backend/runtime diagnostics

---

## Notes

This Markdown file is an English adaptation of the original German API and architecture reference. It keeps the original structure and terminology while simplifying some repeated table formatting for readability in Markdown.
