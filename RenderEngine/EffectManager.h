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

		// RenderModule 활성화 상태
		bool billboardEnabled = false;
		bool meshEnabled = false;
	};
	
	// 원본 설정
	nlohmann::json originalJson;

	ModuleConfig moduleConfig;
	int maxParticles = 1000;
	ParticleDataType dataType = ParticleDataType::Standard;

	// 모듈별 데이터 (한 번만 파싱해서 저장)
	nlohmann::json spawnModuleData;
	nlohmann::json colorModuleData;
	nlohmann::json movementModuleData;
	nlohmann::json sizeModuleData;
	nlohmann::json billboardModuleData;
	nlohmann::json meshModuleData;

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

	EffectBase* GetEffectInstance(const std::string& instanceId);

	EffectBase* GetEffect(std::string_view instanceName);

	bool RemoveEffect(std::string_view instanceName);

	size_t GetPoolSize() const { return universalPool.size(); }
	size_t GetActiveEffectCount() const { return activeEffects.size(); }

	// 읽기만 effects에 접근은 오로지 매니저에서만
	const std::unordered_map<std::string, UniversalEffectTemplate> GetEffectTemplates() const { return templates; }

	UINT GetInstanceId() const { return nextInstanceId.load(); }

	void RegisterTemplateFromEditor(const std::string& effectName, const nlohmann::json& effectJson);

private:
	// 템플릿 설정들 (JSON에서 로드)
	std::unordered_map<std::string, UniversalEffectTemplate> templates;

	// 현재 활성화된 이펙트들
	std::unordered_map<std::string, std::unique_ptr<EffectBase>> activeEffects;

	// 유니버셜 풀 (모든 이펙트가 동일한 구조)
	std::queue<std::unique_ptr<EffectBase>> universalPool;

	// 인스턴스 ID 생성기
	std::atomic<UINT> nextInstanceId{ 1 };

	// 풀 설정
	static const int DEFAULT_POOL_SIZE = 50;  // 동시 이펙트 최대 개수
	static const int MAX_PARTICLES_PER_SYSTEM = 2000;  // 시스템당 최대 파티클 수

private:
	void InitializeUniversalPool();

	std::unique_ptr<EffectBase> CreateUniversalEffect();

	std::unique_ptr<EffectBase> AcquireFromPool();

	void ReturnToPool(std::unique_ptr<EffectBase> effect);

	void ConfigureInstance(EffectBase* effect, const UniversalEffectTemplate& templateConfig);

	void DisableAllModules(EffectBase* effect);
};

static inline auto EffectManagers = EffectManager::GetInstance();
