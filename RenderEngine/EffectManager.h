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
		bool meshColorEnabled = false;
		bool meshMovementEnabled = false;
		bool meshSizeEnabled = false;
		bool trailGenerateEnable = false;
		bool trailCSEnabled = false;

		// RenderModule Ȱ��ȭ ����
		bool billboardEnabled = false;
		bool meshEnabled = false;
		bool trailEnable = false;
	};
	
	struct ParticleSystemConfig {
		ModuleConfig moduleConfig;
		int maxParticles = 1000;
		ParticleDataType dataType = ParticleDataType::Standard;
	};

	// ���� ����
	nlohmann::json originalJson;

	std::vector<ParticleSystemConfig> particleSystemConfigs;

	std::string name{};
	float duration = 1.0f;
	bool loop = false;
	float timeScale = 1.0f;

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

	std::string PlayEffectWithCustomId(const std::string& templateName, const std::string& customInstanceId);

	EffectBase* GetEffectInstance(const std::string& instanceId);

	EffectBase* GetEffect(std::string_view instanceName);

	bool RemoveEffect(std::string_view instanceName);

	size_t GetPoolSize() const { return universalPool.size(); }
	size_t GetActiveEffectCount() const { return activeEffects.size(); }

	// �б⸸ effects�� ������ ������ �Ŵ���������
	const std::unordered_map<std::string, UniversalEffectTemplate> GetEffectTemplates() const { return templates; }

	void RegisterTemplateFromEditor(const std::string& effectName, const nlohmann::json& effectJson);

	std::string ReplaceEffect(const std::string& instanceId, const std::string& newTemplateName);

	uint32_t GetSmartAvailableId(const std::string& templateName);

	bool GetTemplateSettings(const std::string& templateName,
		float& outTimeScale,
		bool& outLoop,
		float& outDuration);

private:
	// ���ø� ������ (JSON���� �ε�)
	std::unordered_map<std::string, UniversalEffectTemplate> templates;

	// ���� Ȱ��ȭ�� ����Ʈ��
	std::unordered_map<std::string, std::unique_ptr<EffectBase>> activeEffects;

	// ���Ϲ��� Ǯ (��� ����Ʈ�� ������ ����)
	std::queue<std::unique_ptr<EffectBase>> universalPool;

	// �ν��Ͻ� ID ������
	std::mutex smartIdMutex;


	// Ǯ ����
	static const int DEFAULT_POOL_SIZE = 50;  // ���� ����Ʈ �ִ� ����
	static const int MAX_PARTICLES_PER_SYSTEM = 10000;  // �ý��۴� �ִ� ��ƼŬ ��

	std::queue<std::unique_ptr<EffectBase>> cleanupQueue;
	std::mutex cleanupQueueMutex;

private:
	void InitializeUniversalPool();

	std::unique_ptr<EffectBase> CreateUniversalEffect();

	std::unique_ptr<EffectBase> AcquireFromPool();

	void ReturnToPool(std::unique_ptr<EffectBase> effect);

	void ConfigureInstance(EffectBase* effect, const UniversalEffectTemplate& templateConfig);

	void DisableAllModules(EffectBase* effect);
};

static inline auto EffectManagers = EffectManager::GetInstance();
