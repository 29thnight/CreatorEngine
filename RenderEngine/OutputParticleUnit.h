#pragma once
#include "ParticlesUnit.h"

class Texture;
class RenderPassData;
class OutputParticleUnit abstract
{
public:
	OutputParticleUnit() = default;
	~OutputParticleUnit() = default;

	virtual void Initialize() abstract;
	virtual void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) abstract;

	virtual void SetTexture(Texture* texture) abstract;
	virtual void BindResource() abstract;
	virtual void SetupRenderTarget(RenderPassData* renderData) abstract;
	virtual void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instancecount) abstract;

	HashedGuid m_inputDataID{ make_guid() }; // input pin ID for Editor use
};