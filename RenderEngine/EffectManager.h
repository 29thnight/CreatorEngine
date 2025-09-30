#pragma once
#include "IRenderPass.h"
#include "DLLAcrossSingleton.h"
#include "EffectBase.h"
#include "ParticleSystem.h"

class UniversalEffectTemplate {
public:
	struct ModuleConfig {
		// ParticleModule 활성화 상태
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

		// RenderModule 활성화 상태
		bool billboardEnabled = false;
		bool meshEnabled = false;
		bool trailEnable = false;
	};
	
	struct ParticleSystemConfig {
		ModuleConfig moduleConfig;
		int maxParticles = 1000;
		ParticleDataType dataType = ParticleDataType::Standard;
	};

	// 원본 설정
	nlohmann::json originalJson;

	std::vector<ParticleSystemConfig> particleSystemConfigs;

	std::string name{};
	float duration = 1.0f;
	bool loop = false;
	float timeScale = 1.0f;

	// JSON에서 설정 로드
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

	// 읽기만 effects에 접근은 오로지 매니저에서만
	const std::unordered_map<std::string, UniversalEffectTemplate> GetEffectTemplates() const { return templates; }

	void RegisterTemplateFromEditor(const std::string& effectName, const nlohmann::json& effectJson);

	std::string ReplaceEffect(const std::string& instanceId, const std::string& newTemplateName);

	uint32_t GetSmartAvailableId(const std::string& templateName);

	bool GetTemplateSettings(const std::string& templateName,
		float& outTimeScale,
		bool& outLoop,
		float& outDuration);

private:
	// 템플릿 설정들 (JSON에서 로드)
	std::unordered_map<std::string, UniversalEffectTemplate> templates;

	// 현재 활성화된 이펙트들
	std::unordered_map<std::string, std::unique_ptr<EffectBase>> activeEffects;

	// 유니버셜 풀 (모든 이펙트가 동일한 구조)
	std::queue<std::unique_ptr<EffectBase>> universalPool;

	// 인스턴스 ID 생성기
	std::mutex smartIdMutex;


	// 풀 설정
	static const int DEFAULT_POOL_SIZE = 50;  // 동시 이펙트 최대 개수
	static const int MAX_PARTICLES_PER_SYSTEM = 10000;  // 시스템당 최대 파티클 수

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
