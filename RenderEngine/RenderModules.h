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
    float polarClippingEnabled = 0.0f;    // ����ǥ Ŭ���� Ȱ��ȭ ����
    float polarAngleProgress = 0.0f;      // 0~1: ���� ���൵
    float polarStartAngle = 0.0f;         // ���� ���� (����)
    float polarDirection = 1.0f;          // 1: �ð����, -1: �ݽð����

    Mathf::Vector3 polarCenter = Mathf::Vector3::Zero;    // ����ǥ �߽��� (���� ��ǥ)
    float pad1 = 0.0f;

    Mathf::Vector3 polarUpAxis = Mathf::Vector3::Up;      // ����ǥ ���� ��
    float pad3 = 0.0f;

    Mathf::Vector3 polarReferenceDir = Mathf::Vector3::Zero;
    float pad4;

};

enum class BlendPreset
{
    None,           // ������ ��Ȱ��ȭ
    Alpha,          // ǥ�� ���� ������
    Additive,       // ���� ������
    Multiply,       // ���� ������
    Subtractive,    // ���� ������
    Custom          // ����� ����
};

enum class DepthPreset
{
    None,
    Default,        // �б�/���� ��� Ȱ��ȭ, LESS ��
    ReadOnly,       // �б⸸ Ȱ��ȭ, ���� ��Ȱ��ȭ
    WriteOnly,      // ���⸸ Ȱ��ȭ, �� ��Ȱ��ȭ
    Disabled,       // ���� �׽�Ʈ ���� ��Ȱ��ȭ
    Custom          // ����� ����
};

enum class RasterizerPreset
{
    None,
    Default,        // SOLID, BACK �ø�
    NoCull,         // SOLID, �ø� ����
    Wireframe,      // WIREFRAME, BACK �ø�
    WireframeNoCull,// WIREFRAME, �ø� ����
    Custom          // ����� ����
};

struct SpriteAnimationBuffer
{
    uint32 frameCount;      // �� ������ ��
    float animationDuration;
    uint32 gridColumns;     // ��������Ʈ ��Ʈ ���� ũ�� - ��
    uint32 gridRows;        // ��������Ʈ ��Ʈ ���� ũ�� - ��
};

class RenderModules
{
public:
    virtual ~RenderModules()
    {
        Release();
        ReleaseSavedRenderState();
    }
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

    // Ŭ���� ����� �ʿ��� �ڽ� Ŭ���������� �������̵�
    virtual bool SupportsClipping() const { return false; }

    // Ŭ���� ���� ���� Ȯ�� �� ���
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

    // Owner �ý��� ���� �� ����
    void SetOwnerSystem(ParticleSystem* owner) { m_ownerSystem = owner; }
    ParticleSystem* GetOwnerSystem() const { return m_ownerSystem; }

    // Owner �ý����� �ִ��� Ȯ��
    bool HasOwnerSystem() const { return m_ownerSystem != nullptr; }

    bool IsSystemRunning() const;

    // ���̴� ����
    void SetVertexShader(const std::string& shaderName);
    void SetPixelShader(const std::string& shaderName);
    void SetShaders(const std::string& vertexShader, const std::string& pixelShader);

    const std::string& GetVertexShaderName() const { return m_vertexShaderName; }
    const std::string& GetPixelShaderName() const { return m_pixelShaderName; }

    static std::vector<std::string> GetAvailableVertexShaders();
    static std::vector<std::string> GetAvailablePixelShaders();

    // ������ ���� ����
    void SetBlendPreset(BlendPreset preset);
    void SetCustomBlendState(const D3D11_BLEND_DESC& desc);
    BlendPreset GetBlendPreset() const { return m_blendPreset; }

