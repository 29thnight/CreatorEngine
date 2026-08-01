#pragma once
#include "IRenderPass.h"
#include "Texture.h"

constexpr uint32 NUM_BINS = 256;

enum class ToneMapType
{
	Reinhard,
	ACES,
	Uncharted2,
	HDR10,
	ACESFilm,
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

struct ToneMapPassSetting;
class ToneMapPass final : public IRenderPass
{
public:
    ToneMapPass();
    ~ToneMapPass();
    void Initialize(Managed::SharedPtr<Texture> dest);
	void ToneMapSetting(bool isAbleToneMap, ToneMapType type);
    void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
    void ApplySettings(const ToneMapPassSetting& setting);
	void PrepareDownsampleTextures(uint32_t width, uint32_t height);
	void Resize(uint32_t width, uint32_t height) override;

	/// 자동 노출이 매 프레임 무슨 값을 보고 무엇을 결정했는지.
	///
	/// 이 값들은 화면만 봐서는 알 수 없다 — 결과가 '어둡다' 하나로 뭉뚱그려져
	/// 측광이 틀린 것인지, 노출 계산이 틀린 것인지, 조명이 안 닿는 것인지가
	/// 구분되지 않는다. 그래서 숫자를 밖으로 낸다(render.exposure).
	///
	/// 인스턴스가 하나뿐이라 정적으로 둔다. 카메라가 여럿이면 마지막 카메라
	/// 값이 남는데, 진단용으로는 그것으로 충분하다.
	struct ExposureDiagnostics
	{
		bool  autoEnabled{ false };
		bool  sampled{ false };      // 이번 프레임에 휘도를 실제로 읽었는가
		float measuredLuminance{ 0.f };
		float exposureManual{ 0.f };
		float exposureAuto{ 0.f };
		float exposureFinal{ 0.f };
		float currentExposure{ 0.f };
		float fNumber{ 0.f };
		float shutterTime{ 0.f };
		float iso{ 0.f };
	};

	static const ExposureDiagnostics& GetExposureDiagnostics() { return s_exposureDiagnostics; }

private:
	static inline ExposureDiagnostics s_exposureDiagnostics{};

	Managed::WeakPtr<Texture> m_DestTexture{};
	ComPtr<ID3D11Texture2D> m_readbackTexture[2];

	bool m_isAbleAutoExposure{ true };
	bool m_isAbleToneMap{ true };
	// Auto Exposure Settings
	float m_fNumber{ 4.f };
	float m_shutterTime{ 16.f }; // 1/100s
	float m_ISO{ 100.f };
	float m_exposureCompensation{};
	float m_speedBrightness{ 3.f };
	float m_speedDarkness{ 1.7f };

	uint32 m_readIndex{ 0 };
	uint32 m_writeIndex{ 1 };

	ComputeShader* m_pAutoExposureEvalCS{};
	std::vector<Texture*> m_downsampleTextures;
	ToneMapType m_toneMapType{ ToneMapType::ACES };

	ID3D11Buffer* m_pToneMapConstantBuffer{};

	ToneMapConstant m_toneMapConstant{};
};
