#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "../ScriptBinder/GameObject.h"

class ForwardPass final : public IRenderPass
{
public:
	ForwardPass();
	~ForwardPass();

	void Execute(RenderScene& scene, Camera& camera);
	void ControlPanel() override;
	void Resize() override;

	void PushForwardQueue(GameObject* sceneObject);
	void ClearForwardQueue();

private:
	ID3D11RenderTargetView* m_RenderTarget{};
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;

	//forward render queue
	std::vector<GameObject*> m_forwardQueue;
};