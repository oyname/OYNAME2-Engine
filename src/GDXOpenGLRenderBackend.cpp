#include "GDXOpenGLRenderBackend.h"
#include "GDXOpenGLIncludes.h"
#include "Debug.h"

#include <utility>

GDXOpenGLRenderBackend::GDXOpenGLRenderBackend(std::unique_ptr<IGDXOpenGLContext> context)
    : m_context(std::move(context))
{
}

bool GDXOpenGLRenderBackend::Initialize(ResourceStore<GDXTextureResource, TextureTag>&)
{
    if (!m_context)
    {
        Debug::LogError("GDXOpenGLRenderBackend: context is null");
        return false;
    }

    if (!m_context->MakeCurrent())
    {
        Debug::LogError("GDXOpenGLRenderBackend: MakeCurrent failed during Initialize");
        return false;
    }

    const auto info = m_context->QueryDeviceInfo();
    Debug::Log("GDXOpenGLRenderBackend: vendor=", info.vendor);
    Debug::Log("GDXOpenGLRenderBackend: renderer=", info.renderer);
    Debug::Log("GDXOpenGLRenderBackend: version=", info.version);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    return true;
}

void GDXOpenGLRenderBackend::BeginFrame(const float clearColor[4])
{
    if (!m_context || !m_context->MakeCurrent())
        return;

    m_clearColor[0] = clearColor[0];
    m_clearColor[1] = clearColor[1];
    m_clearColor[2] = clearColor[2];
    m_clearColor[3] = clearColor[3];

    glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GDXOpenGLRenderBackend::Present(bool)
{
    if (m_context)
        m_context->SwapBuffers();
}

void GDXOpenGLRenderBackend::Resize(int w, int h)
{
    m_width = w;
    m_height = h;

    if (!m_context)
        return;

    m_context->Resize(w, h);
    if (m_context->MakeCurrent())
        glViewport(0, 0, w, h);
}

void GDXOpenGLRenderBackend::Shutdown(ResourceStore<MaterialResource, MaterialTag>&,
                                      ResourceStore<GDXShaderResource, ShaderTag>&,
                                      ResourceStore<GDXTextureResource, TextureTag>&)
{
    m_context.reset();
}

ShaderHandle GDXOpenGLRenderBackend::CreateShader(ResourceStore<GDXShaderResource, ShaderTag>&,
                                                  const std::wstring&,
                                                  const std::wstring&,
                                                  uint32_t,
                                                  const GDXShaderLayout&,
                                                  const std::wstring&)
{
    Debug::Log("GDXOpenGLRenderBackend: CreateShader noch nicht implementiert");
    return ShaderHandle::Invalid();
}

TextureHandle GDXOpenGLRenderBackend::CreateTexture(ResourceStore<GDXTextureResource, TextureTag>&,
                                                    const std::wstring&,
                                                    bool,
                                                    TextureHandle fallbackOnFailure)
{
    Debug::Log("GDXOpenGLRenderBackend: CreateTexture noch nicht implementiert");
    return fallbackOnFailure;
}

TextureHandle GDXOpenGLRenderBackend::CreateTextureFromImage(ResourceStore<GDXTextureResource, TextureTag>&,
                                                             const ImageBuffer&,
                                                             bool,
                                                             const std::wstring&,
                                                             TextureHandle fallbackOnFailure)
{
    Debug::Log("GDXOpenGLRenderBackend: CreateTextureFromImage noch nicht implementiert");
    return fallbackOnFailure;
}

bool GDXOpenGLRenderBackend::UploadMesh(MeshAssetResource&)
{
    Debug::Log("GDXOpenGLRenderBackend: UploadMesh noch nicht implementiert");
    return false;
}

bool GDXOpenGLRenderBackend::CreateMaterialGpu(MaterialResource&)
{
    return true;
}

void GDXOpenGLRenderBackend::UpdateLights(Registry&, FrameData& frame)
{
    frame.lightCount = 0u;
    frame.hasShadowPass = false;
}

void GDXOpenGLRenderBackend::UpdateFrameConstants(const FrameData&)
{
}

void GDXOpenGLRenderBackend::ExecuteShadowPass(Registry&,
                                               const RenderQueue&,
                                               ResourceStore<MeshAssetResource, MeshTag>&,
                                               ResourceStore<MaterialResource, MaterialTag>&,
                                               ResourceStore<GDXShaderResource, ShaderTag>&,
                                               ResourceStore<GDXTextureResource, TextureTag>&,
                                               const FrameData&)
{
}

void* GDXOpenGLRenderBackend::ExecuteMainPass(Registry&,
                                              const RenderQueue&,
                                              ResourceStore<MeshAssetResource, MeshTag>&,
                                              ResourceStore<MaterialResource, MaterialTag>&,
                                              ResourceStore<GDXShaderResource, ShaderTag>&,
                                              ResourceStore<GDXTextureResource, TextureTag>&)
{
    return nullptr;
}

void* GDXOpenGLRenderBackend::ExecuteMainPassToTarget(GDXRenderTargetResource&,
                                                      const RenderPassClearDesc&,
                                                      Registry&,
                                                      const RenderQueue&,
                                                      ResourceStore<MeshAssetResource, MeshTag>&,
                                                      ResourceStore<MaterialResource, MaterialTag>&,
                                                      ResourceStore<GDXShaderResource, ShaderTag>&,
                                                      ResourceStore<GDXTextureResource, TextureTag>&)
{
    Debug::Log("GDXOpenGLRenderBackend: RTT-Offscreen-Pass noch nicht implementiert");
    return nullptr;
}

void GDXOpenGLRenderBackend::SetShadowMapSize(uint32_t)
{
}

uint32_t GDXOpenGLRenderBackend::GetDrawCallCount() const
{
    return 0u;
}

bool GDXOpenGLRenderBackend::HasShadowResources() const
{
    return false;
}

const IGDXRenderBackend::DefaultTextureSet& GDXOpenGLRenderBackend::GetDefaultTextures() const
{
    return m_defaultTextures;
}
