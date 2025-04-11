#pragma once
#include "Core.Minimal.h"
#include "PSO.h"
#include "DeviceState.h"

enum class BillBoardType
{
    Basic,
    YAxs,
    AxisAligned
};

struct BillboardVertex
{
    Mathf::Vector4 position;
};

struct BillBoardInstanceData
{
    Mathf::Vector4 position;
    Mathf::Vector2 size;
    UINT id;
    Mathf::Vector4 color;
};

struct ModelConstantBuffer
{
    Mathf::Matrix world;
    Mathf::Matrix view;
    Mathf::Matrix projection;
};

class RenderModules
{
public:
    virtual void Initialize() {}
    virtual void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) {}
    void movePSO(std::unique_ptr<PipelineStateObject> pso) { m_pso.swap(pso); }
    PipelineStateObject* GetPSO() { return m_pso.get(); }
    virtual void SetupInstancing(void* instanceData, UINT count) = 0;

    void CleanupRenderState();
    void SaveRenderState();
    void RestoreRenderState();

protected:
    std::unique_ptr<PipelineStateObject> m_pso;

private:
    ID3D11DepthStencilState* m_prevDepthState = nullptr;
    UINT m_prevStencilRef = 0;
    ID3D11BlendState* m_prevBlendState = nullptr;
    float m_prevBlendFactor[4] = { 0 };
    UINT m_prevSampleMask = 0;
    ID3D11RasterizerState* m_prevRasterizerState = nullptr;

};


class BillboardModule : public RenderModules
{
public:
    

public:

    void Initialize() override;
    void CreateBillboard(BillboardVertex* vertex, BillBoardType type);
    void SetupInstancing(BillBoardInstanceData* instance, UINT count);
    void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) override;

    BillBoardType GetBillboardType() const { return m_BillBoardType; }
    PipelineStateObject* GetPSO() { return m_pso.get(); }

    void SetBillboardType(BillBoardType type) { m_BillBoardType = type; }

    virtual void SetupInstancing(void* instanceData, UINT count) override {
        SetupInstancing(static_cast<BillBoardInstanceData*>(instanceData), count);
    }

private:
    BillBoardType m_BillBoardType;
    UINT m_instanceCount;

    BillboardVertex* mVertex;

    Microsoft::WRL::ComPtr<ID3D11Buffer> billboardVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_InstanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ModelBuffer;

    ModelConstantBuffer m_ModelConstantBuffer;
};