    // ���� ���ٽ� ���� ����
    void SetDepthPreset(DepthPreset preset);
    void SetCustomDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc);
    DepthPreset GetDepthPreset() const { return m_depthPreset; }

    // �����Ͷ����� ���� ����
    void SetRasterizerPreset(RasterizerPreset preset);
    void SetCustomRasterizerState(const D3D11_RASTERIZER_DESC& desc);
    RasterizerPreset GetRasterizerPreset() const { return m_rasterizerPreset; }

    // ����ȭ
    nlohmann::json SerializeRenderStates() const;
    void DeserializeRenderStates(const nlohmann::json& json);

    virtual void UpdatePSOShaders();
    virtual void UpdatePSORenderStates();
    virtual void OnShadersChanged() {}
    virtual void OnRenderStatesChanged() {}

    // ���� �ؽ�ó ����
    void SetTexture(int slot, Texture* texture);
    void SetTexture(Texture* texture) { SetTexture(0, texture); }
    void AddTexture(Texture* texture);
    void RemoveTexture(int slot);
    void ClearTextures();

    // �ؽ�ó ����
    Texture* GetTexture(int slot) const;
    size_t GetTextureCount() const { return m_textures.size(); }
    const std::vector<Texture*>& GetTextures() const { return m_textures; }

    // �ڵ� ũ�� ����
    void EnsureTextureSlots(size_t count);

    // ����ȭ
    nlohmann::json SerializeTextures() const;
    void DeserializeTextures(const nlohmann::json& json);

    virtual void BindTextures();
protected:
    std::vector<Texture*> m_textures;
    // ��� ����
    bool m_enabled = true;
    bool m_isRendering = false;
    mutable std::atomic<bool> m_gpuWorkPending = false;

    // owner
    ParticleSystem* m_ownerSystem;

    // �ؽ�ó
    Texture* m_assignedTexture;
    Texture* m_dissolveTexture;

    // pso
    std::unique_ptr<PipelineStateObject> m_pso;

    // ���� ���� ���� ���� �Լ���
    void CreateBlendState(BlendPreset preset);
    void CreateDepthStencilState(DepthPreset preset);
    void CreateRasterizerState(RasterizerPreset preset);

    // �������� D3D11 ����ü�� ��ȯ�ϴ� �Լ���
    D3D11_BLEND_DESC GetBlendDesc(BlendPreset preset) const;
    D3D11_DEPTH_STENCIL_DESC GetDepthStencilDesc(DepthPreset preset) const;
    D3D11_RASTERIZER_DESC GetRasterizerDesc(RasterizerPreset preset) const;
    
    // ���̴� ����
    std::string m_vertexShaderName = "None";
    std::string m_pixelShaderName = "None";

    // ���� ���� ����
    BlendPreset m_blendPreset = BlendPreset::None;
    DepthPreset m_depthPreset = DepthPreset::None;
    RasterizerPreset m_rasterizerPreset = RasterizerPreset::None;

    // Ŀ���� ���� ����
    D3D11_BLEND_DESC m_customBlendDesc = {};
    D3D11_DEPTH_STENCIL_DESC m_customDepthDesc = {};
    D3D11_RASTERIZER_DESC m_customRasterizerDesc = {};

protected:
    // �Ⱦ����� �ϴ� ����
    // Ŭ���� ���� �����͸� �θ𿡼� ����
    PolarClippingParams m_clippingParams = {};
    bool m_clippingEnabled = false;

    // Ŭ���� ��� ���� (�ʿ��� �ڽ� Ŭ���������� ����)
    virtual void OnClippingStateChanged() {}
    virtual void CreateClippingBuffer() {}
    virtual void UpdateClippingBuffer() {}

    float m_effectProgress = 0.0f;
    bool m_useEffectProgress = false;

private:
    // SaveRenderState()가 OMGet*/RSGetState로 캡처한 이전 상태.
    // 이 API들은 참조 카운트를 올려서 돌려주므로 반드시 놓아줘야 한다.
    // SaveRenderState는 재호출 시 이전 캡처를 해제하지만 소멸 시점에는
    // 놓아주는 곳이 없어, 마지막 캡처 3개가 인스턴스마다 누수됐다.
    void ReleaseSavedRenderState()
    {
        Memory::SafeDelete(m_prevDepthState);
        Memory::SafeDelete(m_prevBlendState);
        Memory::SafeDelete(m_prevRasterizerState);
    }

    ID3D11DepthStencilState* m_prevDepthState = nullptr;
    UINT m_prevStencilRef = 0;
    ID3D11BlendState* m_prevBlendState = nullptr;
    float m_prevBlendFactor[4] = { 0 };
    UINT m_prevSampleMask = 0;
    ID3D11RasterizerState* m_prevRasterizerState = nullptr;

};