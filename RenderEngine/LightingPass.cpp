#include "LightingPass.h"

LightingPass::LightingPass()
{
}

LightingPass::~LightingPass()
{
}

void LightingPass::Initialize(Texture* dest)
{
	m_pLightingTexture = dest;
}

void LightingPass::Execute(RenderScene& scene, Camera& camera)
{
}

void LightingPass::ControlPanel()
{
}
