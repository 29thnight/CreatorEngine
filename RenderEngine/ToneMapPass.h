#pragma once
#include "IRenderPass.h"
#include "Texture.h"

constexpr uint32 NUM_BINS = 256;

enum class ToneMapType
{
	Reinhard,
	ACES,
	Uncharted2,
	HDR10
};

cbuffer ToneMapConstant
{
	int	  operatorType{ static_cast<int>(ToneMapType::ACES) };
    float filmSlope{ 0.88f };
    float filmToe{ 0.55f };
    float filmShoulder{ 0.26f };
    float filmBlackClip{ 0.f };
    float filmWhiteClip{ 0.04f };
	float toneMapExposure{ 1.f };
};

class ToneMapPass final : public IRenderPass
{
public:
    ToneMapPass();
    ~ToneMapPass();
    void Initialize(Texture* dest);
	void ToneMapSetting(bool isAbleToneMap, ToneMapType type);
    void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void PrepareDownsampleTextures(uint32_t width, uint32_t height);
	void Resize(uint32_t width, uint32_t height) override;


private:
    Texture* m_DestTexture{};
	ComPtr<ID3D11Texture2D> m_readbackTexture[2];

	bool m_isAbleAutoExposure{ true };
	bool m_isAbleToneMap{ true };

	float m_fNumber{ 2.5f };
	float m_shutterTime{ 0.01f }; // 1/100s
	float m_ISO{ 100.f };
	float m_exposureCompensation{};

	uint32 m_readIndex{ 0 };
	uint32 m_writeIndex{ 1 };

	ComputeShader* m_pAutoExposureEvalCS{};
	std::vector<Texture*> m_downsampleTextures;
	ToneMapType m_toneMapType{ ToneMapType::ACES };

	ID3D11Buffer* m_pToneMapConstantBuffer{};

	ToneMapConstant m_toneMapConstant{};
};
