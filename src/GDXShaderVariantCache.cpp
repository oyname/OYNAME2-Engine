#include "GDXShaderVariantCache.h"
#include "IGDXRenderBackend.h"
#include "GDXVertexFlags.h"
#include "Core/Debug.h"

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
    if (pass != RenderPass::Shadow && mat.GetShader().IsValid())
        return mat.GetShader();

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
    const ShaderSourceDesc desc = ShaderSourceDesc::FromHlslFiles(vsFile, psFile, vertexFlags, layout, debugName);
    return m_backend->UploadShader(*m_shaderStore, desc);
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
    if (mat.HasTexture(MaterialTextureSlot::Normal)) key.features |= SVF_NORMAL_MAP;
    if (mat.IsUnlit())        key.features |= SVF_UNLIT;

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
    std::vector<std::string> defines;

    const bool skinned     = (key.features & SVF_SKINNED)      != 0u;
    const bool vertexColor = (key.features & SVF_VERTEX_COLOR) != 0u;
    const bool alphaTest   = (key.features & SVF_ALPHA_TEST)   != 0u;

    if (key.pass == ShaderPassType::Main)
    {
        vsFile = m_config.mainVS;
        psFile = m_config.mainPS;
        if (vertexColor) defines.push_back("HAS_VERTEX_COLOR");
        if (skinned)     defines.push_back("HAS_SKINNING");
    }
    else
    {
        vsFile = m_config.shadowVS;
        psFile = m_config.shadowPS;

        if (skinned)
        {
            defines.push_back("HAS_SKINNING");
            vertexFlags = alphaTest
                ? GDX_VERTEX_POSITION | GDX_VERTEX_TEX1 | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS
                : GDX_VERTEX_POSITION | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS;
        }
        else
        {
            vertexFlags = alphaTest ? GDX_VERTEX_POSITION | GDX_VERTEX_TEX1 : GDX_VERTEX_POSITION;
        }

        if (alphaTest)
            defines.push_back("ALPHA_TEST");
    }

    const std::wstring debugName = L"Variant: " + vsFile + L" / " + psFile;
    const GDXShaderLayout layout = (key.pass == ShaderPassType::Shadow)
        ? GDXShaderLayouts::BuildShadow(vertexFlags, skinned, alphaTest)
        : GDXShaderLayouts::BuildMain(vertexFlags, skinned);

    if (!m_backend || !m_shaderStore) return ShaderHandle::Invalid();
    ShaderSourceDesc desc = ShaderSourceDesc::FromHlslFiles(vsFile, psFile, vertexFlags, layout, debugName);
    if (auto* stage = desc.VertexStage()) stage->defines = defines;
    if (auto* stage = desc.PixelStage()) stage->defines = defines;
    ShaderHandle handle = m_backend->UploadShader(*m_shaderStore, desc);
    if (!handle.IsValid()) return ShaderHandle::Invalid();

    if (auto* res = m_shaderStore->Get(handle))
    {
        res->passType         = key.pass;
        res->variantFeatures  = key.features;
        res->supportsSkinning = skinned;
        res->usesVertexColor  = vertexColor;
    }

    m_cache.emplace(key, handle);
    return handle;
}
