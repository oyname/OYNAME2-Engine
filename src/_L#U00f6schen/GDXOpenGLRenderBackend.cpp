#include "GDXOpenGLRenderBackend.h"
#include "GDXOpenGLIncludes.h"
#include "Core/Debug.h"

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

ShaderHandle GDXOpenGLRenderBackend::UploadShader(
    ResourceStore<GDXShaderResource, ShaderTag>& /*shaderStore*/,
    const ShaderSourceDesc& /*desc*/)
{
    Debug::Log("GDXOpenGLRenderBackend: UploadShader nicht implementiert");
    return ShaderHandle::Invalid();
}

TextureHandle GDXOpenGLRenderBackend::UploadTexture(ResourceStore<GDXTextureResource, TextureTag>&,
                                                    const std::wstring&,
                                                    bool,
                                                    TextureHandle fallbackOnFailure)
{
    Debug::Log("GDXOpenGLRenderBackend: CreateTexture noch nicht implementiert");
    return fallbackOnFailure;
}

TextureHandle GDXOpenGLRenderBackend::UploadTextureFromImage(ResourceStore<GDXTextureResource, TextureTag>&,
                                                             const ImageBuffer&,
                                                             bool,
                                                             const std::wstring&,
                                                             TextureHandle fallbackOnFailure)
{
    Debug::Log("GDXOpenGLRenderBackend: CreateTextureFromImage noch nicht implementiert");
    return fallbackOnFailure;
}

bool GDXOpenGLRenderBackend::UploadMesh(MeshHandle, MeshAssetResource&)
{
    Debug::Log("GDXOpenGLRenderBackend: UploadMesh noch nicht implementiert");
    return false;
}

bool GDXOpenGLRenderBackend::UploadMaterial(MaterialHandle, MaterialResource&)
{
    return true;
}

void GDXOpenGLRenderBackend::ExtractLightData(Registry& registry, FrameData& frame)
{
    (void)registry;
    frame.lightCount = 0u;
    frame.hasShadowPass = false;
}

void GDXOpenGLRenderBackend::UploadLightConstants(const FrameData& /*frame*/)
{
    // OpenGL-Backend: light cbuffer upload not yet implemented.
}

void GDXOpenGLRenderBackend::UpdateFrameConstants(const FrameData&)
{
}

void GDXOpenGLRenderBackend::ExecuteRenderPass(const BackendRenderPassDesc& passDesc,
                                                Registry&,
                                                const ICommandList&,
                                                const ICommandList&,
                                                ResourceStore<MeshAssetResource, MeshTag>&,
                                                ResourceStore<MaterialResource, MaterialTag>&,
                                                ResourceStore<GDXShaderResource, ShaderTag>&,
                                                ResourceStore<GDXTextureResource, TextureTag>&,
                                                ResourceStore<GDXRenderTargetResource, RenderTargetTag>&)
{
    if (passDesc.kind == BackendRenderPassDesc::Kind::Shadow)
        return;

    if (!passDesc.target.useBackbuffer)
        Debug::Log("GDXOpenGLRenderBackend: RTT-Offscreen-Pass noch nicht implementiert");

    return;
}

void GDXOpenGLRenderBackend::SetShadowMapSize(uint32_t)
{
}

void GDXOpenGLRenderBackend::LoadIBL(const wchar_t* /*hdrPath*/)
{
    // OpenGL-Backend: IBL noch nicht implementiert.
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


PostProcessHandle GDXOpenGLRenderBackend::CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>&,
                                                                const PostProcessPassDesc&)
{
    Debug::Log("GDXOpenGLRenderBackend: PostProcess noch nicht implementiert");
    return PostProcessHandle::Invalid();
}

bool GDXOpenGLRenderBackend::UpdatePostProcessConstants(PostProcessResource&, const void*, uint32_t)
{
    return false;
}

void GDXOpenGLRenderBackend::DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>&)
{
}

bool GDXOpenGLRenderBackend::ExecutePostProcessChain(const std::vector<PostProcessHandle>&,
                                                     ResourceStore<PostProcessResource, PostProcessTag>&,
                                                     ResourceStore<GDXTextureResource, TextureTag>&,
                                                     ResourceStore<GDXRenderTargetResource, RenderTargetTag>*,
                                                     const PostProcessExecutionInputs&,
                                                     float,
                                                     float,
                                                     RenderTargetHandle,
                                                     bool)
{
    return false;
}
