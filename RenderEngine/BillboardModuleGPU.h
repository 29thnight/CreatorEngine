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

    // ISerializable �������̽� ����
    virtual nlohmann::json SerializeData() const override
    {
        nlohmann::json json;

        // ������ ����
        json["billboard"] = {
            {"type", static_cast<int>(m_BillBoardType)},
            {"maxCount", m_maxCount}
        };

        // �ؽ�ó ���� (�ؽ�ó ���� ��γ� �̸� ����)
        json["texture"] = {
            {"hasTexture", m_assignedTexture != nullptr}
        };

        if (m_assignedTexture)
        {
            // �ؽ�ó ���� ��γ� �̸��� ���� (Texture Ŭ������ GetPath() ���� �޼ҵ尡 �ִٰ� ����)
            // json["texture"]["path"] = m_assignedTexture->GetPath();
            // �Ǵ� �ؽ�ó ID�� �̸�
            // json["texture"]["name"] = m_assignedTexture->GetName();

            // ����� �ؽ�ó�� �Ҵ�Ǿ� �ִٴ� ������ ����
            json["texture"]["assigned"] = true;
        }

        // ���� ������ (Ŀ���� ������ �ִ� ���)
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

        // �ε��� ������ (Ŀ���� �ε����� �ִ� ���)
        if (!m_indices.empty())
        {
            json["indices"] = m_indices;
        }

        return json;
    }

    virtual void DeserializeData(const nlohmann::json& json) override
    {
        // ������ ���� ����
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

        // �ؽ�ó ���� ����
        if (json.contains("texture"))
        {
            const auto& textureJson = json["texture"];

            // �ؽ�ó �ε�� ������ ó���ؾ� ��
            // �ؽ�ó �Ŵ����� ���� �ε��ϰų�, ��θ� �����صξ��ٰ� ���߿� �ε�
            if (textureJson.contains("path"))
            {
                // std::string texturePath = textureJson["path"];
                // m_assignedTexture = TextureManager::LoadTexture(texturePath);
            }
        }

        // ���� ������ ����
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

        // �ε��� ������ ����
        if (json.contains("indices"))
        {
            m_indices = json["indices"].get<std::vector<uint32>>();
        }

        // ���� �� ���ҽ� ����� �ʿ�
        // Initialize()�� �ٽ� ȣ���ϰų� ���� �޼ҵ�� GPU ���ҽ� �����
    }

    virtual std::string GetModuleType() const override
    {
        return "BillboardModuleGPU";
    }

    // �߰� Getter �޼ҵ�� (JSON ����ȭ��)
    UINT GetMaxCount() const { return m_maxCount; }
    const std::vector<BillboardVertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32>& GetIndices() const { return m_indices; }
    Texture* GetAssignedTexture() const { return m_assignedTexture; }

    // ���� �� ���ҽ� ������� ���� �޼ҵ�
    void RecreateResources()
    {
        // GPU ���ҽ����� �ٽ� ����
        CreateBillboard();
        // �ʿ��ϴٸ� Initialize() ȣ��
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
        { { -1.0f, 1.0f, 0.0f, 1.0f}, { 0.0f, 0.0f } },  // �»��
        { { 1.0f,  1.0f, 0.0f, 1.0f}, { 1.0f, 0.0f} },   // ����
        { { 1.0f, -1.0f, 0.0f, 1.0f}, { 1.0f, 1.0f} },   // ���ϴ�
        { {-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },    // ���ϴ�

    };
    std::vector<uint32> Indices = { 0, 1, 2, 0, 2, 3 };

    std::vector<BillboardVertex> m_vertices;
    std::vector<uint32> m_indices;

    Texture* m_assignedTexture;
};

