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

using namespace concurrency;

class IRenderPass abstract : public IViewEvent
{
public:
	using CommandQueue = concurrent_queue<ID3D11CommandList*>;
	using CommandQueueMap = std::unordered_map<size_t, CommandQueue>;
public:
	IRenderPass() = default;
	virtual ~IRenderPass() = default;

	//virtual std::string ToString() abstract;
	virtual void Execute(RenderScene& scene, Camera& camera) abstract;
	virtual void CreateRenderCommandList(RenderScene& scene, Camera& camera) {}
	virtual void ControlPanel() {};
	void ReloadShaders() { m_pso->ReloadShaders(); }
	virtual void ResizeRelease() {};
	virtual void Resize(uint32_t width, uint32_t height) {}

	void PushQueue(size_t key, ID3D11CommandList* command) { m_commandQueueMapArr[(size_t)!m_frame][key].push(command); }

	CommandQueue* GetCommandQueue(size_t key)
	{
		auto& frameCommandQueue = m_commandQueueMapArr[(size_t)m_frame];

		auto it = frameCommandQueue.find(key);
		if (it != frameCommandQueue.end())
		{
			return &it->second;
		}

		return nullptr;
	}

	void SwapQueue()
	{
		m_frame = !m_frame;
	}

protected:
	std::unique_ptr<PipelineStateObject> m_pso{ nullptr };
	std::array<CommandQueueMap, 2> m_commandQueueMapArr{};

	bool m_abled{ true };
	std::atomic_bool m_frame{};
};