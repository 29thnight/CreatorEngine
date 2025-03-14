#pragma once
#include "IRenderPass.h"
#include "Texture.h"

enum class ToneMapType
{
	Reinhard,
	ACES
};

struct alignas(16) ToneMapReinhardConstant
{
    bool m_bUseToneMap{ true };
};

struct alignas(16) ToneMapACESConstant
{
	bool m_bUseToneMap{ true };
    float shoulderStrength{ 0.22f };
	float linearStrength{ 0.30f };
	float linearAngle{ 0.10f };
	float toeStrength{ 0.20f };
	float toeNumerator{ 0.02f };
	float toeDenominator{ 0.30f };
};

class ToneMapPass final : public IRenderPass
{
public:
    ToneMapPass();
    ~ToneMapPass();
    void Initialize(Texture* color, Texture* dest);
	void ToneMapSetting(bool isAbleToneMap, ToneMapType type);
    void Execute(Scene& scene) override;

private:
    Texture* m_ColorTexture{};
    Texture* m_DestTexture{};
	bool m_isAbleToneMap{ true };
	ToneMapType m_toneMapType{ ToneMapType::ACES };
	ID3D11Buffer* m_pReinhardConstantBuffer{};
	ID3D11Buffer* m_pACESConstantBuffer{};
	ToneMapReinhardConstant m_toneMapReinhardConstant{};
	ToneMapACESConstant m_toneMapACESConstant{};
};
