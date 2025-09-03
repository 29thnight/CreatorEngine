#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "TerrainBuffers.h"
#include "Mesh.h"
#include "DeviceState.h"
#include "DirectXHelper.h"
#include "Shader.h"

////-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// TerrainMesh: 한 번 생성 후,
// 내부에 UpdateVertexBufferPatch()를 추가해 “부분 업데이트”가 가능하도록 수정
//-----------------------------------------------------------------------------
class TerrainMesh 
{
public:
    // meshWidth: 버텍스가 m_width × m_height로 들어왔다고 가정
    TerrainMesh(std::string_view name, const std::vector<Vertex>& vertices, const std::vector<uint32>& indices, uint32_t meshWidth)
        : m_name(name), m_vertices(vertices), m_indices(indices), m_meshWidth(meshWidth)
    {
        D3D11_BUFFER_DESC vbDesc = {};

#ifndef BUILD_FLAG
        // ★ 버텍스 버퍼는 DYNAMIC + WRITE_DISCARD로 생성
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.ByteWidth = sizeof(Vertex) * (UINT)m_vertices.size();
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        vbDesc.MiscFlags = 0;
        vbDesc.StructureByteStride = 0;

        D3D11_SUBRESOURCE_DATA vbInit = {};
        vbInit.pSysMem = m_vertices.data();
#else
        //build 된 상태에서는 버텍스 버퍼를 IMMUTABLE로 생성
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.ByteWidth = sizeof(Vertex) * (UINT)m_vertices.size();
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = 0;
        vbDesc.MiscFlags = 0;
        vbDesc.StructureByteStride = 0;
        D3D11_SUBRESOURCE_DATA vbInit = {};
        vbInit.pSysMem = m_vertices.data();
#endif // !BUILD_FLAG

        DirectX11::ThrowIfFailed(
            DirectX11::DeviceStates->g_pDevice->CreateBuffer(&vbDesc, &vbInit, m_vertexBuffer.GetAddressOf())
        );
        //DirectX::SetName(m_vertexBuffer.Get(), m_name + "VertexBuffer");

        // 인덱스 버퍼는 변하지 않으므로 IMMUTABLE로 생성
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.ByteWidth = sizeof(uint32) * (UINT)m_indices.size();
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA ibInit = {};
        ibInit.pSysMem = m_indices.data();

        DirectX11::DeviceStates->g_pDevice->CreateBuffer(&ibDesc, &ibInit, m_indexBuffer.GetAddressOf());
        //DirectX::SetName(m_indexBuffer.Get(), m_name + "IndexBuffer");
    }

    ~TerrainMesh() = default;

    void Draw() 
    {
        UINT offset = 0;
        DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
        DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        DirectX11::DrawIndexed((UINT)m_indices.size(), 0, 0);
    }

    void Draw(ID3D11DeviceContext* _deferredContext) 
    {
        UINT offset = 0;
        DirectX11::IASetVertexBuffers(_deferredContext, 0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
        DirectX11::IASetIndexBuffer(_deferredContext, m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        DirectX11::IASetPrimitiveTopology(_deferredContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        DirectX11::DrawIndexed(_deferredContext, m_indices.size(), 0, 0);
    }

    std::string GetName() const { return m_name; }
    const std::vector<Vertex>& GetVertices() { return m_vertices; }
    const std::vector<uint32>& GetIndices() { return m_indices; }

    // 빌드 모드가 아닐 때만 사용
    // 전체 버텍스 업데이트
    void UpdateVertexBuffer(const Vertex* srcVertices, uint32_t vertexCount)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(
            m_vertexBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped
        );
        if (SUCCEEDED(hr))
        {
            memcpy(mapped.pData, srcVertices, sizeof(Vertex) * vertexCount);
            DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_vertexBuffer.Get(), 0);
        }
    }

    // 패치(사각형 영역) 단위로 버퍼 업데이트
    void UpdateVertexBufferPatch(const Vertex* src, uint32_t offsetX, uint32_t offsetY, uint32_t patchW, uint32_t patchH)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        auto context = DirectX11::DeviceStates->g_pDeviceContext;

        HRESULT hr = context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(hr))
        {
            assert(false && "Map failed");
            return;
        }

        // 전체 버퍼 포인터
        Vertex* dst = reinterpret_cast<Vertex*>(mapped.pData);

        for (uint32_t y = 0; y < patchH; ++y)
        {
            uint32_t dstIndex = (offsetY + y) * m_meshWidth + offsetX;
            uint32_t srcIndex = y * patchW;

            memcpy(&dst[dstIndex], &src[srcIndex], sizeof(Vertex) * patchW);
        }

        context->Unmap(m_vertexBuffer.Get(), 0);
    }

private:
    std::string m_name;
    std::vector<Vertex> m_vertices;
    std::vector<uint32> m_indices;
    uint32_t m_meshWidth;    // (m_width) × (m_height) 형태일 때, 가로 크기

    DirectX::BoundingBox m_boundingBox;
    DirectX::BoundingSphere m_boundingSphere;

    ComPtr<ID3D11Buffer> m_vertexBuffer{};
    ComPtr<ID3D11Buffer> m_indexBuffer{};
    static constexpr uint32 m_stride = sizeof(Vertex);
};
