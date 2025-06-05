#pragma once
#include "Component.h"
#include "ResourceAllocator.h"
#include "TerrainCollider.h"
#include "TerrainComponent.generated.h"
#include "DeviceState.h"
#include "Mesh.h" // Vertex ���� ����


//-----------------------------------------------------------------------------
#include "DirectXHelper.h"



////-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// TerrainMesh: �� �� ���� ��,
// ���ο� UpdateVertexBufferPatch()�� �߰��� ���κ� ������Ʈ���� �����ϵ��� ����
//-----------------------------------------------------------------------------
class TerrainMesh {
public:
    // meshWidth: ���ؽ��� m_width �� m_height�� ���Դٰ� ����
    TerrainMesh(const std::string_view& name, const std::vector<Vertex>& vertices, const std::vector<uint32>& indices, uint32_t meshWidth)
        : m_name(name), m_vertices(vertices), m_indices(indices), m_meshWidth(meshWidth)
    {
        // �� ���ؽ� ���۴� DYNAMIC + WRITE_DISCARD�� ����
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.ByteWidth = sizeof(Vertex) * (UINT)m_vertices.size();
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        vbDesc.MiscFlags = 0;
        vbDesc.StructureByteStride = 0;

        D3D11_SUBRESOURCE_DATA vbInit = {};
        vbInit.pSysMem = m_vertices.data();

        DirectX11::ThrowIfFailed(
            DeviceState::g_pDevice->CreateBuffer(&vbDesc, &vbInit, m_vertexBuffer.GetAddressOf())
        );
        DirectX::SetName(m_vertexBuffer.Get(), m_name + "VertexBuffer");

        // �ε��� ���۴� ������ �����Ƿ� IMMUTABLE�� ����
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.ByteWidth = sizeof(uint32) * (UINT)m_indices.size();
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA ibInit = {};
        ibInit.pSysMem = m_indices.data();

        DeviceState::g_pDevice->CreateBuffer(&ibDesc, &ibInit, m_indexBuffer.GetAddressOf());
        DirectX::SetName(m_indexBuffer.Get(), m_name + "IndexBuffer");
    }

    ~TerrainMesh() = default;

    void Draw() {
        UINT offset = 0;
        DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
        DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        DirectX11::DrawIndexed((UINT)m_indices.size(), 0, 0);
    }

    ID3D11CommandList* Draw(ID3D11DeviceContext* _deferredContext) {
        UINT offset = 0;
        _deferredContext->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
        _deferredContext->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        _deferredContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        _deferredContext->DrawIndexed((UINT)m_indices.size(), 0, 0);

        ID3D11CommandList* commandList;
        _deferredContext->FinishCommandList(false, &commandList);
        return commandList;
    }

    std::string GetName() const { return m_name; }
    const std::vector<Vertex>& GetVertices() { return m_vertices; }
    const std::vector<uint32>& GetIndices() { return m_indices; }

    // ��ü ���ؽ� ������Ʈ
    void UpdateVertexBuffer(const Vertex* srcVertices, uint32_t vertexCount)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = DeviceState::g_pDeviceContext->Map(
            m_vertexBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped
        );
        if (SUCCEEDED(hr))
        {
            memcpy(mapped.pData, srcVertices, sizeof(Vertex) * vertexCount);
            DeviceState::g_pDeviceContext->Unmap(m_vertexBuffer.Get(), 0);
        }
    }

    // ��ġ(�簢�� ����) ������ ���� ������Ʈ
    void UpdateVertexBufferPatch(const Vertex* src, uint32_t offsetX, uint32_t offsetY, uint32_t patchW, uint32_t patchH)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        auto context = DeviceState::g_pDeviceContext;

        HRESULT hr = context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(hr))
        {
            assert(false && "Map failed");
            return;
        }

        // ��ü ���� ������
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
    uint32_t m_meshWidth;    // (m_width) �� (m_height) ������ ��, ���� ũ��

    DirectX::BoundingBox m_boundingBox;
    DirectX::BoundingSphere m_boundingSphere;

    ComPtr<ID3D11Buffer> m_vertexBuffer{};
    ComPtr<ID3D11Buffer> m_indexBuffer{};
    static constexpr uint32 m_stride = sizeof(Vertex);
};

