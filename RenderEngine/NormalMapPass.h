#pragma once
#include "IRenderPass.h"
#include <unordered_map>

class Texture;
class NormalMapPass final : public IRenderPass
{
public:
	NormalMapPass();

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ClearTextures();

	void ControlPanel() override;
	void ReloadShaders() override;
	void Resize() override;

	std::unordered_map<std::string, Texture*> m_normalMapTextures;
};

