#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

struct alignas(16) TrailParams
{
    float deltaTime;
    float currentTime;
    UINT maxTrailParticles;
    float trailLifetime;

    UINT sourceParticleCount;
    float minDistance;
    XMFLOAT2 size;

    XMFLOAT4 color;
};

struct TrailSegment
{
    UINT sourceParticleID;
    float pad1;
    float pad2;
    float pad3;
    XMFLOAT3 previousPosition;
    float segmentAge;
};

class TrailModuleCS : public ParticleModule, public ISerializable
{
private:
    ID3D11ComputeShader* m_computeShader;
    ID3D11Buffer* m_paramsBuffer;
    ID3D11Buffer* m_trailDataBuffer;
    ID3D11UnorderedAccessView* m_trailDataUAV;
    ID3D11ShaderResourceView* m_sourceParticlesSRV;

    TrailParams m_params;
    bool m_paramsDirty;
    UINT m_particleCapacity;
    UINT m_sourceParticleCount;
    std::mutex m_resetMutex;

public:
    TrailModuleCS();
    virtual ~TrailModuleCS();

    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;
    virtual void OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition) override;

    void ResetForReuse();
    bool IsReadyForReuse() const;

    void SetSourceParticleCount(UINT count);
    void SetSourceParticlesSRV(ID3D11ShaderResourceView* srv);
    void SetMinDistance(float distance) { m_params.minDistance = distance; m_paramsDirty = true; }
    void SetTrailLifetime(float lifetime) { m_params.trailLifetime = lifetime; m_paramsDirty = true; }
    void SetParticleSize(const XMFLOAT2& size) { m_params.size = size; m_paramsDirty = true; }
    void SetParticleColor(const XMFLOAT4& color) { m_params.color = color; m_paramsDirty = true; }

    float GetMinDistance() const { return m_params.minDistance; }
    float GetTrailLifetime() const { return m_params.trailLifetime; }
    XMFLOAT2 GetParticleSize() const { return m_params.size; }
    XMFLOAT4 GetParticleColor() const { return m_params.color; }
    UINT GetParticleCapacity() const { return m_particleCapacity; }
    bool IsInitialized() const { return m_isInitialized; }

    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

private:
    bool InitializeComputeShader();
    bool CreateConstantBuffers();
    bool CreateUtilityBuffers();
    void UpdateConstantBuffers(float deltaTime);
    void ReleaseResources();
};