//-----------------------------------------------------------------------------
// TerrainBrush / TerrainLayer ���� (���� ����)
//-----------------------------------------------------------------------------
struct TerrainBrush
{
    enum class Mode { Raise, Lower, Flatten, PaintLayer } m_mode;
    DirectX::XMFLOAT2 m_center;
    float m_radius{ 1.0f };
    float m_strength{ 1.0f };
    float m_flatTargetHeight{ 0.0f };
    uint32_t m_layerID{ 0 };

    void SetBrushMode(Mode mode) { m_mode = mode; }
};

struct TerrainLayer
{
    uint32_t m_layerID{ 0 };
    ID3D11ShaderResourceView* diffuseTexture{ nullptr };
    ID3D11ShaderResourceView* normalTexture{ nullptr };
    float fileFactor{ 1.0f };
};

struct LayerDesc
{
	uint32_t layerID{ 0 };
	std::wstring diffuseTexturePath;
	std::wstring normalTexturePath;
    float tilling;
};

//-----------------------------------------------------------------------------
// TerrainComponent: ApplyBrush ����ȭ ����
//-----------------------------------------------------------------------------
class TerrainComponent : public Component
{
public:
    ReflectTerrainComponent
        [[Serializable(Inheritance:Component)]]
    TerrainComponent() {
        m_name = "TerrainComponent";
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<TerrainComponent>();
        Initialize();
    }
    virtual ~TerrainComponent() = default;

    [[Property]] 
    int m_width{ 1000 };
    [[Property]] 
    int m_height{ 1000 };

    // �ʱ�ȭ
    void Initialize()
    {
        m_heightMap.assign(m_width * m_height, 0.0f);
        m_vNormalMap.assign(m_width * m_height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });

        // ���̾� �ʱ�ȭ
        m_layers.clear();
        m_layerHeightMap.clear();

        // �� ���� �ʱ� �޽� ����
        std::vector<Vertex> verts(m_width * m_height);
        for (int i = 0; i < m_height; ++i)
        {
            for (int j = 0; j < m_width; ++j)
            {
                int idx = i * m_width + j;
                verts[idx] = Vertex(
                    // ��ġ(x, ����, z)
                    DirectX::XMFLOAT3((float)j, m_heightMap[idx], (float)i),
                    // �븻
                    m_vNormalMap[idx],
                    // UV0
                    DirectX::XMFLOAT2((float)j / (float)m_width, (float)i / (float)m_height)
                );
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve((m_width - 1) * (m_height - 1) * 6);

        for (int i = 0; i < m_height - 1; ++i)
        {
            for (int j = 0; j < m_width - 1; ++j)
            {
                uint32_t topLeft = i * m_width + j;
                uint32_t bottomLeft = (i + 1) * m_width + j;
                uint32_t topRight = i * m_width + (j + 1);
                uint32_t bottomRight = (i + 1) * m_width + (j + 1);

                // �ﰢ�� 1
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);
                // �ﰢ�� 2
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
                indices.push_back(topRight);
            }
        }

        // TerrainMesh ���� (�� ����)
        m_pMesh = new TerrainMesh(
            m_name.ToString(),
            verts,
            indices,
            (uint32_t)m_width
        );
    }

