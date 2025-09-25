#pragma once
#include "ParticleModule.h"
#include "EaseInOut.h"
#include "ISerializable.h"

using namespace DirectX;

// ��ƼŬ ������ ����ü (�ٸ� ������ ���ǵǾ� �ִٰ� ����)
struct ParticleData;

// �׶��̼� ����Ʈ ����ü (GPU��)
struct GradientPoint
{
    float time;                    // 0.0 ~ 1.0
    Mathf::Vector4 color;         // RGBA
};

// ���� �Ķ���� ��� ���� (GPU��)
struct ColorParams
{
    float deltaTime;              // ��Ÿ Ÿ��
    int transitionMode;           // ���� ��ȯ ���
    int gradientSize;             // �׶��̼� ����Ʈ ����
    int discreteColorsSize;       // �̻� ���� ����

    int customFunctionType;       // Ŀ���� �Լ� Ÿ��
    float customParam1;           // Ŀ���� �Ķ���� 1
    float customParam2;           // Ŀ���� �Ķ���� 2
    float customParam3;           // Ŀ���� �Ķ���� 3

    float customParam4;           // Ŀ���� �Ķ���� 4
    UINT maxParticles;            // �ִ� ��ƼŬ ��
    float padding[2];             // 16����Ʈ ������ ���� �е�
};

class ColorModuleCS : public ParticleModule, public ISerializable
{
private:
    // ��ǻƮ ���̴� ���ҽ�
    ID3D11ComputeShader* m_computeShader;

    // ��� ����
    ID3D11Buffer* m_colorParamsBuffer;

    // ���ҽ� ����
    ID3D11Buffer* m_gradientBuffer;           // �׶��̼� ������
    ID3D11Buffer* m_discreteColorsBuffer;     // �̻� ���� ������

    // ���̴� ���ҽ� ��
    ID3D11ShaderResourceView* m_gradientSRV;
    ID3D11ShaderResourceView* m_discreteColorsSRV;

    // �Ķ���� �� ����
    ColorParams m_colorParams;

    // ���� ������
    std::vector<std::pair<float, Mathf::Vector4>> m_colorGradient;  // �׶��̼� ������
    std::vector<Mathf::Vector4> m_discreteColors;                   // �̻� ���� ������

    // ��Ƽ �÷���
    bool m_colorParamsDirty;
    bool m_gradientDirty;
    bool m_discreteColorsDirty;

    // �ý��� ����
    UINT m_particleCapacity;

    // ��¡ ���
    EaseInOut m_easingModule;
    bool m_easingEnable;

public:
    ColorModuleCS();
    virtual ~ColorModuleCS();

    // ParticleModuleBase �������̽� ����
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;
    
    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

private:
    // �ʱ�ȭ �޼���
    bool InitializeComputeShader();
    bool CreateConstantBuffers();
    bool CreateResourceBuffers();

    // ������Ʈ �޼���
    void UpdateConstantBuffers();
    void UpdateResourceBuffers();

    // ���� �޼���
    void ReleaseResources();

public:
    // ���� �׶��̼� ����
    void SetColorGradient(const std::vector<std::pair<float, Mathf::Vector4>>& gradient);

    // ��ȯ ��� ����
    void SetTransitionMode(ColorTransitionMode mode);

    // �̻� ���� ����
    void SetDiscreteColors(const std::vector<Mathf::Vector4>& colors);

    // Ŀ���� �Լ� ����
    void SetCustomFunction(int functionType, float param1, float param2 = 0.0f, float param3 = 0.0f, float param4 = 0.0f);

    // �̸� ���ǵ� ���� ȿ����
    void SetPulseColor(const Mathf::Vector4& baseColor, const Mathf::Vector4& pulseColor, float frequency);
    void SetSineWaveColor(const Mathf::Vector4& color1, const Mathf::Vector4& color2, float frequency, float phase = 0.0f);
    void SetFlickerColor(const std::vector<Mathf::Vector4>& colors, float speed);
    void SetRandomColors(const std::vector<Mathf::Vector4>& colors);

    // ��¡ ����
    void SetEasing(EasingEffect easingType, StepAnimation animationType, float duration);

    // Getter
    int GetCustomFunctionType() const { return m_colorParams.customFunctionType; }
    float GetCustomParam1() const { return m_colorParams.customParam1; }
    float GetCustomParam2() const { return m_colorParams.customParam2; }
    float GetCustomParam3() const { return m_colorParams.customParam3; }
    float GetCustomParam4() const { return m_colorParams.customParam4; }
    EaseInOut GetEasingModule() { return m_easingModule; }

    // ���� ��ȸ
    bool IsInitialized() const { return m_isInitialized; }
    ColorTransitionMode GetTransitionMode() const { return static_cast<ColorTransitionMode>(m_colorParams.transitionMode); }
    const std::vector<std::pair<float, Mathf::Vector4>>& GetColorGradient() const { return m_colorGradient; }
    const std::vector<Mathf::Vector4>& GetDiscreteColors() const { return m_discreteColors; }

    // ��¡ ����
    void EnableEasing(bool enable) { m_easingEnable = enable; }
    bool IsEasingEnabled() const { return m_easingEnable; }
};