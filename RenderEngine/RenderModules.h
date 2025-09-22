#pragma once
#include "Core.Minimal.h"
#include "PSO.h"
#include "DeviceState.h"
#include "GameObject.h"
#include "RenderPassData.h"

class ParticleSystem;
enum class BillBoardType
{
    None,
    Basic,
    SpriteAnimation,
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

struct TimeParams
{
    float time;
    float3 pad1;
};

struct PolarClippingParams
{
    float polarClippingEnabled = 0.0f;    // 극좌표 클리핑 활성화 여부
    float polarAngleProgress = 0.0f;      // 0~1: 각도 진행도
    float polarStartAngle = 0.0f;         // 시작 각도 (라디안)
    float polarDirection = 1.0f;          // 1: 시계방향, -1: 반시계방향

    Mathf::Vector3 polarCenter = Mathf::Vector3::Zero;    // 극좌표 중심점 (월드 좌표)
    float pad1 = 0.0f;

    Mathf::Vector3 polarUpAxis = Mathf::Vector3::Up;      // 극좌표 위쪽 축
    float pad3 = 0.0f;

    Mathf::Vector3 polarReferenceDir = Mathf::Vector3::Zero;
    float pad4;

};

enum class BlendPreset
{
    None,           // 블렌딩 비활성화
    Alpha,          // 표준 알파 블렌딩
    Additive,       // 가산 블렌딩
    Multiply,       // 곱셈 블렌딩
    Subtractive,    // 감산 블렌딩
    Custom          // 사용자 정의
};

enum class DepthPreset
{
    None,
    Default,        // 읽기/쓰기 모두 활성화, LESS 비교
    ReadOnly,       // 읽기만 활성화, 쓰기 비활성화
    WriteOnly,      // 쓰기만 활성화, 비교 비활성화
    Disabled,       // 깊이 테스트 완전 비활성화
    Custom          // 사용자 정의
};

enum class RasterizerPreset
{
    None,
    Default,        // SOLID, BACK 컬링
    NoCull,         // SOLID, 컬링 없음
    Wireframe,      // WIREFRAME, BACK 컬링
    WireframeNoCull,// WIREFRAME, 컬링 없음
    Custom          // 사용자 정의
};

class RenderModules
{
public:
    virtual ~RenderModules() { Release(); }
    virtual void Initialize() {}
    virtual void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) {}
    virtual void Release() {}
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

    // 셰이더 관리
    void SetVertexShader(const std::string& shaderName);
    void SetPixelShader(const std::string& shaderName);
    void SetShaders(const std::string& vertexShader, const std::string& pixelShader);

    const std::string& GetVertexShaderName() const { return m_vertexShaderName; }
    const std::string& GetPixelShaderName() const { return m_pixelShaderName; }

    static std::vector<std::string> GetAvailableVertexShaders();
    static std::vector<std::string> GetAvailablePixelShaders();

    // 블렌드 상태 관리
    void SetBlendPreset(BlendPreset preset);
    void SetCustomBlendState(const D3D11_BLEND_DESC& desc);
    BlendPreset GetBlendPreset() const { return m_blendPreset; }

    // 깊이 스텐실 상태 관리
    void SetDepthPreset(DepthPreset preset);
    void SetCustomDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc);
    DepthPreset GetDepthPreset() const { return m_depthPreset; }

    // 래스터라이저 상태 관리
    void SetRasterizerPreset(RasterizerPreset preset);
    void SetCustomRasterizerState(const D3D11_RASTERIZER_DESC& desc);
    RasterizerPreset GetRasterizerPreset() const { return m_rasterizerPreset; }

    // 직렬화
    nlohmann::json SerializeRenderStates() const;
    void DeserializeRenderStates(const nlohmann::json& json);

    virtual void UpdatePSOShaders();
    virtual void UpdatePSORenderStates();
    virtual void OnShadersChanged() {}
    virtual void OnRenderStatesChanged() {}

    // 다중 텍스처 관리
    void SetTexture(int slot, Texture* texture);
    void SetTexture(Texture* texture) { SetTexture(0, texture); }
    void AddTexture(Texture* texture);
    void RemoveTexture(int slot);
    void ClearTextures();

    // 텍스처 접근
    Texture* GetTexture(int slot) const;
    size_t GetTextureCount() const { return m_textures.size(); }
    const std::vector<Texture*>& GetTextures() const { return m_textures; }

    // 자동 크기 조정
    void EnsureTextureSlots(size_t count);

    // 직렬화
    nlohmann::json SerializeTextures() const;
    void DeserializeTextures(const nlohmann::json& json);

    virtual void BindTextures();
protected:
    std::vector<Texture*> m_textures;
    // 모듈 상태
    bool m_enabled = true;
    bool m_isRendering = false;
    mutable std::atomic<bool> m_gpuWorkPending = false;

    // owner
    ParticleSystem* m_ownerSystem;

    // 텍스처
    Texture* m_assignedTexture;
    Texture* m_dissolveTexture;

    // pso
    std::unique_ptr<PipelineStateObject> m_pso;

    // 렌더 상태 생성 헬퍼 함수들
    void CreateBlendState(BlendPreset preset);
    void CreateDepthStencilState(DepthPreset preset);
    void CreateRasterizerState(RasterizerPreset preset);

    // 프리셋을 D3D11 구조체로 변환하는 함수들
    D3D11_BLEND_DESC GetBlendDesc(BlendPreset preset) const;
    D3D11_DEPTH_STENCIL_DESC GetDepthStencilDesc(DepthPreset preset) const;
    D3D11_RASTERIZER_DESC GetRasterizerDesc(RasterizerPreset preset) const;
    
    // 셰이더 설정
    std::string m_vertexShaderName = "None";
    std::string m_pixelShaderName = "None";

    // 렌더 상태 설정
    BlendPreset m_blendPreset = BlendPreset::None;
    DepthPreset m_depthPreset = DepthPreset::None;
    RasterizerPreset m_rasterizerPreset = RasterizerPreset::None;

    // 커스텀 상태 저장
    D3D11_BLEND_DESC m_customBlendDesc = {};
    D3D11_DEPTH_STENCIL_DESC m_customDepthDesc = {};
    D3D11_RASTERIZER_DESC m_customRasterizerDesc = {};

protected:
    // 안쓰지만 일단 유지
    // 클리핑 관련 데이터를 부모에서 관리
    PolarClippingParams m_clippingParams = {};
    bool m_clippingEnabled = false;

    // 클리핑 상수 버퍼 (필요한 자식 클래스에서만 생성)
    virtual void OnClippingStateChanged() {}
    virtual void CreateClippingBuffer() {}
    virtual void UpdateClippingBuffer() {}

    float m_effectProgress = 0.0f;
    bool m_useEffectProgress = false;

private:
    ID3D11DepthStencilState* m_prevDepthState = nullptr;
    UINT m_prevStencilRef = 0;
    ID3D11BlendState* m_prevBlendState = nullptr;
    float m_prevBlendFactor[4] = { 0 };
    UINT m_prevSampleMask = 0;
    ID3D11RasterizerState* m_prevRasterizerState = nullptr;

};