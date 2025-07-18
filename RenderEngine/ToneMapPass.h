#pragma once
#include "IRenderPass.h"
#include "Texture.h"

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
	ComPtr<ID3D11Texture2D> readbackTexture;

	bool m_isAbleAutoExposure{ true };
	bool m_isAbleToneMap{ true };
	bool m_isAbleFilmic{ true };

	ComputeShader* m_pAutoExposureEvalCS{};
	std::vector<Texture*> m_downsampleTextures;
	ToneMapType m_toneMapType{ ToneMapType::ACES };

	ID3D11Buffer* m_pReinhardConstantBuffer{};
	ID3D11Buffer* m_pACESConstantBuffer{};

	ToneMapReinhardConstant m_toneMapReinhardConstant{};
	ToneMapACESConstant m_toneMapACESConstant{};
};
