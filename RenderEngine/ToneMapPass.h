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
    float filmSlope{ 2.51f };
    float filmToe{ 0.03f };
    float filmShoulder{ 2.43f };
    float filmBlackClip{ 0.59f };
    float filmWhiteClip{ 0.14f };
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
	ToneMapType m_toneMapType{ ToneMapType::ACES };
	ID3D11Buffer* m_pReinhardConstantBuffer{};
	ID3D11Buffer* m_pACESConstantBuffer{};
	ToneMapReinhardConstant m_toneMapReinhardConstant{};
	ToneMapACESConstant m_toneMapACESConstant{};
};
