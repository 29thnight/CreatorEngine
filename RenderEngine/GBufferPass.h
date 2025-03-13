#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "SceneObject.h"

class GBufferPass final : public IRenderPass
{
public:
	GBufferPass();
	~GBufferPass();

	void SetRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size);
	void Execute(Scene& scene) override;
	void PushDeferredQueue(SceneObject* sceneObject);

private:
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
	ID3D11RenderTargetView* m_renderTargetViews[RTV_TypeMax]{}; //0: diffuse, 1: metalRough, 2: normal, 3: emissive

	//deferred render queue
	std::vector<SceneObject*> m_deferredQueue;
};