    void Resize(int newWidth, int newHeight)
    {
        // 1) �� ũ��� ���� ���� ���Ҵ�
        m_width = newWidth;
        m_height = newHeight;
        m_heightMap.assign(m_width * m_height, 0.0f);
        m_vNormalMap.assign(m_width * m_height, { 0.0f, 1.0f, 0.0f });

        // ���̾� ����ġ ���� �ٽ� �ʱ�ȭ
        for (auto& w : m_layerHeightMap)
            w.assign(m_width * m_height, 0.0f);

        // 2) ���� �޽� ����
        if (m_pMesh) {
            delete m_pMesh;
            m_pMesh = nullptr;
        }

        // 3) �޽� ����� (initMesh ���� �״�� ����)
        {
            std::vector<Vertex> verts(m_width * m_height);
            for (int i = 0; i < m_height; ++i)
            {
                for (int j = 0; j < m_width; ++j)
                {
                    int idx = i * m_width + j;
                    verts[idx] = Vertex(
                        { (float)j, m_heightMap[idx], (float)i },
                        m_vNormalMap[idx],
                        { (float)j / m_width, (float)i / m_height }
                    );
                }
            }
            std::vector<uint32_t> indices;
            indices.reserve((m_width - 1) * (m_height - 1) * 6);
            for (int i = 0; i < m_height - 1; ++i)
            {
                for (int j = 0; j < m_width - 1; ++j)
                {
                    uint32_t tl = i * m_width + j;
                    uint32_t bl = (i + 1) * m_width + j;
                    uint32_t tr = i * m_width + (j + 1);
                    uint32_t br = (i + 1) * m_width + (j + 1);
                    indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
                    indices.push_back(bl); indices.push_back(br); indices.push_back(tr);
                }
            }
            m_pMesh = new TerrainMesh(m_name.ToString(), verts, indices, (uint32_t)m_width);
        }

        // 4) ����Ʈ�� ���� �� �籸��
        //if (m_rootNode)
        //{
        //    DestroyQuadTree(m_rootNode);
        //    m_rootNode = nullptr;
        //}
        //m_rootNode = BuildQuadTree(0, 0, m_width - 1, m_height - 1);

        //// 5) ���������� �� �޽�/����Ʈ�� ���� (����: m_renderer�� �̹� ����)
        //m_renderer->ReleaseResources();      // ���� GPU ���ҽ� ����
        //m_renderer->Initialize(this);       // �� ũ��/����Ʈ���� �ٽ� �ʱ�ȭ
    }

    // �귯�� ����
    void ApplyBrush(const TerrainBrush& brush)
    {
        // 1) �귯�ð� ���� �ּ�/�ִ� X,Y ���
        int minX = std::max(0, int(brush.m_center.x - brush.m_radius));
        int maxX = std::min(m_width - 1, int(brush.m_center.x + brush.m_radius));
        int minY = std::max(0, int(brush.m_center.y - brush.m_radius));
        int maxY = std::min(m_height - 1, int(brush.m_center.y + brush.m_radius));

        // 2) ���� �� ����: �귯�� �� ���θ�
        for (int i = minY; i <= maxY; ++i)
        {
            for (int j = minX; j <= maxX; ++j)
            {
                float dx = brush.m_center.x - (float)j;
                float dy = brush.m_center.y - (float)i;
                float distSq = dx * dx + dy * dy;
                if (distSq <= brush.m_radius * brush.m_radius)
                {
                    float dist = std::sqrt(distSq);
                    float t = brush.m_strength * (1.0f - (dist / brush.m_radius));
                    int idx = i * m_width + j;

                    switch (brush.m_mode)
                    {
                    case TerrainBrush::Mode::Raise:
                        m_heightMap[idx] += t;
                        break;
                    case TerrainBrush::Mode::Lower:
                        m_heightMap[idx] -= t;
                        break;
                    case TerrainBrush::Mode::Flatten:
                        m_heightMap[idx] = brush.m_flatTargetHeight;
                        break;
                    case TerrainBrush::Mode::PaintLayer:
                        PaintLayer(brush.m_layerID, j, i, t);
                        break;
                    }
                }
            }
        }

        // 3) ��� ���� (�ٲ� ���� + �ֺ� 1�ȼ���)
        RecalculateNormalsPatch(minX, minY, maxX, maxY);

        // 4) ���ؽ� ���� �κ� ���ε�
        //    ��ġ ũ�� = (maxX-minX+1) �� (maxY-minY+1)
        int patchW = maxX - minX + 1;
        int patchH = maxY - minY + 1;
        std::vector<Vertex> patchVerts;
        patchVerts.reserve(patchW * patchH);

        for (int i = minY; i <= maxY; ++i)
        {
            for (int j = minX; j <= maxX; ++j)
            {
                int idx = i * m_width + j;
                Vertex v;
                v.position = { (float)j, m_heightMap[idx], (float)i };
                v.normal = m_vNormalMap[idx];
                v.uv0 = { (float)j / (float)m_width, (float)i / (float)m_height };
                // uv1, tangent, bitangent, boneIndices, boneWeights�� �ʿ��� �� �߰� ����
                patchVerts.push_back(v);
            }
        }

        // ���� GPU ���ۿ� ��ġ�� ���ε�
        m_pMesh->UpdateVertexBufferPatch(
            patchVerts.data(),
            (uint32_t)minX,
            (uint32_t)minY,
            (uint32_t)patchW,
            (uint32_t)patchH
        );
    }

