#pragma once
#include "RenderModules.h"
#include "TrailGenerateModule.h"
#include "ISerializable.h"

struct TrailConstantBuffer
{
    Mathf::Matrix world;
    Mathf::Matrix view;
    Mathf::Matrix projection;
    Mathf::Vector3 cameraPosition;
    float time;
};

class TrailRenderModule : public RenderModules, public ISerializable
{
public:
    TrailRenderModule();
    virtual ~TrailRenderModule();

    void Initialize() override;
    void Release() override;
    void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) override;
    void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount) override;
    void SetupRenderTarget(RenderPassData* renderData) override;
    void SetTexture(Texture* texture) override;

    void ResetForReuse() override;
    bool IsReadyForReuse() const override;
    void WaitForGPUCompletion() override;

    // TrailGenerateModule 연결
    void SetTrailGenerateModule(TrailGenerateModule* trailModule);
    TrailGenerateModule* GetTrailGenerateModule() const { return m_trailModule; }

    // 카메라 위치 설정
    void SetCameraPosition(const Mathf::Vector3& position);
    Mathf::Vector3 GetCameraPosition() const { return m_constantBufferData.cameraPosition; }

    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

private:
    void UpdateConstantBuffer(const Mathf::Matrix& world, const Mathf::Matrix& view, const Mathf::Matrix& projection);

private:
    ComPtr<ID3D11Buffer> m_constantBuffer;
    TrailConstantBuffer m_constantBufferData;

    TrailGenerateModule* m_trailModule;
    ID3D11ShaderResourceView* m_particleSRV;
    UINT m_instanceCount;
};