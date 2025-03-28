#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "GameObject.h"

class Camera;
class GBufferPass final : public IRenderPass
{
public:
	GBufferPass();
	~GBufferPass();

	void SetRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size);
	void SetEditorRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size);
	void Execute(RenderScene& scene, Camera& camera) override;
	void PushDeferredQueue(GameObject* sceneObject);
	void ClearDeferredQueue();

private:
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
	ID3D11RenderTargetView* m_renderTargetViews[RTV_TypeMax]{}; //0: diffuse, 1: metalRough, 2: normal, 3: emissive
	ID3D11RenderTargetView* m_editorRTV[RTV_TypeMax]{}; //0: diffuse, 1: metalRough, 2: normal, 3: emissive

	//deferred render queue
	std::vector<GameObject*> m_deferredQueue;
};