#pragma once
#include "IRenderPass.h"
#include "../ScriptBinder/ImageComponent.h"
#include "../ScriptBinder/TextComponent.h"

namespace DirectX
{
	namespace DX11
	{
		class SpriteBatch;
	}
}

class GameObject;
class Camera;

class UIPass : public IRenderPass
{
public:
	UIPass();
	virtual ~UIPass() {}

	void Initialize(Texture* renderTargetView, SpriteBatch* spriteBatch);


	virtual void Execute(RenderScene& scene,Camera& camera) override;

	static bool compareLayer(int a, int b);

	void ControlPanel() override;
	void Resize() override;


	
private:
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
	ComPtr<ID3D11Buffer> m_UIBuffer;
	SpriteBatch* m_spriteBatch = nullptr;
	Texture* m_renderTarget = nullptr;
	float m_delta;
	
	std::vector<ImageComponent*> _ImageObjects;
	std::vector<TextComponent*> _TextObjects;
};

