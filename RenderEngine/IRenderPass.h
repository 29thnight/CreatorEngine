#pragma once
#include "RenderScene.h"
#include "PSO.h"
#include "SwapEvent.h"

class RHICommandContext;

enum RTV_Type
{
	BaseColor,
	OccRoughMetal,
	Normal,
	Emissive,
	BitMask,
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
using CommandQueue = concurrent_queue<ID3D11CommandList*>;

constexpr size_t CameraCount = 10; // �ִ� ī�޶� ����
constexpr size_t FrameCount = 3; // ������ ���� ����

class IRenderPass abstract : public IViewEvent
{
public:
	using FrameQueueArray = std::array<std::array<CommandQueue, CameraCount>, FrameCount>;
public:
	IRenderPass()
	{
		m_swapEventHandle = SwapEvent.AddRaw(this, &IRenderPass::SwapQueue);
	}
	virtual ~IRenderPass()
	{
		SwapEvent -= m_swapEventHandle;

		for (auto& frameQueue : m_frameQueues)
		{
			for (auto& queue : frameQueue)
			{
				while (!queue.empty())
				{
					ID3D11CommandList* command;
					if (queue.try_pop(command))
					{
						Memory::SafeDelete(command);
					}
				}
			}
		}

	}

	//virtual std::string ToString() abstract;
	virtual void Execute(RenderScene& scene, Camera& camera) abstract;
	void ExecuteCommandList(RenderScene& scene, Camera& camera)
	{
		size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;

		auto& frameQueue = m_frameQueues[prevIndex][camera.m_cameraIndex];
		while (!frameQueue.empty())
		{
			ID3D11CommandList* command;
			if (frameQueue.try_pop(command))
			{
				DirectX11::ExecuteCommandList(command, true);
				Memory::SafeDelete(command);
			}
		}
		frameQueue.clear();
	}
	// 커맨드 기록 진입점(PHASE 3-1, 7차 슬라이스에서 서명 교체).
	// 빌드 시스템이 RHI 컨텍스트를 만들어 넘긴다 — 패스는 그래픽 API를 직접 받지
	// 않는다. 아직 네이티브가 필요한 잔존 경로는 context.GetNativeHandle()로 얻는다.
	virtual void CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera) {}
	virtual void ControlPanel() {};
	void ReloadShaders() { m_pso->ReloadShaders(); }
	virtual void ResizeRelease() {};
	virtual void Resize(uint32_t width, uint32_t height) {}

	void PushQueue(size_t key, ID3D11CommandList* command) 
	{ 
		size_t index = m_frame.load(std::memory_order_relaxed) % 3;
		m_frameQueues[index][key].push(command);
	}

	CommandQueue* GetCommandQueue(size_t key)
	{
		size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
		return &m_frameQueues[prevIndex][key];
	}

	void SwapQueue()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

protected:
	std::unique_ptr<PipelineStateObject> m_pso{ nullptr };
	//CommandQueueMap m_commandQueueMap{}; //ī�޶� �� Ŀ��� ť
	FrameQueueArray m_frameQueues;
	Core::DelegateHandle m_swapEventHandle{};

	bool m_abled{ true };
	std::atomic_ullong m_frame{};
};