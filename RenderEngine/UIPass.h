#pragma once
#include "IRenderPass.h"
#include "SceneObject.h"
#include "UIsprite.h"



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
	virtual void Execute(Scene& scene,Camera& camera);



	
private:
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
	ComPtr<ID3D11Buffer> m_UIBuffer;

	Texture* m_renderTarget = nullptr;
	float m_delta;
	std::vector<SceneObject*> _2DObjects;
	//테스트용
	std::vector<UIsprite*> _testUI;
	
};

