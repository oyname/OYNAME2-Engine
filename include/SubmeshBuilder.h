#pragma once

#include "SubmeshData.h"

#include <stdexcept>
#include <utility>

// ---------------------------------------------------------------------------
// SubmeshBuilder
//
// Komfortabler CPU-Builder für Geometrie vor dem Upload.
// Kein Runtime-Objekt, kein ECS-State – nur Authoring/Build-Helfer, der am Ende
// reines SubmeshData ausgibt.
// ---------------------------------------------------------------------------
class SubmeshBuilder
{
public:
    SubmeshBuilder() = default;

    void Clear()
    {
        m_data = {};
        m_lastVertex = InvalidIndex();
    }

    void Reserve(uint32_t vertexCount, uint32_t indexCount = 0u)
    {
        m_data.positions.reserve(vertexCount);
        m_data.normals.reserve(vertexCount);
        m_data.uv0.reserve(vertexCount);
        m_data.uv1.reserve(vertexCount);
        m_data.tangents.reserve(vertexCount);
        m_data.colors.reserve(vertexCount);
        m_data.boneIndices.reserve(vertexCount);
        m_data.boneWeights.reserve(vertexCount);
        m_data.indices.reserve(indexCount);
    }

    uint32_t AddVertex(const Float3& position)
    {
        m_data.positions.push_back(position);
        m_lastVertex = static_cast<uint32_t>(m_data.positions.size() - 1u);
        return m_lastVertex;
    }

    uint32_t AddVertex(float x, float y, float z)
    {
        return AddVertex({ x, y, z });
    }

    uint32_t VertexCount() const noexcept
    {
        return static_cast<uint32_t>(m_data.positions.size());
    }

    uint32_t IndexCount() const noexcept
    {
        return static_cast<uint32_t>(m_data.indices.size());
    }

    void SetNormal(const Float3& n) { SetNormal(m_lastVertex, n); }
    void SetNormal(uint32_t vertexIndex, const Float3& n)
    {
        EnsureVertex(vertexIndex);
        EnsureSize(m_data.normals, m_data.positions.size(), Float3{ 0, 1, 0 });
        m_data.normals[vertexIndex] = n;
    }

    void SetUV0(const Float2& uv) { SetUV0(m_lastVertex, uv); }
    void SetUV0(uint32_t vertexIndex, const Float2& uv)
    {
        EnsureVertex(vertexIndex);
        EnsureSize(m_data.uv0, m_data.positions.size(), Float2{ 0, 0 });
        m_data.uv0[vertexIndex] = uv;
    }

    void SetUV1(const Float2& uv) { SetUV1(m_lastVertex, uv); }
    void SetUV1(uint32_t vertexIndex, const Float2& uv)
    {
        EnsureVertex(vertexIndex);
        EnsureSize(m_data.uv1, m_data.positions.size(), Float2{ 0, 0 });
        m_data.uv1[vertexIndex] = uv;
    }

    void SetColor(const Float4& color) { SetColor(m_lastVertex, color); }
    void SetColor(uint32_t vertexIndex, const Float4& color)
    {
        EnsureVertex(vertexIndex);
        EnsureSize(m_data.colors, m_data.positions.size(), Float4{ 1, 1, 1, 1 });
        m_data.colors[vertexIndex] = color;
    }

    void SetTangent(const Float4& tangent) { SetTangent(m_lastVertex, tangent); }
    void SetTangent(uint32_t vertexIndex, const Float4& tangent)
    {
        EnsureVertex(vertexIndex);
        EnsureSize(m_data.tangents, m_data.positions.size(), Float4{ 1, 0, 0, 1 });
        m_data.tangents[vertexIndex] = tangent;
    }

    void SetBoneData(const UInt4& indices, const Float4& weights)
    {
        SetBoneData(m_lastVertex, indices, weights);
    }

    void SetBoneData(uint32_t vertexIndex,
        const UInt4& indices,
        const Float4& weights)
    {
        EnsureVertex(vertexIndex);
        EnsureSize(m_data.boneIndices, m_data.positions.size(), UInt4{ 0, 0, 0, 0 });
        EnsureSize(m_data.boneWeights, m_data.positions.size(), Float4{ 1, 0, 0, 0 });
        m_data.boneIndices[vertexIndex] = indices;
        m_data.boneWeights[vertexIndex] = weights;
    }

    void AddIndex(uint32_t i)
    {
        EnsureVertex(i);
        m_data.indices.push_back(i);
    }

    void AddTriangle(uint32_t i0, uint32_t i1, uint32_t i2)
    {
        EnsureVertex(i0);
        EnsureVertex(i1);
        EnsureVertex(i2);
        m_data.indices.push_back(i0);
        m_data.indices.push_back(i1);
        m_data.indices.push_back(i2);
    }

    void AddQuad(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3)
    {
        AddTriangle(i0, i1, i2);
        AddTriangle(i2, i1, i3);
    }

    const SubmeshData& Peek() const noexcept { return m_data; }

    SubmeshData Build() const
    {
        return m_data;
    }

    SubmeshData MoveBuild()
    {
        SubmeshData out = std::move(m_data);
        Clear();
        return out;
    }

private:
    static constexpr uint32_t InvalidIndex() noexcept { return 0xFFFFFFFFu; }

    void EnsureVertex(uint32_t index) const
    {
        if (index == InvalidIndex() || index >= m_data.positions.size())
            throw std::out_of_range("SubmeshBuilder: vertex index out of range");
    }

    template<typename T>
    static void EnsureSize(std::vector<T>& v, size_t size, const T& fillValue)
    {
        if (v.size() < size)
            v.resize(size, fillValue);
    }

    SubmeshData m_data;
    uint32_t m_lastVertex = InvalidIndex();
};
