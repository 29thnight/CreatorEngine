#pragma once
#include "IRenderPass.h"
#include <unordered_map>

class Texture;
class PositionMapPass final : public IRenderPass
{
public:
	PositionMapPass();

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ClearTextures();

	std::unordered_map<std::string, Texture*> m_positionMapTextures;
};

