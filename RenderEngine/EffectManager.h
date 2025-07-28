#pragma once
#include "IRenderPass.h"
#include "DLLAcrossSingleton.h"
#include "EffectBase.h"
#include "ParticleSystem.h"

class UniversalEffectTemplate {
public:
	struct ModuleConfig {
		// ParticleModule Ȱ��ȭ ����
		bool spawnEnabled = false;
		bool colorEnabled = false;
		bool movementEnabled = false;
		bool sizeEnabled = false;
		bool meshSpawnEnabled = false;

		// RenderModule Ȱ��ȭ ����
		bool billboardEnabled = false;
		bool meshEnabled = false;
	};
	
	// ���� ����
	nlohmann::json originalJson;

	ModuleConfig moduleConfig;
	int maxParticles = 1000;
	ParticleDataType dataType = ParticleDataType::Standard;

	// ��⺰ ������ (�� ���� �Ľ��ؼ� ����)
	nlohmann::json spawnModuleData;
	nlohmann::json colorModuleData;
	nlohmann::json movementModuleData;
	nlohmann::json sizeModuleData;
	nlohmann::json billboardModuleData;
	nlohmann::json meshModuleData;

	// JSON���� ���� �ε�
	void LoadConfigFromJSON(const nlohmann::json& effectJson);
};

class EffectManager : public IRenderPass, public DLLCore::Singleton<EffectManager>
{
private:
	friend class DLLCore::Singleton<EffectManager>;
	EffectManager() = default;
	~EffectManager() = default;

public:
	void Initialize();

	virtual void Execute(RenderScene& scene, Camera& camera);

	virtual void Render(RenderScene& scene, Camera& camera) {};

	void Update(float delta);

	std::string PlayEffect(const std::string& templateName);

	EffectBase* GetEffectInstance(const std::string& instanceId);

	EffectBase* GetEffect(std::string_view instanceName);

	bool RemoveEffect(std::string_view instanceName);

	size_t GetPoolSize() const { return universalPool.size(); }
	size_t GetActiveEffectCount() const { return activeEffects.size(); }

	// �б⸸ effects�� ������ ������ �Ŵ���������
	const std::unordered_map<std::string, UniversalEffectTemplate> GetEffectTemplates() const { return templates; }

	UINT GetInstanceId() const { return nextInstanceId.load(); }

	void RegisterTemplateFromEditor(const std::string& effectName, const nlohmann::json& effectJson);

private:
	// ���ø� ������ (JSON���� �ε�)
	std::unordered_map<std::string, UniversalEffectTemplate> templates;

	// ���� Ȱ��ȭ�� ����Ʈ��
	std::unordered_map<std::string, std::unique_ptr<EffectBase>> activeEffects;

	// ���Ϲ��� Ǯ (��� ����Ʈ�� ������ ����)
	std::queue<std::unique_ptr<EffectBase>> universalPool;

	// �ν��Ͻ� ID ������
	std::atomic<UINT> nextInstanceId{ 1 };

	// Ǯ ����
	static const int DEFAULT_POOL_SIZE = 50;  // ���� ����Ʈ �ִ� ����
	static const int MAX_PARTICLES_PER_SYSTEM = 2000;  // �ý��۴� �ִ� ��ƼŬ ��

private:
	void InitializeUniversalPool();

	std::unique_ptr<EffectBase> CreateUniversalEffect();

	std::unique_ptr<EffectBase> AcquireFromPool();

	void ReturnToPool(std::unique_ptr<EffectBase> effect);

	void ConfigureInstance(EffectBase* effect, const UniversalEffectTemplate& templateConfig);

	void DisableAllModules(EffectBase* effect);
};

static inline auto EffectManagers = EffectManager::GetInstance();
