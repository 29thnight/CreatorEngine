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
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(RenderScene& scene, Camera& camera) override;
	virtual void Resize(uint32_t width, uint32_t height) override;


private:
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
	ID3D11RenderTargetView* m_renderTargetViews[RTV_TypeMax]{}; //0: diffuse, 1: metalRough, 2: normal, 3: emissive
	ComPtr<ID3D11DeviceContext> defferdContext1{ nullptr };
	ThreadPool* m_threadPool{ nullptr };
};