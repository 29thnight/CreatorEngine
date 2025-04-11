#pragma once
#include "IRenderPass.h"
#include "UIsprite.h"
#include "../ScriptBinder/SpriteComponent.h"
class GameObject;
class Camera;
class UIPass : public IRenderPass
{
public:
	UIPass();
	virtual ~UIPass() {}

	void Initialize(Texture* renderTargetView);


	virtual void Execute(RenderScene& scene,Camera& camera) override;

	static bool compareLayer(SpriteComponent* a, SpriteComponent* b);

	void ControlPanel() override;
	void ReloadShaders() override;
	void Resize() override;


	
private:
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
	ComPtr<ID3D11Buffer> m_UIBuffer;

	Texture* m_renderTarget = nullptr;
	float m_delta;
	
	std::vector<SpriteComponent*> _2DObjects;

};

