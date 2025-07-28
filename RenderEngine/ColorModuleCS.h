#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"

using namespace DirectX;

// 그라데이션 포인트 구조체 (GPU용)
struct GradientPoint
{
    float time;                    // 0.0 ~ 1.0
    Mathf::Vector4 color;         // RGBA
};

// 색상 파라미터 상수 버퍼 (GPU용)
struct ColorParams
{
    float deltaTime;              // 델타 타임
    int transitionMode;           // 색상 전환 모드
    int gradientSize;             // 그라데이션 포인트 개수
    int discreteColorsSize;       // 이산 색상 개수

    int customFunctionType;       // 커스텀 함수 타입
    float customParam1;           // 커스텀 파라미터 1
    float customParam2;           // 커스텀 파라미터 2
    float customParam3;           // 커스텀 파라미터 3

    float customParam4;           // 커스텀 파라미터 4
    UINT maxParticles;            // 최대 파티클 수
    float padding[2];             // 16바이트 정렬을 위한 패딩
};

// 파티클 데이터 구조체 (다른 곳에서 정의되어 있다고 가정)
struct ParticleData;

class ColorModuleCS : public ParticleModule
{
private:
    // 컴퓨트 셰이더 리소스
    ID3D11ComputeShader* m_computeShader;

    // 상수 버퍼
    ID3D11Buffer* m_colorParamsBuffer;

    // 리소스 버퍼
    ID3D11Buffer* m_gradientBuffer;           // 그라데이션 데이터
    ID3D11Buffer* m_discreteColorsBuffer;     // 이산 색상 데이터

    // 셰이더 리소스 뷰
    ID3D11ShaderResourceView* m_gradientSRV;
    ID3D11ShaderResourceView* m_discreteColorsSRV;

    // 파라미터 및 상태
    ColorParams m_colorParams;

    // 색상 데이터
    std::vector<std::pair<float, Mathf::Vector4>> m_colorGradient;  // 그라데이션 데이터
    std::vector<Mathf::Vector4> m_discreteColors;                   // 이산 색상 데이터

    // 더티 플래그
    bool m_colorParamsDirty;
    bool m_gradientDirty;
    bool m_discreteColorsDirty;

    // 시스템 상태
    UINT m_particleCapacity;

    // 이징 모듈
    EaseInOut m_easingModule;
    bool m_easingEnable;

public:
    ColorModuleCS();
    virtual ~ColorModuleCS();

    // ParticleModuleBase 인터페이스 구현
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;

private:
    // 초기화 메서드
    bool InitializeComputeShader();
    bool CreateConstantBuffers();
    bool CreateResourceBuffers();

    // 업데이트 메서드
    void UpdateConstantBuffers();
    void UpdateResourceBuffers();

    // 정리 메서드
    void ReleaseResources();

public:
    // 색상 그라데이션 설정
    void SetColorGradient(const std::vector<std::pair<float, Mathf::Vector4>>& gradient);

    // 전환 모드 설정
    void SetTransitionMode(ColorTransitionMode mode);

    // 이산 색상 설정
    void SetDiscreteColors(const std::vector<Mathf::Vector4>& colors);

    // 커스텀 함수 설정
    void SetCustomFunction(int functionType, float param1, float param2 = 0.0f, float param3 = 0.0f, float param4 = 0.0f);

    // 미리 정의된 색상 효과들
    void SetPulseColor(const Mathf::Vector4& baseColor, const Mathf::Vector4& pulseColor, float frequency);
    void SetSineWaveColor(const Mathf::Vector4& color1, const Mathf::Vector4& color2, float frequency, float phase = 0.0f);
    void SetFlickerColor(const std::vector<Mathf::Vector4>& colors, float speed);

    // 이징 설정
    void SetEasing(EasingEffect easingType, StepAnimation animationType, float duration);

    // 상태 조회
    bool IsInitialized() const { return m_isInitialized; }
    ColorTransitionMode GetTransitionMode() const { return static_cast<ColorTransitionMode>(m_colorParams.transitionMode); }
    const std::vector<std::pair<float, Mathf::Vector4>>& GetColorGradient() const { return m_colorGradient; }
    const std::vector<Mathf::Vector4>& GetDiscreteColors() const { return m_discreteColors; }

    // 이징 제어
    void EnableEasing(bool enable) { m_easingEnable = enable; }
    bool IsEasingEnabled() const { return m_easingEnable; }
};