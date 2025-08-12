#pragma once
#include "IRenderPass.h"
#include "../ScriptBinder/ImageComponent.h"
#include "../ScriptBinder/TextComponent.h"
#include "directxtk/CommonStates.h"

namespace DirectX
{
	namespace DX11
	{
		class SpriteBatch;
	}
}

class GameObject;
class Camera;
class RenderPassData;
class UIPass : public IRenderPass
{
	using UIRenderQueue = std::array<std::vector<ImageComponent*>, FrameCount>;
	using TextRenderQueue = std::array<std::vector<TextComponent*>, FrameCount>;
public:
	UIPass();
	virtual ~UIPass() {}

	void Initialize(Texture* renderTargetView);

	void SortUIObjects();
	virtual void Execute(RenderScene& scene,Camera& camera) override;
	virtual void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ClearFrameQueue();
	static bool compareLayer(int a, int b);

	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;


	
private:
	ComPtr<ID3D11DepthStencilState>	m_NoWriteDepthStencilState{};
	ComPtr<ID3D11Buffer>			m_UIBuffer;
	std::unique_ptr<SpriteBatch>	m_spriteBatch = nullptr;
	std::unique_ptr<CommonStates>   m_commonStates;
	Texture*						m_renderTarget = nullptr;
	float							m_delta;

private:
	UIRenderQueue					_ImageObjects;
	TextRenderQueue					_TextObjects;
};

