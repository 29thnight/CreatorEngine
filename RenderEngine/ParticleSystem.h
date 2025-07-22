#pragma once
#include "Texture.h"
#include "IRenderPass.h"
#include "SpawnModuleCS.h"
#include "MovementModuleCS.h"
//#include "LifeModuleCS.h"
#include "ColorModuleCS.h"
#include "SizeModuleCS.h"
#include "MeshSpawnModuleCS.h"
#include "BillboardModuleGPU.h"
#include "MeshModuleGPU.h"

enum class ParticleDataType
{
	None,
	Standard,    // 기존 ParticleData (112바이트)
	Mesh        // MeshParticleData (144바이트)
};

// maxparticles
class ParticleSystem
{
public:
	ParticleSystem(int maxParticles = 1, ParticleDataType dataType = ParticleDataType::Standard);
	~ParticleSystem();

	template<typename T, typename... Args>
	T* AddModule(Args&&... args)
	{
		static_assert(std::is_base_of<ParticleModule, T>::value, "T must derive from ParticleModule");

		T* module = new T(std::forward<Args>(args)...);
		module->Initialize();
		module->OnSystemResized(m_maxParticles);
		m_moduleList.Link(module);
		return module;
	}

	void AddExistingModule(std::unique_ptr<ParticleModule> module)
	{
		if (module)
		{
			ParticleModule* rawPtr = module.release(); // unique_ptr에서 소유권 해제
			rawPtr->Initialize();
			rawPtr->OnSystemResized(m_maxParticles);
			m_moduleList.Link(rawPtr);
		}
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
	T* AddRenderModule(Args&&... args) 
	{
		static_assert(std::is_base_of<RenderModules, T>::value,
			"T must be derived from RenderModules");

		T* module = new T(std::forward<Args>(args)...);
		module->Initialize();
		m_renderModules.push_back(module);
		return module;
	}

	void AddExistingRenderModule(std::unique_ptr<RenderModules> renderModule)
	{
		if (renderModule)
		{
			RenderModules* rawPtr = renderModule.release(); // unique_ptr에서 소유권 해제
			rawPtr->Initialize();
			m_renderModules.push_back(rawPtr);
		}
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

	virtual void Update(float delta);

	virtual void Render(RenderScene& scene, Camera& camera);

	void UpdateEffectBasePosition(const Mathf::Vector3& newBasePosition);

	void SetPosition(const Mathf::Vector3& position);

	Mathf::Vector3 GetWorldPosition() const {
		return m_effectBasePosition + m_position;
	}

	Mathf::Vector3 GetRelativePosition() const {
		return m_position;
	}

	Mathf::Vector3 GetEffectBasePosition() const {
		return m_effectBasePosition;
	}

	void SetRotation(const Mathf::Vector3& rotation);

	Mathf::Vector3 GetRotation() { return m_rotation; }

	void ResizeParticleSystem(UINT newMaxParticles);

	void SetParticleDatatype(ParticleDataType type);

	void ReleaseBuffers();

	void ReleaseParticleBuffers();

	ID3D11ShaderResourceView* GetCurrentRenderingSRV() const;

	LinkedList<ParticleModule>& GetModuleList() { return m_moduleList; }
	const LinkedList<ParticleModule>& GetModuleList() const { return m_moduleList; }

	std::vector<RenderModules*>& GetRenderModules() { return m_renderModules; }
	const std::vector<RenderModules*>& GetRenderModules() const { return m_renderModules; }

	// JSON 직렬화를 위한 getter들
	UINT GetMaxParticles() const { return m_maxParticles; }
	ParticleDataType GetParticleDataType() const { return m_particleDataType; }
	const Mathf::Vector3& GetPosition() const { return m_position; }
	bool IsRunning() const { return m_isRunning; }

	void SetEffectProgress(float progress);

	void SetShouldRender(bool shouldRender) { m_shouldRender = shouldRender; }

	void StopSpawning() { m_shouldSpawn = false; }

	void UpdateTransformOnly();

	void UpdateParticleSimulation(float delta);

	std::string m_name{};
private:

	void ConfigureModuleBuffers(ParticleModule& module, bool isFirstModule);

	void CreateParticleBuffer(UINT numParticles);

private:

	void InitializeParticleIndices();

	size_t GetParticleStructSize() const;

	ParticleDataType m_particleDataType = ParticleDataType::None;
	size_t m_particleStructSize;

protected:
	// 렌더 초기화 메소드는 rendermodule에서 정의.
	float m_effectProgress = 0.0f;
	bool m_isRunning;
	std::vector<ParticleData> m_particleData;
	LinkedList<ParticleModule> m_moduleList;
	int m_activeParticleCount = 0;
	int m_maxParticles;
	std::vector<BillBoardInstanceData> m_instanceData;
	Mathf::Vector3 m_position = { 0, 0, 0 };
	Mathf::Vector3 m_effectBasePosition = { 0, 0, 0 };
	Mathf::Vector3 m_rotation = { 0, 0, 0 };
	std::vector<RenderModules*> m_renderModules;

	// 더블 버퍼링을 위한 멤버들
	ID3D11Buffer* m_particleBufferA = nullptr;
	ID3D11Buffer* m_particleBufferB = nullptr;
	ID3D11UnorderedAccessView* m_particleUAV_A = nullptr;
	ID3D11UnorderedAccessView* m_particleUAV_B = nullptr;
	ID3D11ShaderResourceView* m_particleSRV_A = nullptr;
	ID3D11ShaderResourceView* m_particleSRV_B = nullptr;

	bool m_usingBufferA = true; // 현재 A 버퍼를 입력으로 사용 중인지 여부

	bool m_shouldRender = true;
	bool m_shouldSpawn = true;
};

using EmitterContainer = std::vector<std::shared_ptr<ParticleSystem>>;