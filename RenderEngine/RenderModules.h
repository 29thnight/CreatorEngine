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

// 클리핑 파라미터 구조체
struct ClippingParams
{
    float clippingProgress;
    Mathf::Vector3 clippingAxis;
    Mathf::Matrix invWorldMatrix;
    float clippingEnabled;
    Mathf::Vector3 pad;
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

    // 클리핑 기능이 필요한 자식 클래스에서만 오버라이드
    virtual bool SupportsClipping() const { return false; }

    // 클리핑 지원 여부 확인 후 사용
    void EnableClipping(bool enable);
    void SetClippingProgress(float progress);
    void SetClippingAxis(const Mathf::Vector3& axis);
    void SetClippingBounds(const Mathf::Vector3& min, const Mathf::Vector3& max);

    bool IsClippingEnabled() const { return m_clippingEnabled && SupportsClipping(); }
    float GetClippingProgress() const { return m_clippingParams.clippingProgress; }
    const ClippingParams& GetClippingParams() const { return m_clippingParams; }

    Texture* GetAssignedTexture() const { return m_assignedTexture; }

protected:
    std::unique_ptr<PipelineStateObject> m_pso;

    // 클리핑 관련 데이터를 부모에서 관리
    ClippingParams m_clippingParams = {};
    bool m_clippingEnabled = false;

    // 클리핑 상수 버퍼 (필요한 자식 클래스에서만 생성)
    virtual void OnClippingStateChanged() {}
    virtual void CreateClippingBuffer() {}
    virtual void UpdateClippingBuffer() {}

    Texture* m_assignedTexture;
private:
    ID3D11DepthStencilState* m_prevDepthState = nullptr;
    UINT m_prevStencilRef = 0;
    ID3D11BlendState* m_prevBlendState = nullptr;
    float m_prevBlendFactor[4] = { 0 };
    UINT m_prevSampleMask = 0;
    ID3D11RasterizerState* m_prevRasterizerState = nullptr;

};