    // ����� ����(minX..maxX, minY..maxY) �ֺ� ����� �ùٸ��� ���̵��� +1 �ȼ� Ȯ��
    void RecalculateNormalsPatch(int minX, int minY, int maxX, int maxY)
    {
        int startX = std::max(0, minX - 1);
        int endX = std::min(m_width - 1, maxX + 1);
        int startY = std::max(0, minY - 1);
        int endY = std::min(m_height - 1, maxY + 1);

        for (int i = startY; i <= endY; ++i)
        {
            for (int j = startX; j <= endX; ++j)
            {
                float heightL = (j > 0) ? m_heightMap[i * m_width + (j - 1)] : m_heightMap[i * m_width + j];
                float heightR = (j < m_width - 1) ? m_heightMap[i * m_width + (j + 1)] : m_heightMap[i * m_width + j];
                float heightD = (i > 0) ? m_heightMap[(i - 1) * m_width + j] : m_heightMap[i * m_width + j];
                float heightU = (i < m_height - 1) ? m_heightMap[(i + 1) * m_width + j] : m_heightMap[i * m_width + j];

                DirectX::XMFLOAT3 normal;
                normal.x = heightL - heightR;
                normal.y = 2.0f;
                normal.z = heightD - heightU;

                float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (len > 0.0f)
                {
                    normal.x /= len;
                    normal.y /= len;
                    normal.z /= len;
                }
                m_vNormalMap[i * m_width + j] = normal;
            }
        }
    }

    // ���̾� ������
    void PaintLayer(uint32_t layerId, int x, int y, float strength)
    {
        for (size_t i = 0; i < m_layers.size(); ++i)
        {
            if (m_layers[i].m_layerID == layerId)
            {
                int idx = y * m_width + x;
                m_layerHeightMap[i][idx] = std::clamp(m_layerHeightMap[i][idx] + strength, 0.0f, 1.0f);
                break;
            }
        }
    }

	std::vector<uint32_t> GetLayerCount() const
	{
		std::vector<uint32_t> layerIDs;
		layerIDs.reserve(m_layers.size());
		for (const auto& layer : m_layers)
			layerIDs.push_back(layer.m_layerID);
		return layerIDs;
	}


    void Save(){

    }

    void Load(){

    }

    void loatFromPng(const std::string& filepath, std::vector<std::vector<float>>& outLayerWeights, int& outWidth, int& outHeights);



    //layer ���� �Լ�
	void AddLayer(const std::wstring& diffuseFile,const std::wstring& nomalFile,float tilling) {
	
		TerrainLayer newLayer;
		newLayer.m_layerID = m_nextLayerID++;
		newLayer.fileFactor = tilling;
		// diffuseTexture �ε�
		if (!diffuseFile.empty()) {
            ID3D11ShaderResourceView* diffuseSRV = nullptr;
            if (CreateTextureFromFile(DeviceState::g_pDevice,diffuseFile,&diffuseSRV))
            {
                newLayer.diffuseTexture = diffuseSRV;
            }
		}
		// normalTexture �ε�
		if (!nomalFile.empty()) {
			ID3D11ShaderResourceView* normalSRV = nullptr;
			if (CreateTextureFromFile(DeviceState::g_pDevice, nomalFile, &normalSRV))
			{
				newLayer.normalTexture = normalSRV;
			}
		}
		m_layers.push_back(newLayer);
		
		LayerDesc desc;
		desc.layerID = newLayer.m_layerID;
        desc.diffuseTexturePath = diffuseFile;
		desc.normalTexturePath = nomalFile;
		desc.tilling = tilling;
    }


