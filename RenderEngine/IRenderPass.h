#pragma once
#include "RenderScene.h"
#include "PSO.h"

enum RTV_Type
{
	BaseColor,
	OccRoughMetal,
	Normal,
	Emissive,
	RTV_TypeMax
};

class IRenderPass abstract
{
public:
	IRenderPass() = default;
	virtual ~IRenderPass() = default;

	//virtual std::string ToString() abstract;
	virtual void Execute(RenderScene& scene, Camera& camera) abstract;
	virtual void ControlPanel() {};
	void ReloadShaders() { m_pso->ReloadShaders(); };
	virtual void Resize() {};


protected:
	std::unique_ptr<PipelineStateObject> m_pso{ nullptr };
	bool m_abled{ true };
};