#pragma once
#include "Core.Minimal.h"
#include "PSO.h"
#include "DeviceState.h"
#include "GameObject.h"
#include "RenderPassData.h"

class ParticleSystem;
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

struct PolarClippingParams
{
    float polarClippingEnabled = 0.0f;    // 극좌표 클리핑 활성화 여부
    float polarAngleProgress = 0.0f;      // 0~1: 각도 진행도
    float polarStartAngle = 0.0f;         // 시작 각도 (라디안)
    float polarDirection = 1.0f;          // 1: 시계방향, -1: 반시계방향

    Mathf::Vector3 polarCenter = Mathf::Vector3::Zero;    // 극좌표 중심점 (월드 좌표)
    float pad2 = 0.0f;

    Mathf::Vector3 polarUpAxis = Mathf::Vector3::Up;      // 극좌표 위쪽 축
    float pad3 = 0.0f;

    Mathf::Vector3 polarReferenceDir = Mathf::Vector3::Zero;
    float pad4;

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

    virtual void ResetForReuse() {}
    virtual bool IsReadyForReuse() const { return true; }
    virtual void WaitForGPUCompletion() {}

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

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

    bool IsClippingEnabled() const { return m_clippingEnabled && SupportsClipping(); }
    float GetClippingProgress() const { return m_clippingParams.polarAngleProgress; }
    const PolarClippingParams& GetClippingParams() const { return m_clippingParams; }

    Texture* GetAssignedTexture() const { return m_assignedTexture; }

    void SetEffectProgress(float progress) {
        m_effectProgress = progress;
        m_useEffectProgress = true;
    }

    void SetUseEffectProgress(bool use) { m_useEffectProgress = use; }

    // Owner 시스템 설정 및 접근
    void SetOwnerSystem(ParticleSystem* owner) { m_ownerSystem = owner; }
    ParticleSystem* GetOwnerSystem() const { return m_ownerSystem; }

    // Owner 시스템이 있는지 확인
    bool HasOwnerSystem() const { return m_ownerSystem != nullptr; }

    bool IsSystemRunning() const;


protected:
    ParticleSystem* m_ownerSystem;

    std::unique_ptr<PipelineStateObject> m_pso;

    // 클리핑 관련 데이터를 부모에서 관리
    PolarClippingParams m_clippingParams = {};
    bool m_clippingEnabled = false;

    // 클리핑 상수 버퍼 (필요한 자식 클래스에서만 생성)
    virtual void OnClippingStateChanged() {}
    virtual void CreateClippingBuffer() {}
    virtual void UpdateClippingBuffer() {}

    Texture* m_assignedTexture;

    float m_effectProgress = 0.0f;
    bool m_useEffectProgress = false;

    bool m_enabled = true;

    bool m_isRendering = false;
    mutable std::atomic<bool> m_gpuWorkPending = false;

private:
    ID3D11DepthStencilState* m_prevDepthState = nullptr;
    UINT m_prevStencilRef = 0;
    ID3D11BlendState* m_prevBlendState = nullptr;
    float m_prevBlendFactor[4] = { 0 };
    UINT m_prevSampleMask = 0;
    ID3D11RasterizerState* m_prevRasterizerState = nullptr;

};