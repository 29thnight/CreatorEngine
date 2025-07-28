#pragma once
#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"

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
    float padding1;              // 16����Ʈ ������ ���� �е�
    float padding2;              // 16����Ʈ ������ ���� �е�
    float padding3;              // 16����Ʈ ������ ���� �е�
};

class SizeModuleCS : public ParticleModule
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