#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
struct ColorGradingPassSetting;
class ColorGradingPass final : public IRenderPass
{
public:
	ColorGradingPass();
	~ColorGradingPass();

	void Initialize();
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
    void ApplySettings(const ColorGradingPassSetting& setting);
	void SetColorGradingTexture(std::string_view filename);

	Managed::UniquePtr<Texture> m_pColorGradingTexture{};
private:
	Texture* m_pCopiedTexture{};
	Texture* m_pDefaultLUTTexture{};
	ComPtr<ID3D11Buffer> m_Buffer{};
	ComPtr<ID3D11Buffer> m_EditBuffer{};
	float lerp = 0.f;
	float timer = 0.f;
	bool isOn{ true };
	bool useLUTEdit{ false };
	HashingString m_textureFilePath{ "" };

	// Basic
	float exposure = 1;
	float contrast = 1;
	float saturation = 1;

	// White Balance
	float temperature = 0;
	float tint = 0;

	// Tonal & Color Tinting
	float4 shadows{ 1,1,1,0 };    // .rgb: Color, .a: Brightness Offset
	float4 midtones{ 1,1,1,1 };   // .rgb: Color, .a: Brightness Power
	float4 highlights{ 1,1,1,1 }; // .rgb: Color, .a: Brightness Scale

	ComputeShader* m_pColorGradingLUTShader = nullptr;
	Texture* m_pColorGradingLUTTexture = nullptr;
	std::string m_tempLUTName{ "ColorGradingLUT" };
};

