#pragma once
#include "Core.Minimal.h"
#include "PSO.h"
#include "DeviceState.h"
#include "GameObject.h"
#include "RenderPassData.h"

enum class BillBoardType
{
    Basic,
    YAxs,
    AxisAligned
};

struct BillboardVertex
{
    Mathf::Vector4 position;
    Mathf::Vector2 texcoord;
};

struct BillBoardInstanceData
{
    Mathf::Vector4 Position;
    Mathf::Vector2 TexCoord;
    UINT TexIndex;
    Mathf::Vector4 Color;
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
    virtual ~RenderModules() { Release(); }
    virtual void Initialize() {}
    virtual void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) {}
    virtual void Release() {}
    virtual void SetTexture(Texture* texture) {}
    virtual void BindResource() {}
    virtual void SetupRenderTarget(RenderPassData* renderData) {}

    virtual void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instancecount) = 0;

    void movePSO(std::unique_ptr<PipelineStateObject> pso) { m_pso.swap(pso); }
    PipelineStateObject* GetPSO() { return m_pso.get(); }

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