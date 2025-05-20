#pragma once
#include "../Texture.h"
#include "RenderModules.h"
#include "../IRenderPass.h"
#include "SpawnModuleCS.h"
#include "MovementModuleCS.h"
#include "LifeModuleCS.h"

// maxparticles
class ParticleSystem
{
public:
	ParticleSystem(int maxParticles = 1000);
	~ParticleSystem();

	template<typename T, typename... Args>
	T* AddModule(Args&&... args)
	{
		static_assert(std::is_base_of<ParticleModule, T>::value, "T must derive from ParticleModule");

		T* module = new T(std::forward<Args>(args)...);
		module->Initialize();
		m_moduleList.Link(module);
		return module;
	}

	template<typename T>
	T* GetModule()
	{
		static_assert(std::is_base_of<ParticleModule, T>::value, "T must derive from ParticleModule");

		for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
		{
			ParticleModule& module = *it;
			if (T* t = dynamic_cast<T*>(&module))
			{
				return t;
			}
		}
		return nullptr;
	}

	template<typename T, typename... Args>
	T* AddRenderModule(Args&&... args) {
		static_assert(std::is_base_of<RenderModules, T>::value,
			"T must be derived from RenderModules");

		T* module = new T(std::forward<Args>(args)...);
		module->Initialize();
		m_renderModules.push_back(module);
		return module;
	}

	template<typename T>
	T* GetRenderModule()
	{
		static_assert(std::is_base_of<RenderModules, T>::value, "T must derive from RenderModules");

		for (auto* module : m_renderModules)
		{
			if (T* t = dynamic_cast<T*>(module))
			{
				return t;
			}
		}
		return nullptr;
	}

	void Play();

	void Stop() { m_isRunning = false; }

	void Pause() { m_isPaused = true; }

	void Resume() { m_isPaused = false; }

	virtual void Update(float delta);

	virtual void Render(RenderScene& scene, Camera& camera);

	void SetPosition(const Mathf::Vector3& position);

	void ResizeParticleSystem(UINT newMaxParticles);

	void ReleaseBuffers();

	ID3D11ShaderResourceView* GetCurrentRenderingSRV() const;

private:

	void ConfigureModuleBuffers(ParticleModule& module, bool isFirstModule);

	void CreateParticleBuffer(UINT numParticles);


	
private:

	// 공유 버퍼 스왑용 정리
	void CreateSharedBuffers();

	void ReleaseSharedBuffer();

	void InitializeParticleIndices();

	void SwapIndexBuffer();   

	ID3D11Buffer* m_currentIndicesBuffer;
	ID3D11Buffer* m_nextIndicesBuffer;
	ID3D11UnorderedAccessView* m_currentIndicesUAV;
	ID3D11UnorderedAccessView* m_nextIndicesUAV;

	// 비활성 파티클 관리용 버퍼
	ID3D11Buffer* m_inactiveIndicesBuffer = nullptr;
	ID3D11Buffer* m_inactiveCountBuffer = nullptr;
	ID3D11Buffer* m_activeCountBuffer = nullptr;

	ID3D11UnorderedAccessView* m_inactiveIndicesUAV = nullptr;
	ID3D11UnorderedAccessView* m_inactiveCountUAV = nullptr;
	ID3D11UnorderedAccessView* m_activeCountUAV = nullptr;

protected:
	// 렌더 초기화 메소드는 rendermodule에서 정의.

	// data members
	bool m_isRunning;
	bool m_isPaused;
	std::vector<ParticleData> m_particleData;
	LinkedList<ParticleModule> m_moduleList;
	int m_activeParticleCount = 0;
	int m_maxParticles;
	std::vector<BillBoardInstanceData> m_instanceData;
	Mathf::Vector3 m_position;
	std::vector<RenderModules*> m_renderModules;

	// 더블 버퍼링을 위한 멤버들
	ID3D11Buffer* m_particleBufferA = nullptr;
	ID3D11Buffer* m_particleBufferB = nullptr;
	ID3D11UnorderedAccessView* m_particleUAV_A = nullptr;
	ID3D11UnorderedAccessView* m_particleUAV_B = nullptr;
	ID3D11ShaderResourceView* m_particleSRV_A = nullptr;
	ID3D11ShaderResourceView* m_particleSRV_B = nullptr;

	bool m_usingBufferA = true; // 현재 A 버퍼를 입력으로 사용 중인지 여부
};

