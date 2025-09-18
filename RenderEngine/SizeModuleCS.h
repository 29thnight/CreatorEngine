#pragma once
#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"
#include "ISerializable.h"

using namespace DirectX;

struct alignas(16) SizeParams
{
    Mathf::Vector2 startSize;         // ���� ũ��
    Mathf::Vector2 endSize;           // �� ũ��

    float deltaTime;            // ������ ��Ÿ �ð� (��¡ �����)
    int useRandomScale;         // ���� ������ ��� ����
    float randomScaleMin;       // ���� ������ �ּҰ�
    float randomScaleMax;       // ���� ������ �ִ밪
    
    UINT maxParticles;          // �ִ� ��ƼŬ ��
    float pad1;              // 16����Ʈ ������ ���� �е�
    float pad2;              // 16����Ʈ ������ ���� �е�
    float pad3;              // 16����Ʈ ������ ���� �е�

    Mathf::Vector3 emitterScale;
    float pad4;
};

class SizeModuleCS : public ParticleModule, public ISerializable
{
public:
    SizeModuleCS();
    virtual ~SizeModuleCS();

    // �⺻ �������̽�
    void Initialize() override;
    void Update(float deltaTime) override;
    void Release() override;
    void OnSystemResized(UINT maxParticles) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;

    // Size ��� ���� ���� �޼���
    void SetStartSize(const XMFLOAT2& size);
    void SetEndSize(const XMFLOAT2& size);
    void SetRandomScale(bool enabled, float minScale = 0.5f, float maxScale = 2.0f);
    void SetEasing(EasingEffect easingType, StepAnimation animationType, float duration);

    // ���� �޼��� (uniform size)
    void SetStartSize(float size) { SetStartSize(XMFLOAT2(size, size)); }
    void SetEndSize(float size) { SetEndSize(XMFLOAT2(size, size)); }

    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

    void SetEmitterTransform(Mathf::Vector3 scale) { m_sizeParams.emitterScale = scale; m_paramsDirty = true; }

    XMFLOAT2 GetStartSize() const {
        return XMFLOAT2(m_sizeParams.startSize.x, m_sizeParams.startSize.y);
    }

    XMFLOAT2 GetEndSize() const {
        return XMFLOAT2(m_sizeParams.endSize.x, m_sizeParams.endSize.y);
    }

    bool GetUseRandomScale() const {
        return m_sizeParams.useRandomScale != 0;
    }

    float GetRandomScaleMin() const {
        return m_sizeParams.randomScaleMin;
    }

    float GetRandomScaleMax() const {
        return m_sizeParams.randomScaleMax;
    }

    bool IsEasingEnabled() const {
        return m_easingEnable;
    }

    EasingEffect GetEasingType() const {
        return m_easingModule.GetEasingType();
    }

    StepAnimation GetAnimationType() const {
        return m_easingModule.GetAnimationType();
    }

    float GetEasingDuration() const {
        return m_easingModule.GetDuration();
    }

    UINT GetParticleCapacity() const {
        return m_particleCapacity;
    }

    bool IsInitialized() const {
        return m_isInitialized;
    }

    // ��¡ ��Ȱ��ȭ �޼���
    void DisableEasing() {
        m_easingEnable = false;
    }

private:
    // �ʱ�ȭ �޼���
    bool InitializeComputeShader();
    bool CreateConstantBuffers();

    // ������Ʈ �޼���
    void UpdateConstantBuffers();

    // ���ҽ� ����
    void ReleaseResources();

private:
    // DirectX ���ҽ�
    ID3D11ComputeShader* m_computeShader;
    ID3D11Buffer* m_sizeParamsBuffer;

    // �Ķ����
    SizeParams m_sizeParams;
    bool m_paramsDirty;

    // ��¡ ����
    EaseInOut m_easingModule;
    bool m_easingEnable;

    // ����
    bool m_isInitialized;
    UINT m_particleCapacity;
};