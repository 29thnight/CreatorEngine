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
	Standard,    // ���� ParticleData (112����Ʈ)
	Mesh        // MeshParticleData (144����Ʈ)
};

// maxparticles
class ParticleSystem
{
public:
	ParticleSystem(int maxParticles = 1000, ParticleDataType dataType = ParticleDataType::Standard);
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
			ParticleModule* rawPtr = module.release(); // unique_ptr���� ������ ����
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
			RenderModules* rawPtr = renderModule.release(); // unique_ptr���� ������ ����
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

	void SetPosition(const Mathf::Vector3& position);

	Mathf::Vector3 GetPosition() { return m_position; }

	void ResizeParticleSystem(UINT newMaxParticles);

	void ReleaseBuffers();

	void ReleaseParticleBuffers();

	ID3D11ShaderResourceView* GetCurrentRenderingSRV() const;

	LinkedList<ParticleModule>& GetModuleList() { return m_moduleList; }
	const LinkedList<ParticleModule>& GetModuleList() const { return m_moduleList; }

	std::vector<RenderModules*>& GetRenderModules() { return m_renderModules; }
	const std::vector<RenderModules*>& GetRenderModules() const { return m_renderModules; }

	// JSON ����ȭ�� ���� getter��
	UINT GetMaxParticles() const { return m_maxParticles; }
	ParticleDataType GetParticleDataType() const { return m_particleDataType; }
	const Mathf::Vector3& GetPosition() const { return m_position; }
	bool IsRunning() const { return m_isRunning; }

private:

	void ConfigureModuleBuffers(ParticleModule& module, bool isFirstModule);

	void CreateParticleBuffer(UINT numParticles);

private:

	void InitializeParticleIndices();

	void SetParticleDatatype(ParticleDataType type);

	size_t GetParticleStructSize() const;

	ParticleDataType m_particleDataType = ParticleDataType::None;
	size_t m_particleStructSize;

protected:
	// ���� �ʱ�ȭ �޼ҵ�� rendermodule���� ����.

	bool m_isRunning;
	std::vector<ParticleData> m_particleData;
	LinkedList<ParticleModule> m_moduleList;
	int m_activeParticleCount = 0;
	int m_maxParticles;
	std::vector<BillBoardInstanceData> m_instanceData;
	Mathf::Vector3 m_position = { 0, 0, 0 };
	std::vector<RenderModules*> m_renderModules;

	// ���� ���۸��� ���� �����
	ID3D11Buffer* m_particleBufferA = nullptr;
	ID3D11Buffer* m_particleBufferB = nullptr;
	ID3D11UnorderedAccessView* m_particleUAV_A = nullptr;
	ID3D11UnorderedAccessView* m_particleUAV_B = nullptr;
	ID3D11ShaderResourceView* m_particleSRV_A = nullptr;
	ID3D11ShaderResourceView* m_particleSRV_B = nullptr;

	bool m_usingBufferA = true; // ���� A ���۸� �Է����� ��� ������ ����
};

