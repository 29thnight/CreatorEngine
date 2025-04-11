#pragma once
#include "IRenderPass.h"
#include "UIsprite.h"

class GameObject;
class Camera;
class UIPass : public IRenderPass
{
public:
	UIPass();
	virtual ~UIPass() {}

	void Initialize(Texture* renderTargetView);

	void pushUI(UIsprite* UI);
	void Update(float delta);

	void DrawCanvas(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection);
	virtual void Execute(RenderScene& scene,Camera& camera);

	static bool compareLayer(UIsprite* a, UIsprite* b);

	void ControlPanel() override;
	void Resize() override;


	
private:
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
	ComPtr<ID3D11Buffer> m_UIBuffer;

	Texture* m_renderTarget = nullptr;
	float m_delta;
	std::vector<GameObject*> _2DObjects;
	//테스트용
	std::vector<UIsprite*> _testUI;
	
};

