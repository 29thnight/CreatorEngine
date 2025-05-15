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

struct IViewEvent
{
	virtual ~IViewEvent() = default;

	virtual void ResizeRelease() = 0;
	virtual void Resize(uint32_t width, uint32_t height) = 0;

	Core::DelegateHandle m_onReleaseHandle{};
	Core::DelegateHandle m_onResizeHandle{};
};

class IRenderPass abstract : public IViewEvent
{
public:
	IRenderPass() = default;
	virtual ~IRenderPass() = default;

	//virtual std::string ToString() abstract;
	virtual void Execute(RenderScene& scene, Camera& camera) abstract;
	virtual void ControlPanel() {};
	void ReloadShaders() { m_pso->ReloadShaders(); };
	virtual void ResizeRelease() {};
	virtual void Resize(uint32_t width, uint32_t height) {};


protected:
	std::unique_ptr<PipelineStateObject> m_pso{ nullptr };
	bool m_abled{ true };
};