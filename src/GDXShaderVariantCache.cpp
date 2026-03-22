#include "GDXShaderVariantCache.h"
#include "IGDXRenderBackend.h"
#include "GDXVertexFlags.h"
#include "Debug.h"

namespace
{
    constexpr uint32_t kRelevantMainFeatures   = SVF_SKINNED | SVF_VERTEX_COLOR;
    constexpr uint32_t kRelevantShadowFeatures = SVF_SKINNED | SVF_ALPHA_TEST;
}

void GDXShaderVariantCache::Init(IGDXRenderBackend* backend,
                                  ResourceStore<GDXShaderResource, ShaderTag>* shaderStore)
{
    m_backend     = backend;
    m_shaderStore = shaderStore;
}

bool GDXShaderVariantCache::LoadDefaults()
{
    ShaderVariantKey mainKey{};
    mainKey.pass         = ShaderPassType::Main;
    mainKey.vertexFlags  = GDX_VERTEX_DEFAULT;
    mainKey.features     = SVF_NONE;
    m_defaultShader = CreateVariant(mainKey);
    if (!m_defaultShader.IsValid()) return false;

    ShaderVariantKey shadowKey{};
    shadowKey.pass        = ShaderPassType::Shadow;
    shadowKey.vertexFlags = GDX_VERTEX_POSITION;
    shadowKey.features    = SVF_NONE;
    m_shadowShader = CreateVariant(shadowKey);

    if (!m_shadowShader.IsValid())
        Debug::Log("GDXShaderVariantCache: Kein Shadow-Shader gefunden — Shadow Pass deaktiviert.");

    return true;
}

ShaderHandle GDXShaderVariantCache::LoadShader(
    const std::wstring& vsFile, const std::wstring& psFile, uint32_t vertexFlags)
{
    return LoadInternal(vsFile, psFile, vertexFlags, vsFile + L" / " + psFile, nullptr);
}

ShaderHandle GDXShaderVariantCache::LoadShader(
    const std::wstring& vsFile, const std::wstring& psFile,
    uint32_t vertexFlags, const GDXShaderLayout& layout)
{
    return LoadInternal(vsFile, psFile, vertexFlags, vsFile + L" / " + psFile, &layout);
}

ShaderHandle GDXShaderVariantCache::Resolve(
    RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
{
    if (pass != RenderPass::Shadow && mat.shader.IsValid())
        return mat.shader;

    const ShaderVariantKey key = NormalizeKey(BuildKey(pass, submesh, mat));
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    return CreateVariant(key);
}

ShaderHandle GDXShaderVariantCache::LoadInternal(
    const std::wstring& vsFile, const std::wstring& psFile,
    uint32_t vertexFlags, const std::wstring& debugName,
    const GDXShaderLayout* customLayout)
{
    if (!m_backend || !m_shaderStore) return ShaderHandle::Invalid();
    const GDXShaderLayout layout = customLayout
        ? *customLayout
        : GDXShaderLayouts::BuildMain(vertexFlags, (vertexFlags & GDX_VERTEX_BONE_WEIGHTS) != 0u);
    return m_backend->CreateShader(*m_shaderStore, vsFile, psFile, vertexFlags, layout, debugName);
}

ShaderVariantKey GDXShaderVariantCache::BuildKey(
    RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat) const
{
    ShaderVariantKey key{};
    key.pass        = (pass == RenderPass::Shadow) ? ShaderPassType::Shadow : ShaderPassType::Main;
    key.vertexFlags = submesh.ComputeVertexFlags();

    if (submesh.HasSkinning()) key.features |= SVF_SKINNED;
    if (!submesh.colors.empty() && submesh.colors.size() == submesh.positions.size())
        key.features |= SVF_VERTEX_COLOR;
    if (mat.IsAlphaTest())   key.features |= SVF_ALPHA_TEST;
    if (mat.IsTransparent()) key.features |= SVF_TRANSPARENT;
    if (mat.data.flags & MF_USE_NORMAL_MAP) key.features |= SVF_NORMAL_MAP;
    if (mat.data.flags & MF_UNLIT)          key.features |= SVF_UNLIT;

    return key;
}

ShaderVariantKey GDXShaderVariantCache::NormalizeKey(const ShaderVariantKey& in) const
{
    ShaderVariantKey key = in;
    if (key.pass == ShaderPassType::Main)
        key.features &= kRelevantMainFeatures;
    else
        key.features &= kRelevantShadowFeatures;
    key.vertexFlags &= ~GDX_VERTEX_TANGENT;
    return key;
}

ShaderHandle GDXShaderVariantCache::CreateVariant(const ShaderVariantKey& rawKey)
{
    const ShaderVariantKey key = NormalizeKey(rawKey);

    std::wstring vsFile, psFile;
    uint32_t vertexFlags = key.vertexFlags;

    const bool skinned      = (key.features & SVF_SKINNED)       != 0u;
    const bool vertexColor  = (key.features & SVF_VERTEX_COLOR)  != 0u;
    const bool alphaTest    = (key.features & SVF_ALPHA_TEST)    != 0u;

    if (key.pass == ShaderPassType::Main)
    {
        psFile = L"PixelShader.hlsl";
        if      (skinned && vertexColor) vsFile = L"VertexShader_SkinnedVertexColor.hlsl";
        else if (skinned)                vsFile = L"VertexShader_Skinned.hlsl";
        else if (vertexColor)            vsFile = L"VertexShader_VertexColor.hlsl";
        else                             vsFile = L"VertexShader.hlsl";
    }
    else
    {
        if (skinned && alphaTest)
        {
            vsFile = L"ShadowVertexShader_SkinnedAlphaTest.hlsl";
            psFile = L"ShadowPixelShader_AlphaTest.hlsl";
            vertexFlags = GDX_VERTEX_POSITION | GDX_VERTEX_TEX1 | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS;
        }
        else if (skinned)
        {
            vsFile = L"ShadowVertexShader_Skinned.hlsl";
            psFile = L"ShadowPixelShader.hlsl";
            vertexFlags = GDX_VERTEX_POSITION | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS;
        }
        else if (alphaTest)
        {
            vsFile = L"ShadowVertexShader_AlphaTest.hlsl";
            psFile = L"ShadowPixelShader_AlphaTest.hlsl";
            vertexFlags = GDX_VERTEX_POSITION | GDX_VERTEX_TEX1;
        }
        else
        {
            vsFile = L"ShadowVertexShader.hlsl";
            psFile = L"ShadowPixelShader.hlsl";
            vertexFlags = GDX_VERTEX_POSITION;
        }
    }

    const std::wstring debugName = L"Variant: " + vsFile + L" / " + psFile;
    const GDXShaderLayout layout = (key.pass == ShaderPassType::Shadow)
        ? GDXShaderLayouts::BuildShadow(vertexFlags, skinned, alphaTest)
        : GDXShaderLayouts::BuildMain(vertexFlags, skinned);

    if (!m_backend || !m_shaderStore) return ShaderHandle::Invalid();
    ShaderHandle handle = m_backend->CreateShader(*m_shaderStore, vsFile, psFile, vertexFlags, layout, debugName);
    if (!handle.IsValid()) return ShaderHandle::Invalid();

    if (auto* res = m_shaderStore->Get(handle))
    {
        res->passType        = key.pass;
        res->variantFeatures = key.features;
        res->supportsSkinning = skinned;
        res->usesVertexColor  = vertexColor;
    }

    m_cache.emplace(key, handle);
    return handle;
}
