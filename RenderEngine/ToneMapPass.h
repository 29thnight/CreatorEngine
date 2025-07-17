#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "directxtk/PostProcess.h"

constexpr uint32 NUM_BINS = 256;

enum class ToneMapType
{
	Reinhard,
	ACES
};

cbuffer ToneMapReinhardConstant
{
    bool32 m_bUseToneMap{ true };
};

cbuffer ToneMapACESConstant
{
	bool32 m_bUseToneMap{ true };
	bool32 m_bUseFilmic{ true };
    float filmSlope{ 0.88f };
    float filmToe{ 0.55f };
    float filmShoulder{ 0.26f };
    float filmBlackClip{ 0.f };
    float filmWhiteClip{ 0.04f };
	float toneMapExposure{ 1.f };
};

cbuffer LuminanceHistogramData
{
	uint32 inputWidth{ 1920 };
	uint32 inputHeight{ 1080 };
	float minLogLuminance{ -10.f };
	float oneOverLogLuminanceRange{ 1.f / 12.f };
};

cbuffer LuminanceAverageData
{
	uint32 pixelCount{ 1920 * 1080 };
	float minLogLuminance{ -10.f };
	float logLuminanceRange{ 12.f };
	float timeDelta;
	float tau;
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
	void Resize(uint32_t width, uint32_t height) override;


private:
	std::unique_ptr<ToneMapPostProcess> m_pToneMapPostProcess;

    Texture* m_DestTexture{};
	ComPtr<ID3D11Texture2D> luminanceTexture;
	ComPtr<ID3D11UnorderedAccessView> luminanceUAV;
	ComPtr<ID3D11Texture2D> readbackTexture;
	//ComPtr<ID3D11ShaderResourceView> m_exposureSRV;
	ComPtr<ID3D11UnorderedAccessView> m_exposureUAV;
	bool m_isAbleAutoExposure{ true };
	bool m_isAbleToneMap{ true };
	bool m_isAbleFilmic{ true };

	ComputeShader* m_pAutoExposureHistogramCS{};
	ComputeShader* m_pAutoExposureEvalCS{};

	ToneMapType m_toneMapType{ ToneMapType::ACES };

	ID3D11Buffer* m_pHistogramBuffer{};
	ID3D11Buffer* m_pLuminanceAverageBuffer{};
	ID3D11Buffer* m_pAutoExposureConstantBuffer{};
	//ID3D11Buffer* m_pAutoExposureReadBuffer{};
	ID3D11Buffer* m_pReinhardConstantBuffer{};
	ID3D11Buffer* m_pACESConstantBuffer{};

	LuminanceHistogramData m_luminanceHistogramConstant{};
	LuminanceAverageData m_luminanceAverageConstant{};
	ToneMapReinhardConstant m_toneMapReinhardConstant{};
	ToneMapACESConstant m_toneMapACESConstant{};
};
