#include "NeonTimeBuffer.h"
#include "MaterialResource.h"
#include "Core/Debug.h"

bool NeonTimeBuffer::Initialize(GDXECSRenderer& renderer, MaterialHandle material)
{
    m_renderer = &renderer;
    m_material = material;
    m_elapsedTime = 0.0f;

    auto* mat = m_renderer->GetMatStore().Get(m_material);
    if (!mat)
    {
        Debug::LogError("NeonTimeBuffer.cpp: Initialize - invalid material handle");
        m_renderer = nullptr;
        m_material = MaterialHandle::Invalid();
        return false;
    }

    mat->data.uvTilingOffset.x = 0.0f; // Zeitkanal
    mat->cpuDirty = true;
    return true;
}

void NeonTimeBuffer::Update(float deltaTime)
{
    if (!m_renderer || !m_material.IsValid())
        return;

    m_elapsedTime += deltaTime;

    auto* mat = m_renderer->GetMatStore().Get(m_material);
    if (!mat)
        return;

    mat->data.uvTilingOffset.x = m_elapsedTime;
    mat->cpuDirty = true;
}

void NeonTimeBuffer::Bind()
{
    // In GIDX unnoetig.
    // Die Zeit steckt bereits im Material-CBuffer (b2).
}

void NeonTimeBuffer::Shutdown()
{
    m_renderer = nullptr;
    m_material = MaterialHandle::Invalid();
    m_elapsedTime = 0.0f;
}
