#include "GDXECSRenderer.h"
#include "GDXVertexFlags.h"
#include "Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// ===========================================================================
// Anonymer Namespace — Shader-Hilfsfunktionen
// ===========================================================================
namespace
{
    std::filesystem::path GetExecutableDirectory()
    {
        std::array<wchar_t, 4096> buf{};
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0 || len >= buf.size())
            return std::filesystem::current_path();
        return std::filesystem::path(buf.data()).parent_path();
    }

    std::filesystem::path FindShaderPath(const std::wstring& fileName)
    {
        const auto exeDir = GetExecutableDirectory();
        const std::vector<std::filesystem::path> candidates =
        {
            exeDir / L"shader" / fileName,
            exeDir / L".."    / L"shader" / fileName,
            exeDir / L"..\\..\\shader"     / fileName,
            exeDir / L"..\\..\\..\\"       / L"shader" / fileName,
            std::filesystem::current_path() / L"shader" / fileName,
            std::filesystem::current_path() / L".." / L"shader" / fileName,
        };

        for (const auto& c : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(c, ec))
                return std::filesystem::weakly_canonical(c, ec);
        }
        return {};
    }

    bool CompileShaderFromFile(
        const std::wstring& path,
        const std::string&  entryPoint,
        const std::string&  target,
        ID3DBlob**          blobOut)
    {
        if (!blobOut) return false;
        *blobOut = nullptr;

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ID3DBlob* errorBlob = nullptr;
        const HRESULT hr = D3DCompileFromFile(
            path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint.c_str(), target.c_str(), flags, 0, blobOut, &errorBlob);

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                Debug::LogError(GDX_SRC_LOC,
                    "Shader Compilation Error [", std::wstring(target.begin(), target.end()), L"] ",
                    path.c_str(), L" -> ",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }
            else
            {
                Debug::LogError(GDX_SRC_LOC, L"Shader nicht gefunden: ", path.c_str());
            }
            return false;
        }
        if (errorBlob) errorBlob->Release();
        return true;
    }

    // -----------------------------------------------------------------------
    // BuildInputLayout — dynamisch aus GDXVertexFlags gebaut.
    //
    // Entspricht OYNAME InputLayoutManager::CreateInputLayoutVertex exakt:
    //   Jedes Flag → ein D3D11_INPUT_ELEMENT_DESC mit D3D11_APPEND_ALIGNED_ELEMENT.
    //   Slot-Index = Reihenfolge der gesetzten Flags (separate multi-stream).
    // -----------------------------------------------------------------------
    HRESULT BuildInputLayout(
        ID3D11Device*     device,
        uint32_t          flags,
        ID3DBlob*         vsBlob,
        ID3D11InputLayout** layoutOut)
    {
        std::vector<D3D11_INPUT_ELEMENT_DESC> elems;
        elems.reserve(8);

        UINT slot = 0;

        // POSITION (Slot 0) — immer als erstes
        if (flags & GDX_VERTEX_POSITION)
        {
            elems.push_back({ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // NORMAL (eigener Slot)
        if (flags & GDX_VERTEX_NORMAL)
        {
            elems.push_back({ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // COLOR (eigener Slot)
        if (flags & GDX_VERTEX_COLOR)
        {
            elems.push_back({ "COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT, slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // TEXCOORD0 (eigener Slot)
        if (flags & GDX_VERTEX_TEX1)
        {
            elems.push_back({ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // TEXCOORD1 (eigener Slot)
        if (flags & GDX_VERTEX_TEX2)
        {
            elems.push_back({ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // TANGENT (eigener Slot, float4 xyz+handedness)
        if (flags & GDX_VERTEX_TANGENT)
        {
            elems.push_back({ "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // BLENDINDICES (eigener Slot)
        if (flags & GDX_VERTEX_BONE_INDICES)
        {
            elems.push_back({ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        // BLENDWEIGHT (eigener Slot)
        if (flags & GDX_VERTEX_BONE_WEIGHTS)
        {
            elems.push_back({ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, slot++,
                              0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        }

        if (elems.empty() || !vsBlob)
            return E_INVALIDARG;

        return device->CreateInputLayout(
            elems.data(),
            static_cast<UINT>(elems.size()),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            layoutOut);
    }

} // namespace

// ===========================================================================
// GDXECSRenderer
// ===========================================================================

GDXECSRenderer::GDXECSRenderer(std::unique_ptr<IGDXDXGIContext> context)
    : m_context(std::move(context))
{}

GDXECSRenderer::~GDXECSRenderer()
{
    Shutdown();
}

bool GDXECSRenderer::Initialize()
{
    if (!m_context || !m_context->IsValid())
        return false;

    m_device = static_cast<ID3D11Device*>(m_context->GetDevice());
    m_ctx    = static_cast<ID3D11DeviceContext*>(m_context->GetDeviceContext());

    if (!m_device || !m_ctx)
        return false;

    if (!CreateRenderStates())
        return false;

    m_meshUploader = new GDXDX11MeshUploader(m_device, m_ctx);

    GDXDX11RenderExecutor::InitParams ep;
    ep.device  = m_device;
    ep.context = m_ctx;
    if (!m_executor.Init(ep))
        return false;

    // Standard-Shader beim Start laden — danach ist m_defaultShader gültig.
    if (!LoadDefaultShaders())
        return false;

    m_initialized = true;
    return true;
}

// ---------------------------------------------------------------------------
// LoadDefaultShaders — Standard-Shader einmalig beim Start laden.
//
// Verwendet CreateShader → identischer Pfad wie Anwender-Aufrufe.
// Kein Sonderfall, kein hardcodiertes InputLayout.
// ---------------------------------------------------------------------------
bool GDXECSRenderer::LoadDefaultShaders()
{
    m_defaultShader = CreateShader(
        L"ECSVertexShader.hlsl",
        L"ECSPixelShader.hlsl",
        GDX_VERTEX_DEFAULT);   // POSITION | NORMAL | TEX1

    if (!m_defaultShader.IsValid())
    {
        Debug::LogError(GDX_SRC_LOC, "Standard-Shader konnte nicht geladen werden.");
        return false;
    }

    Debug::Log("GDXECSRenderer: Standard-Shader geladen (GDX_VERTEX_DEFAULT).");
    return true;
}

// ---------------------------------------------------------------------------
// CreateShader — öffentliche API (wie OYNAME CreateShader).
//
// 1. Shader-Dateien suchen und kompilieren.
// 2. InputLayout dynamisch aus vertexFlags bauen (BuildInputLayout).
// 3. GDXShaderResource in m_shaderStore ablegen → Handle zurückgeben.
// ---------------------------------------------------------------------------
ShaderHandle GDXECSRenderer::CreateShader(
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t            vertexFlags)
{
    const std::wstring debugName = vsFile + L" / " + psFile;
    return LoadShaderInternal(vsFile, psFile, vertexFlags, debugName);
}

ShaderHandle GDXECSRenderer::LoadShaderInternal(
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t            vertexFlags,
    const std::wstring& debugName)
{
    // 1. Shader-Dateien finden
    const auto vsPath = FindShaderPath(vsFile);
    const auto psPath = FindShaderPath(psFile);

    if (vsPath.empty() || psPath.empty())
    {
        Debug::LogError(GDX_SRC_LOC,
            L"Shader-Dateien nicht gefunden: ", vsFile.c_str(), L" / ", psFile.c_str());
        return ShaderHandle::Invalid();
    }

    // 2. Kompilieren
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;

    if (!CompileShaderFromFile(vsPath.wstring(), "main", "vs_5_0", &vsBlob))
        return ShaderHandle::Invalid();

    // 3. VS erstellen
    ID3D11VertexShader* vs = nullptr;
    if (FAILED(m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)))
    {
        vsBlob->Release();
        return ShaderHandle::Invalid();
    }

    // 4. InputLayout aus vertexFlags bauen — analog zu OYNAME InputLayoutManager
    ID3D11InputLayout* layout = nullptr;
    if (FAILED(BuildInputLayout(m_device, vertexFlags, vsBlob, &layout)))
    {
        Debug::LogError(GDX_SRC_LOC,
            L"InputLayout konnte nicht gebaut werden für: ", debugName.c_str());
        vs->Release();
        vsBlob->Release();
        return ShaderHandle::Invalid();
    }

    vsBlob->Release();  // Blob nach InputLayout-Bau freigeben (wie OYNAME)

    // 5. PS kompilieren + erstellen
    if (!CompileShaderFromFile(psPath.wstring(), "main", "ps_5_0", &psBlob))
    {
        vs->Release();
        layout->Release();
        return ShaderHandle::Invalid();
    }

    ID3D11PixelShader* ps = nullptr;
    if (FAILED(m_device->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)))
    {
        psBlob->Release();
        vs->Release();
        layout->Release();
        return ShaderHandle::Invalid();
    }
    psBlob->Release();

    // 6. In ShaderStore ablegen
    GDXShaderResource res;
    res.vertexFlags  = vertexFlags;
    res.vertexShader = vs;
    res.pixelShader  = ps;
    res.inputLayout  = layout;
    res.debugName    = debugName;

    return m_shaderStore.Add(std::move(res));
}

// ---------------------------------------------------------------------------
// CreateRenderStates
// ---------------------------------------------------------------------------
bool GDXECSRenderer::CreateRenderStates()
{
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode            = D3D11_FILL_SOLID;
    rd.CullMode            = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable     = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rd, &m_rasterizerState)))
        return false;

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable    = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc      = D3D11_COMPARISON_LESS;
    if (FAILED(m_device->CreateDepthStencilState(&dsd, &m_depthStencilState)))
        return false;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable           = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&bd, &m_blendState)))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Material-cbuffer
// ---------------------------------------------------------------------------
bool GDXECSRenderer::CreateMaterialCBuffer(MaterialResource& mat)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = sizeof(MaterialData);
    desc.Usage          = D3D11_USAGE_DYNAMIC;
    desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = &mat.data;

    ID3D11Buffer* buf = nullptr;
    if (FAILED(m_device->CreateBuffer(&desc, &init, &buf)))
        return false;

    mat.gpuConstantBuffer = buf;
    mat.cpuDirty          = false;
    return true;
}

// ---------------------------------------------------------------------------
// Ressourcen-API
// ---------------------------------------------------------------------------
MeshHandle GDXECSRenderer::UploadMesh(MeshAssetResource mesh)
{
    MeshHandle h = m_meshStore.Add(std::move(mesh));
    MeshAssetResource* r = m_meshStore.Get(h);
    if (r && m_meshUploader)
        m_meshUploader->Upload(*r);
    return h;
}

MaterialHandle GDXECSRenderer::CreateMaterial(MaterialResource mat)
{
    MaterialHandle h = m_matStore.Add(std::move(mat));
    MaterialResource* r = m_matStore.Get(h);
    if (r)
    {
        r->sortID = h.Index();
        CreateMaterialCBuffer(*r);
    }
    return h;
}

void GDXECSRenderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r; m_clearColor[1] = g;
    m_clearColor[2] = b; m_clearColor[3] = a;
}

// ---------------------------------------------------------------------------
// BeginFrame / EndFrame
// ---------------------------------------------------------------------------
void GDXECSRenderer::BeginFrame()
{
    if (!m_context || !m_ctx) return;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());

    m_ctx->OMSetRenderTargets(1, &rtv, dsv);
    m_ctx->ClearRenderTargetView(rtv, m_clearColor);
    m_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_ctx->RSSetState(m_rasterizerState);
    m_ctx->OMSetDepthStencilState(m_depthStencilState, 0u);
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_ctx->OMSetBlendState(m_blendState, blendFactor, 0xFFFFFFFF);
}

void GDXECSRenderer::EndFrame()
{
    const auto now = Clock::now();
    float dt = 0.0f;

    if (m_clockStarted)
    {
        dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
        if (dt > 0.1f) dt = 0.1f;
    }
    m_lastFrameTime = now;
    m_clockStarted  = true;

    if (m_tickCallback)
        m_tickCallback(dt);

    m_transformSystem.Update(m_registry);
    m_cameraSystem.Update(m_registry, m_frameData);

    // Gather mit defaultShader-Fallback
    m_gatherSystem.Gather(m_registry, m_frameData,
                          m_meshStore, m_matStore,
                          m_defaultShader,
                          m_opaqueQueue);

    m_executor.UpdateFrameConstants(m_frameData);

    // Dirty-Materialien hochladen
    m_matStore.ForEach([&](MaterialHandle, MaterialResource& mat)
    {
        if (mat.cpuDirty && mat.gpuConstantBuffer)
        {
            auto* cb = static_cast<ID3D11Buffer*>(mat.gpuConstantBuffer);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &mat.data, sizeof(MaterialData));
                m_ctx->Unmap(cb, 0);
            }
            mat.cpuDirty = false;
        }
    });

    // ShaderStore jetzt als Parameter
    m_executor.ExecuteQueue(m_opaqueQueue, m_meshStore, m_matStore, m_shaderStore);

    m_stats.drawCalls      = m_executor.GetDrawCallCount();
    m_stats.renderCommands = static_cast<uint32_t>(m_opaqueQueue.Count());

    if (m_context)
        m_context->Present(true);
}

void GDXECSRenderer::Resize(int w, int h)
{
    if (m_context) m_context->Resize(w, h);

    if (h > 0)
    {
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        m_registry.View<CameraComponent>([aspect](EntityID, CameraComponent& cam)
        {
            cam.aspectRatio = aspect;
        });
    }

    m_frameData.viewportWidth  = static_cast<float>(w);
    m_frameData.viewportHeight = static_cast<float>(h);
}

// ---------------------------------------------------------------------------
// Shutdown — Shader-Store explizit freigeben
// ---------------------------------------------------------------------------
void GDXECSRenderer::Shutdown()
{
    m_executor.Shutdown();

    // Material-cbuffer freigeben
    m_matStore.ForEach([](MaterialHandle, MaterialResource& mat)
    {
        if (mat.gpuConstantBuffer)
        {
            static_cast<ID3D11Buffer*>(mat.gpuConstantBuffer)->Release();
            mat.gpuConstantBuffer = nullptr;
        }
    });

    // Shader-GPU-Objekte freigeben
    m_shaderStore.ForEach([](ShaderHandle, GDXShaderResource& s)
    {
        if (s.vertexShader) { static_cast<ID3D11VertexShader*>(s.vertexShader)->Release(); s.vertexShader = nullptr; }
        if (s.pixelShader)  { static_cast<ID3D11PixelShader* >(s.pixelShader)->Release();  s.pixelShader  = nullptr; }
        if (s.inputLayout)  { static_cast<ID3D11InputLayout* >(s.inputLayout)->Release();  s.inputLayout  = nullptr; }
    });

    delete m_meshUploader;
    m_meshUploader = nullptr;

    if (m_rasterizerState)   { m_rasterizerState->Release();   m_rasterizerState   = nullptr; }
    if (m_depthStencilState) { m_depthStencilState->Release(); m_depthStencilState = nullptr; }
    if (m_blendState)        { m_blendState->Release();        m_blendState        = nullptr; }

    m_context.reset();
    m_initialized = false;
}
