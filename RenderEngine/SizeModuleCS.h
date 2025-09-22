#pragma once
#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"
#include "ISerializable.h"

using namespace DirectX;

struct alignas(16) SizeParams
{
    Mathf::Vector2 startSize;         // 시작 크기
    Mathf::Vector2 endSize;           // 끝 크기

    float deltaTime;            // 프레임 델타 시간 (이징 적용됨)
    int useRandomScale;         // 랜덤 스케일 사용 여부
    float randomScaleMin;       // 랜덤 스케일 최소값
    float randomScaleMax;       // 랜덤 스케일 최대값
    
    UINT maxParticles;          // 최대 파티클 수
    float pad1;              // 16바이트 정렬을 위한 패딩
    float pad2;              // 16바이트 정렬을 위한 패딩
    float pad3;              // 16바이트 정렬을 위한 패딩

    Mathf::Vector3 emitterScale;
    float pad4;
};

class SizeModuleCS : public ParticleModule, public ISerializable
{
public:
    SizeModuleCS();
    virtual ~SizeModuleCS();

    // 기본 인터페이스
    void Initialize() override;
    void Update(float deltaTime) override;
    void Release() override;
    void OnSystemResized(UINT maxParticles) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;

    // Size 모듈 전용 설정 메서드
    void SetStartSize(const XMFLOAT2& size);
    void SetEndSize(const XMFLOAT2& size);
    void SetRandomScale(bool enabled, float minScale = 0.5f, float maxScale = 2.0f);
    void SetEasing(EasingEffect easingType, StepAnimation animationType, float duration);

    // 편의 메서드 (uniform size)
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

    // 이징 비활성화 메서드
    void DisableEasing() {
        m_easingEnable = false;
    }

private:
    // 초기화 메서드
    bool InitializeComputeShader();
    bool CreateConstantBuffers();

    // 업데이트 메서드
    void UpdateConstantBuffers();

    // 리소스 정리
    void ReleaseResources();

private:
    // DirectX 리소스
    ID3D11ComputeShader* m_computeShader;
    ID3D11Buffer* m_sizeParamsBuffer;

    // 파라미터
    SizeParams m_sizeParams;
    bool m_paramsDirty;

    // 이징 관련
    EaseInOut m_easingModule;
    bool m_easingEnable;

    // 상태
    bool m_isInitialized;
    UINT m_particleCapacity;
};