	void InitSplatMapTexture()
	{
		// ���÷��� �ؽ�ó �ʱ�ȭ
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_width;
		desc.Height = m_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 8-bit format
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DYNAMIC; // ���� ������Ʈ ����
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // CPU���� ���� ����
        desc.MiscFlags = 0;

		HRESULT hr = DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, m_splatMapTexture.GetAddressOf());
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create splat map texture");
		}

		// SRV ����
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		hr = DeviceState::g_pDevice->CreateShaderResourceView(m_splatMapTexture.Get(), &srvDesc, m_splatMapSRV.GetAddressOf());
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create splat map SRV");
		}
        
		// �ʱ� ���÷��� ������ ����
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = DeviceState::g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

        {
			BYTE* dest = reinterpret_cast<BYTE*>(mapped.pData);

			for (int y = 0; y < m_height; ++y)
			{
				BYTE* row = dest + y * mapped.RowPitch;
				memset(row, 0, m_width * 4); // RGBA 4ä���̹Ƿ� 4����Ʈ�� �ʱ�ȭ
			}
        }

		DeviceState::g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
	}

    void UpdateSplatMapPatch(int offsetX, int offsetY, int patchW, int patchH) {

		std::vector<BYTE> patchData(patchW * patchH * 4, 0); // RGBA 4ä��
		for (int y = 0; y < patchH; ++y)
		{
			for (int x = 0; x < patchW; ++x)
			{
				int gridX = offsetX + x;
				int gridY = offsetY + y;
				int idx = gridY * m_width + gridX;
				int dstOffset = (y * patchW + x) * 4; // RGBA 4ä��

				// ���̾� ����ġ ���
                for (int layerIdx = 0; layerIdx < (int)m_layers.size() && layerIdx < 4; ++layerIdx) // �ִ� 4�� ���̾ ���
                {
					float w = std::clamp(m_layerHeightMap[layerIdx][idx], 0.0f, 1.0f);
					patchData[dstOffset + layerIdx] = static_cast<BYTE>(w * 255.0f); // R, G, B, A ä�ο� ����ġ ����
                }
                
                for (int layerIdx = (int)m_layers.size(); layerIdx < 4; ++layerIdx) // ������ ä���� 0���� �ʱ�ȭ
                {
					patchData[dstOffset + layerIdx] = 0; // R, G, B, A ä�ο� 0 ����
                }
			}
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = DeviceState::g_pDeviceContext->Map(m_splatMapTexture.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to map splat map texture");
            return;
        }

		for (int row = 0; row < patchH; ++row)
		{
			BYTE* destRow = reinterpret_cast<BYTE*>(mapped.pData) + (size_t)(offsetY + row) * mapped.RowPitch + (size_t)(offsetX * 4);
			const BYTE* srcPtr = patchData.data() + (size_t)row * patchW * 4;
			memcpy(destRow, srcPtr, patchW * 4); // RGBA 4ä���̹Ƿ� 4����Ʈ�� ����
		}

		DeviceState::g_pDeviceContext->Unmap(m_splatMapTexture.Get(), 0);
    }



    // ���� �귯�� ���� ����/��ȯ
    void SetTerrainBrush(TerrainBrush* brush) { m_currentBrush = brush; }
    TerrainBrush& GetCurrentBrush() { return *m_currentBrush; }

    // Collider�� ������
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }
    float* GetHeightMap() { return m_heightMap.data(); }

    // Mesh ������
    TerrainMesh* GetMesh() const { return m_pMesh; }

private:
    unsigned int m_terrainID{ 0 };
    FileGuid m_trrainAssetGuid{};
    std::vector<float> m_heightMap;
    std::vector<DirectX::XMFLOAT3> m_vNormalMap;

	uint32_t m_nextLayerID{ 0 }; // ���� ���̾� ID
	uint32_t m_selectedLayerID{ 0xFFFFFFFF }; // ���õ� ���̾� ID (0xFFFFFFFF�� ���� �ȵ��� �ǹ�)
    std::vector<TerrainLayer>            m_layers;
	std::vector<LayerDesc>               m_layerDescs; // ���̾� ���� (ID, �ؽ�ó ��� ��)


    std::vector<std::vector<float>>      m_layerHeightMap;

	ComPtr<ID3D11Texture2D> m_splatMapTexture; // ���÷��� �ؽ�ó
	ComPtr<ID3D11ShaderResourceView> m_splatMapSRV; // ���÷��� SRV


    // ���� �޽ø� �� ����� �����ٸ�, �ʿ� �� ���� ���� ����
    TerrainMesh* m_pMesh{ nullptr };
    TerrainBrush* m_currentBrush{ nullptr };

    float m_textureWidth;
    float m_textureHeight;
};
