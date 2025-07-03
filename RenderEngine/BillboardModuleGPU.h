#pragma once
#include "RenderModules.h"
#include "ISerializable.h"

class BillboardModuleGPU : public RenderModules, public ISerializable
{
public:
    ID3D11ShaderResourceView* m_particleSRV;
    UINT m_instanceCount;
public:

    void Initialize() override;
    void CreateBillboard();
    void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) override;

    void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount) override;
    

    BillBoardType GetBillboardType() const { return m_BillBoardType; }
    PipelineStateObject* GetPSO() { return m_pso.get(); }

    void SetTexture(Texture* texture) override;

    void SetupRenderTarget(RenderPassData* renderData) override;

    void BindResource() override;

    void SetBillboardType(BillBoardType type) { m_BillBoardType = type; }

    // ISerializable 인터페이스 구현
    virtual nlohmann::json SerializeData() const override
    {
        nlohmann::json json;

        // 빌보드 설정
        json["billboard"] = {
            {"type", static_cast<int>(m_BillBoardType)},
            {"maxCount", m_maxCount}
        };

        // 텍스처 정보 (텍스처 파일 경로나 이름 저장)
        json["texture"] = {
            {"hasTexture", m_assignedTexture != nullptr}
        };

        if (m_assignedTexture)
        {
            // 텍스처 파일 경로나 이름을 저장 (Texture 클래스에 GetPath() 같은 메소드가 있다고 가정)
            // json["texture"]["path"] = m_assignedTexture->GetPath();
            // 또는 텍스처 ID나 이름
            // json["texture"]["name"] = m_assignedTexture->GetName();

            // 현재는 텍스처가 할당되어 있다는 정보만 저장
            json["texture"]["assigned"] = true;
        }

        // 정점 데이터 (커스텀 정점이 있는 경우)
        if (!m_vertices.empty())
        {
            json["vertices"] = nlohmann::json::array();
            for (const auto& vertex : m_vertices)
            {
                json["vertices"].push_back({
                    {"position", {
                        {"x", vertex.position.x},
                        {"y", vertex.position.y},
                        {"z", vertex.position.z},
                        {"w", vertex.position.w}
                    }},
                    {"texcoord", {
                        {"x", vertex.texcoord.x},
                        {"y", vertex.texcoord.y}
                    }}
                    });
            }
        }

        // 인덱스 데이터 (커스텀 인덱스가 있는 경우)
        if (!m_indices.empty())
        {
            json["indices"] = m_indices;
        }

        return json;
    }

    virtual void DeserializeData(const nlohmann::json& json) override
    {
        // 빌보드 설정 복원
        if (json.contains("billboard"))
        {
            const auto& billboardJson = json["billboard"];

            if (billboardJson.contains("type"))
            {
                m_BillBoardType = static_cast<BillBoardType>(billboardJson["type"]);
            }

            if (billboardJson.contains("maxCount"))
            {
                m_maxCount = billboardJson["maxCount"];
            }
        }

        // 텍스처 정보 복원
        if (json.contains("texture"))
        {
            const auto& textureJson = json["texture"];

            // 텍스처 로드는 별도로 처리해야 함
            // 텍스처 매니저를 통해 로드하거나, 경로를 저장해두었다가 나중에 로드
            if (textureJson.contains("path"))
            {
                // std::string texturePath = textureJson["path"];
                // m_assignedTexture = TextureManager::LoadTexture(texturePath);
            }
        }

        // 정점 데이터 복원
        if (json.contains("vertices"))
        {
            m_vertices.clear();
            for (const auto& vertexJson : json["vertices"])
            {
                BillboardVertex vertex;

                if (vertexJson.contains("position"))
                {
                    const auto& posJson = vertexJson["position"];
                    vertex.position.x = posJson.value("x", 0.0f);
                    vertex.position.y = posJson.value("y", 0.0f);
                    vertex.position.z = posJson.value("z", 0.0f);
                    vertex.position.w = posJson.value("w", 1.0f);
                }

                if (vertexJson.contains("texcoord"))
                {
                    const auto& texJson = vertexJson["texcoord"];
                    vertex.texcoord.x = texJson.value("x", 0.0f);
                    vertex.texcoord.y = texJson.value("y", 0.0f);
                }

                m_vertices.push_back(vertex);
            }
        }

        // 인덱스 데이터 복원
        if (json.contains("indices"))
        {
            m_indices = json["indices"].get<std::vector<uint32>>();
        }

        // 복원 후 리소스 재생성 필요
        // Initialize()를 다시 호출하거나 별도 메소드로 GPU 리소스 재생성
    }

    virtual std::string GetModuleType() const override
    {
        return "BillboardModuleGPU";
    }

    // 추가 Getter 메소드들 (JSON 직렬화용)
    UINT GetMaxCount() const { return m_maxCount; }
    const std::vector<BillboardVertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32>& GetIndices() const { return m_indices; }
    Texture* GetAssignedTexture() const { return m_assignedTexture; }

    // 복원 후 리소스 재생성을 위한 메소드
    void RecreateResources()
    {
        // GPU 리소스들을 다시 생성
        CreateBillboard();
        // 필요하다면 Initialize() 호출
    }


private:
    BillBoardType m_BillBoardType;
    UINT m_maxCount;
    BillboardVertex* mVertex;

    Microsoft::WRL::ComPtr<ID3D11Buffer> billboardVertexBuffer;
    ComPtr<ID3D11Buffer> billboardIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_InstanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ModelBuffer;

    ModelConstantBuffer m_ModelConstantBuffer;

    std::vector<BillboardVertex> Quad
    {
        { { -1.0f, 1.0f, 0.0f, 1.0f}, { 0.0f, 0.0f } },  // 좌상단
        { { 1.0f,  1.0f, 0.0f, 1.0f}, { 1.0f, 0.0f} },   // 우상단
        { { 1.0f, -1.0f, 0.0f, 1.0f}, { 1.0f, 1.0f} },   // 우하단
        { {-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },    // 좌하단

    };
    std::vector<uint32> Indices = { 0, 1, 2, 0, 2, 3 };

    std::vector<BillboardVertex> m_vertices;
    std::vector<uint32> m_indices;

    Texture* m_assignedTexture;
};

