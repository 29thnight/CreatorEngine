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
    void Release() override;
    void CreateBillboard();
    void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) override;

    void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount) override;
    void SetupRenderTarget(RenderPassData* renderData) override;
    void SetTexture(Texture* texture) override;

    virtual void ResetForReuse() override;
    virtual bool IsReadyForReuse() const override;
    virtual void WaitForGPUCompletion() override;

    BillBoardType GetBillboardType() const { return m_BillBoardType; }
    PipelineStateObject* GetPSO() { return m_pso.get(); }

    void BindResource() override;

    void SetBillboardType(BillBoardType type) { m_BillBoardType = type; }

    // ISerializable �������̽� ����
    virtual nlohmann::json SerializeData() const override;

    virtual void DeserializeData(const nlohmann::json& json) override;

    virtual std::string GetModuleType() const override;

    // �߰� Getter �޼ҵ�� (JSON ����ȭ��)
    UINT GetMaxCount() const { return m_maxCount; }
    const std::vector<BillboardVertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32>& GetIndices() const { return m_indices; }

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


};

