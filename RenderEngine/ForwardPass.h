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
	void CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
private:
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
};