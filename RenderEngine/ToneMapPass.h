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
	bool32 m_bUseToneMap{ true };
	bool32 m_bUseFilmic{ true };
    float filmSlope{ 0.91f };
    float filmToe{ 0.53f };
    float filmShoulder{ 0.23f };
    float filmBlackClip{ 0.f };
    float filmWhiteClip{ 0.035f };
};

class ToneMapPass final : public IRenderPass
{
public:
    ToneMapPass();
    ~ToneMapPass();
    void Initialize(Texture* dest);
	void ToneMapSetting(bool isAbleToneMap, ToneMapType type);
    void Execute(Scene& scene, Camera& camera) override;
	void ControlPanel() override;

private:
    Texture* m_DestTexture{};
	bool m_isAbleToneMap{ true };
	bool m_isAbleFilmic{ true };
	ToneMapType m_toneMapType{ ToneMapType::ACES };
	ID3D11Buffer* m_pReinhardConstantBuffer{};
	ID3D11Buffer* m_pACESConstantBuffer{};
	ToneMapReinhardConstant m_toneMapReinhardConstant{};
	ToneMapACESConstant m_toneMapACESConstant{};
};
