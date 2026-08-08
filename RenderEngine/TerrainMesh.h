#pragma once
#include "Mesh.h"

//-----------------------------------------------------------------------------
// TerrainMesh: 격자 지형의 메시 자료
//
// ★ DX11 정점·인덱스 버퍼를 걷어냈다(PHASE 11 착수, 2026-08-08).
//
//   그와 함께 T5가 적어 둔 선결 결함이 풀린다: UpdateVertexBufferPatch가
//   DX11 버퍼만 Map/memcpy 하고 m_vertices(CPU 배열)는 그대로 두고 있었다.
//   즉 조각(sculpt) 결과가 CPU 자료에 남지 않았고, DX12는 CPU 배열에서
//   업로드하므로(DX12MeshCache의 규약) 그 결과가 화면에 반영될 길이
//   없었다. 게다가 그 버퍼를 읽는 코드는 이미 0이었다 — 아무도 읽지 않는
//   버퍼에 계속 쓰고 있었던 셈이다.
//
//   이제 CPU 배열이 진실이다. 갱신은 전부 여기에 쓰고, GPU 반영은 새로 쓸
//   지형 패스가 m_revision을 보고 판단한다.
//-----------------------------------------------------------------------------
class TerrainMesh
{
public:
    // meshWidth: 정점 격자의 가로 폭(m_width). 패치 갱신의 행 계산에 쓴다.
    TerrainMesh(std::string_view name, const std::vector<Vertex>& vertices,
                const std::vector<uint32>& indices, uint32_t meshWidth)
        : m_name(name), m_vertices(vertices), m_indices(indices), m_meshWidth(meshWidth)
    {
    }

    ~TerrainMesh() = default;

    std::string GetName() const { return m_name; }
    const std::vector<Vertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32>& GetIndices() const { return m_indices; }
    uint32_t GetMeshWidth() const { return m_meshWidth; }

    /// 자료가 바뀔 때마다 오른다. 지형 패스가 "올린 것이 최신인가"를 이 값으로
    /// 판정한다 — 정점 배열 전체를 매 프레임 비교할 이유가 없다.
    uint32_t GetRevision() const { return m_revision; }

    /// 정점 전체 갱신.
    void UpdateVertexBuffer(const Vertex* srcVertices, uint32_t vertexCount)
    {
        if (nullptr == srcVertices || 0 == vertexCount) return;

        const size_t count = std::min<size_t>(vertexCount, m_vertices.size());
        std::copy_n(srcVertices, count, m_vertices.begin());
        ++m_revision;
    }

    /// 패치(직사각 영역) 단위 갱신 — 조각·페인팅이 부르는 자리.
    void UpdateVertexBufferPatch(const Vertex* src, uint32_t offsetX, uint32_t offsetY,
                                 uint32_t patchW, uint32_t patchH)
    {
        if (nullptr == src || 0 == patchW || 0 == patchH) return;
        if (0 == m_meshWidth) return;

        for (uint32_t y = 0; y < patchH; ++y)
        {
            const size_t dstIndex = static_cast<size_t>(offsetY + y) * m_meshWidth + offsetX;
            const size_t srcIndex = static_cast<size_t>(y) * patchW;

            // 예전 DX11 경로는 Map한 버퍼에 범위 검사 없이 memcpy 했다.
            // 격자 밖으로 나가는 행은 건너뛴다.
            if (dstIndex >= m_vertices.size()) break;
            const size_t count = std::min<size_t>(patchW, m_vertices.size() - dstIndex);
            std::copy_n(src + srcIndex, count, m_vertices.begin() + dstIndex);
        }

        ++m_revision;
    }

private:
    std::string m_name;
    std::vector<Vertex> m_vertices;
    std::vector<uint32> m_indices;
    uint32_t m_meshWidth;    // 격자 가로 폭

    uint32_t m_revision{ 0 };
};
