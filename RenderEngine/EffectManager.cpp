#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "ImGuiRegister.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "EffectProxyController.h"
#include "EffectSerializer.h"
#include "Profiler.h"

void EffectManager::Initialize()
{
	std::filesystem::path effectPath = PathFinder::Relative("Effect\\");

	// 디렉토리 존재여부
	if (!std::filesystem::exists(effectPath) || !std::filesystem::is_directory(effectPath)) {
		std::cout << "Effect folder does not exist" << '\n';
		return;
	}

	try {
		for (const auto& entry : std::filesystem::directory_iterator(effectPath)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				try {
					std::ifstream file(entry.path());
					if (file.is_open()) {
						nlohmann::json effectJson;
						file >> effectJson;
						file.close();

						std::string effectName = entry.path().stem().string();
						UniversalEffectTemplate templateConfig;
						templateConfig.LoadConfigFromJSON(effectJson);
						templates[effectName] = templateConfig;

						std::cout << "Loaded template config: " << effectName << std::endl;
					}
				}
				catch (const std::exception& e) {
					std::cerr << "Error loading effect: " << entry.path() << " - " << e.what() << std::endl;
				}
			}
		}
	}
	catch (const std::filesystem::filesystem_error& e) {
		std::cout << "Effect json does not exist" << '\n';
		return;
	}

	InitializeUniversalPool();
}

void EffectManager::Execute(RenderScene& scene, Camera& camera)
{
	EffectProxyController::GetInstance()->ExecuteEffectCommands();

	std::vector<EffectBase*> activeEffectList;
	activeEffectList.reserve(activeEffects.size());

	for (auto& [key, effect] : activeEffects) {
		if (effect->GetState() != EffectState::Stopped) {
			activeEffectList.push_back(effect.get());
		}
	}

	// 렌더링 전에 한 번만 공통 상태 설정
	if (!activeEffectList.empty()) {
		for (auto* effect : activeEffectList) {
			effect->Render(scene, camera);
		}
	}
}

void EffectManager::Update(float delta)
{
	// 정리 큐 처리 (매 프레임마다)
	ProcessCleanupQueue();

	// 강제 정리 필요한지 확인
	if (ShouldForceCleanup()) {
		ForceCleanupOldEffects();
	}

	auto it = activeEffects.begin();
	while (it != activeEffects.end()) {
		auto& effect = it->second;
		effect->Update(delta);

		// Stop 상태인 이펙트만 제거
		if (effect->GetState() == EffectState::Stopped) {
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);
			ReturnToPool(std::move(effectToReturn));
		}
		else {
			++it;
		}
	}
}

std::string EffectManager::PlayEffect(const std::string& templateName)
{
	// 활성 이펙트 개수 체크
	if (activeEffects.size() >= MAX_ACTIVE_EFFECTS) {
		std::cout << "Cannot create effect: Active effect limit reached ("
			<< MAX_ACTIVE_EFFECTS << ")" << std::endl;
		return "";  // 생성 거부
	}

	auto templateIt = templates.find(templateName);
	if (templateIt == templates.end()) {
		return "";
	}

	auto instance = AcquireFromPool();
	if (!instance) {
		std::cerr << "Pool exhausted! Cannot play effect: " << templateName << std::endl;
		return "";
	}

	ConfigureInstance(instance.get(), templateIt->second);

	// 스마트 ID 할당 시스템 사용
	uint32_t currentId = GetSmartAvailableId(templateName);
	std::string instanceId = templateName + "_" + std::to_string(currentId);

	instance->Play();
	activeEffects[instanceId] = std::move(instance);

	return instanceId;
}

std::string EffectManager::PlayEffectWithCustomId(const std::string& templateName, const std::string& customInstanceId)
{
	auto templateIt = templates.find(templateName);
	if (templateIt == templates.end()) {
		return "";
	}

	// 기존에 같은 ID가 있으면 먼저 제거
	RemoveEffect(customInstanceId);

	auto instance = AcquireFromPool();
	if (!instance) {
		std::cerr << "Pool exhausted! Cannot play effect: " << templateName << std::endl;
		return "";
	}

	ConfigureInstance(instance.get(), templateIt->second);

	instance->Play();
	activeEffects[customInstanceId] = std::move(instance);

	std::cout << "Created effect with ID: " << customInstanceId << " (template: " << templateName << ")" << std::endl;
	return customInstanceId;
}

EffectBase* EffectManager::GetEffectInstance(const std::string& instanceId)
{
	auto it = activeEffects.find(instanceId);
	return (it != activeEffects.end()) ? it->second.get() : nullptr;
}


EffectBase* EffectManager::GetEffect(std::string_view instanceName)
{
	auto it = activeEffects.find(instanceName.data());
	if (it != activeEffects.end()) {
		return it->second.get();
	}
	return nullptr;
}

bool EffectManager::RemoveEffect(std::string_view instanceName)
{
	auto it = activeEffects.find(instanceName.data());
	if (it != activeEffects.end()) {
		auto effectToReturn = std::move(it->second);
		activeEffects.erase(it);

		// 수명이 없는 이펙트도 풀에 반환하도록 수정
		ReturnToPool(std::move(effectToReturn));
		return true;
	}
	return false;
}

bool EffectManager::IsPoolHealthy() const
{
	std::lock_guard<std::mutex> lock(poolMutex);

	bool sizeOk = universalPool.size() <= maxPoolSize;
	bool activeOk = activeEffects.size() <= MAX_ACTIVE_EFFECTS;
	bool cleanupOk = cleanupQueue.size() < 50; // 정리 큐도 너무 크면 안됨

	return sizeOk && activeOk && cleanupOk;
}

void EffectManager::ForceCleanupOldEffects()
{
	if (isCleanupRunning.exchange(true)) {
		return; // 이미 정리 중이면 스킵
	}

	std::cout << "Starting force cleanup of old effects..." << std::endl;

	auto it = activeEffects.begin();
	int cleanedCount = 0;

	while (it != activeEffects.end() && cleanedCount < 10) { // 한 번에 최대 10개만
		auto& effect = it->second;

		if (effect->GetCurrentTime() > FORCE_CLEANUP_TIME &&
			effect->GetState() != EffectState::Stopped) {

			effect->Stop();
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);
			ReturnToPool(std::move(effectToReturn));
			cleanedCount++;
		}
		else {
			++it;
		}
	}

	lastCleanupTime = std::chrono::steady_clock::now();
	isCleanupRunning = false;

	std::cout << "Force cleanup completed. Cleaned " << cleanedCount << " effects" << std::endl;
}

void EffectManager::SetMaxPoolSize(int maxSize)
{
	if (maxSize < 10) {
		std::cerr << "Warning: MaxPoolSize too small, setting to minimum (10)" << std::endl;
		maxSize = 10;
	}

	if (maxSize > 500) {
		std::cerr << "Warning: MaxPoolSize too large, setting to maximum (500)" << std::endl;
		maxSize = 500;
	}

	int oldSize = maxPoolSize;
	maxPoolSize = maxSize;

	// 현재 풀 크기가 새 제한보다 크면 축소
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		while (universalPool.size() > maxPoolSize && !universalPool.empty()) {
			universalPool.pop();
			totalDestroyedEffects++;
		}
	}

	std::cout << "MaxPoolSize changed from " << oldSize << " to " << maxPoolSize << std::endl;
	std::cout << "Current pool size: " << GetPoolSize() << std::endl;
}

size_t EffectManager::GetTotalMemoryUsage() const
{
	size_t totalMemory = 0;

	// 활성 이펙트 메모리 계산 (대략적인 추정)
	totalMemory += activeEffects.size() * sizeof(EffectBase);

	// 각 이펙트의 파티클 시스템 메모리 계산
	for (const auto& [id, effect] : activeEffects) {
		const auto& particleSystems = effect->GetAllParticleSystems();
		for (const auto& ps : particleSystems) {
			// 파티클 시스템당 대략적인 메모리 사용량
			totalMemory += ps->GetMaxParticles() * 256; // 파티클당 대략 256바이트 추정
		}
	}

	// 풀 메모리 계산
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		totalMemory += universalPool.size() * sizeof(EffectBase);
		totalMemory += universalPool.size() * MAX_PARTICLES_PER_SYSTEM * 256; // 풀의 각 이펙트
	}

	// 정리 큐 메모리
	{
		std::lock_guard<std::mutex> lock(cleanupQueueMutex);
		totalMemory += cleanupQueue.size() * sizeof(EffectBase);
	}

	return totalMemory;
}

void EffectManager::PrintPoolStatistics() const
{
	std::cout << "=== EffectManager Pool Statistics ===" << std::endl;
	std::cout << "Pool size: " << GetPoolSize() << "/" << maxPoolSize << std::endl;
	std::cout << "Active effects: " << activeEffects.size() << "/" << MAX_ACTIVE_EFFECTS << std::endl;
	std::cout << "Cleanup queue: " << cleanupQueue.size() << std::endl;
	std::cout << "Total created: " << totalCreatedEffects.load() << std::endl;
	std::cout << "Total destroyed: " << totalDestroyedEffects.load() << std::endl;
	std::cout << "Pool healthy: " << (IsPoolHealthy() ? "Yes" : "No") << std::endl;
	std::cout << "====================================" << std::endl;
}

void EffectManager::RegisterTemplateFromEditor(const std::string& effectName, const nlohmann::json& effectJson)
{
	UniversalEffectTemplate templateConfig;
	templateConfig.LoadConfigFromJSON(effectJson);
	templates[effectName] = templateConfig;
	std::cout << "Runtime template registered: " << effectName << std::endl;
}

std::string EffectManager::ReplaceEffect(const std::string& instanceId, const std::string& newTemplateName)
{
	auto templateIt = templates.find(newTemplateName);
	if (templateIt == templates.end()) {
		return "";
	}

	auto it = activeEffects.find(instanceId);
	if (it != activeEffects.end()) {
		// 기존 인스턴스를 재설정 (삭제/생성 없음)
		auto& effect = it->second;

		// 이펙트 정지
		effect->Stop();

		// 새 템플릿 설정 적용
		ConfigureInstance(effect.get(), templateIt->second);

		// 재생 시작
		effect->Play();

		return instanceId; // 같은 ID 반환
	}

	// 기존 인스턴스가 없으면 새로 생성
	return PlayEffectWithCustomId(newTemplateName, instanceId);
}

uint32_t EffectManager::GetSmartAvailableId(const std::string& templateName)
{
	std::lock_guard<std::mutex> lock(smartIdMutex);

	// 현재 활성화된 이펙트들에서 사용 중인 ID 수집
	std::set<uint32_t> usedIds;

	for (const auto& [instanceName, effect] : activeEffects) {
		size_t underscorePos = instanceName.find_last_of('_');
		if (underscorePos != std::string::npos) {
			try {
				uint32_t id = std::stoul(instanceName.substr(underscorePos + 1));
				usedIds.insert(id);
			}
			catch (const std::exception&) {
				// 파싱 실패시 무시
			}
		}
	}

	// 가장 작은 사용 가능한 ID 찾기
	uint32_t availableId = 1;
	while (usedIds.find(availableId) != usedIds.end()) {
		availableId++;
	}

	std::cout << "Smart ID assignment: " << templateName << "_" << availableId << std::endl;
	return availableId;
}

// 풀 관련 **************************************************************************************************************************************************

void EffectManager::InitializeUniversalPool()
{
	for (int i = 0; i < DEFAULT_POOL_SIZE; ++i) {
		auto effect = CreateUniversalEffect();
		if (effect) {
			universalPool.push(std::move(effect));
		}
	}

	std::cout << "Universal pool initialized with " << DEFAULT_POOL_SIZE << " instances" << std::endl;
}

std::unique_ptr<EffectBase> EffectManager::AcquireFromPool()
{
	std::lock_guard<std::mutex> lock(poolMutex);

	// 풀이 비어있으면 새로 생성
	if (universalPool.empty()) {
		std::cout << "Pool empty, creating new effect instance" << std::endl;
		totalCreatedEffects++;
		auto newEffect = CreateUniversalEffect();
		if (newEffect) {
			return newEffect;
		}
		return nullptr;
	}

	auto instance = std::move(universalPool.front());
	universalPool.pop();

	// 재사용 준비 상태 확인
	if (!instance->IsReadyForReuse()) {
		std::cerr << "Warning: Pool instance not ready for reuse!" << std::endl;
		QueueForCleanup(std::move(instance));
		totalCreatedEffects++;
		return CreateUniversalEffect();
	}

	std::cout << "Acquired from pool. Remaining pool size: " << universalPool.size() << std::endl;
	return instance;
}

void EffectManager::ReturnToPool(std::unique_ptr<EffectBase> effect)
{
	if (!effect) return;

	// 1. 논리적 정리
	if (effect->GetState() != EffectState::Stopped) {
		effect->Stop();
	}

	// 2. 논리적 리셋
	effect->ResetForReuse();

	std::lock_guard<std::mutex> lock(poolMutex);

	// 3. 풀 크기 제한 확인
	if (universalPool.size() >= maxPoolSize) {
		std::cout << "Pool full (" << maxPoolSize << "), destroying effect instance" << std::endl;
		totalDestroyedEffects++;
		return; // effect는 자동으로 소멸됨
	}

	// 4. 풀에 반환
	if (effect->IsReadyForReuse()) {
		universalPool.push(std::move(effect));
		std::cout << "Effect returned to pool. Pool size: " << universalPool.size() << std::endl;
	}
	else {
		std::cerr << "Warning: Effect not ready for reuse, queueing for cleanup" << std::endl;
		QueueForCleanup(std::move(effect));
	}
}

//***********************************************************************************************************************************************************

bool EffectManager::GetTemplateSettings(const std::string& templateName, float& outTimeScale, bool& outLoop, float& outDuration)
{
	auto templateIt = templates.find(templateName);
	if (templateIt != templates.end()) {
		const auto& templateConfig = templateIt->second;

		outTimeScale = templateConfig.timeScale;
		outLoop = templateConfig.loop;
		outDuration = templateConfig.duration;
		return true;
	}
	return false;
}

bool EffectManager::IsAlive(const std::string& customInstanceId)
{
	auto it = activeEffects.find(customInstanceId);
	if (it != activeEffects.end()) {
		return it->second->GetState() != EffectState::Stopped;
	}
	return false;
}

std::unique_ptr<EffectBase> EffectManager::CreateUniversalEffect()
{
	auto effect = std::make_unique<EffectBase>();

	// ParticleSystem 생성 (최대 구성)
	auto particleSystem = std::make_shared<ParticleSystem>(
		MAX_PARTICLES_PER_SYSTEM,
		ParticleDataType::Mesh
	);

	// 모든 ParticleModule 추가 및 초기화
	auto spawnModule = particleSystem->AddModule<SpawnModuleCS>();
	spawnModule->Initialize(); // PSO 생성
	spawnModule->SetEnabled(false); // 비활성화

	auto colorModule = particleSystem->AddModule<ColorModuleCS>();
	colorModule->Initialize(); // PSO 생성
	colorModule->SetEnabled(false);

	auto movementModule = particleSystem->AddModule<MovementModuleCS>();
	movementModule->Initialize(); // PSO 생성
	movementModule->SetEnabled(false);

	auto sizeModule = particleSystem->AddModule<SizeModuleCS>();
	sizeModule->Initialize(); // PSO 생성
	sizeModule->SetEnabled(false);

	auto trailModule = particleSystem->AddModule<TrailModuleCS>();
	trailModule->Initialize();
	trailModule->SetEnabled(false);

	auto meshSpawnModule = particleSystem->AddModule<MeshSpawnModuleCS>();
	meshSpawnModule->Initialize();
	meshSpawnModule->SetEnabled(false);

	auto meshColorModule = particleSystem->AddModule<MeshColorModuleCS>();
	meshColorModule->Initialize();
	meshColorModule->SetEnabled(false);

	auto meshMovementModule = particleSystem->AddModule<MeshMovementModuleCS>();
	meshMovementModule->Initialize();
	meshMovementModule->SetEnabled(false);

	auto MeshSizeModule = particleSystem->AddModule<MeshSizeModuleCS>();
	MeshSizeModule->Initialize();
	MeshSizeModule->SetEnabled(false);

	auto trailGenerateModule = particleSystem->AddModule<TrailGenerateModule>();
	trailGenerateModule->Initialize();
	trailGenerateModule->SetEnabled(false);
	
	// RenderModule도 초기화
	auto billboardModule = particleSystem->AddRenderModule<BillboardModuleGPU>();
	billboardModule->Initialize(); // PSO 생성
	billboardModule->SetEnabled(false);

	auto meshModule = particleSystem->AddRenderModule<MeshModuleGPU>();
	meshModule->Initialize(); // PSO 생성
	meshModule->SetEnabled(false);

	auto trailRenderModule = particleSystem->AddRenderModule<TrailRenderModule>();
	trailRenderModule->Initialize();
	trailRenderModule->SetEnabled(false);

	effect->AddParticleSystem(particleSystem);
	return effect;
}

void EffectManager::DisableAllModules(EffectBase* effect)
{
	for (auto& ps : effect->GetAllParticleSystems()) {
		// ParticleModule들 비활성화
		for (auto it = ps->GetModuleList().begin(); it != ps->GetModuleList().end(); ++it) {
			ParticleModule& module = *it;
			module.SetEnabled(false);
		}

		// RenderModule들 비활성화
		for (auto* renderModule : ps->GetRenderModules()) {
			renderModule->SetEnabled(false);
		}
	}
}

void EffectManager::CleanupAllResources()
{
	std::cout << "Cleaning up all EffectManager resources..." << std::endl;

	// 1. 활성 이펙트들 정리
	activeEffects.clear();

	// 2. 풀 정리
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		while (!universalPool.empty()) {
			universalPool.pop();
		}
	}

	// 3. 정리 큐 정리
	{
		std::lock_guard<std::mutex> lock(cleanupQueueMutex);
		while (!cleanupQueue.empty()) {
			cleanupQueue.pop();
		}
	}

	std::cout << "EffectManager cleanup completed. Total created: " << totalCreatedEffects.load()
		<< ", Total destroyed: " << totalDestroyedEffects.load() << std::endl;
}

void EffectManager::ProcessCleanupQueue()
{
	std::lock_guard<std::mutex> lock(cleanupQueueMutex);

	// 한 번에 최대 5개씩만 처리 (프레임 드롭 방지)
	int processCount = 0;
	while (!cleanupQueue.empty() && processCount < 5) {
		auto effect = std::move(cleanupQueue.front());
		cleanupQueue.pop();
		totalDestroyedEffects++;
		processCount++;
	}

	// 정리 큐가 너무 크면 비동기 처리 시작
	if (cleanupQueue.size() > 20) {
		std::cout << "Cleanup queue too large (" << cleanupQueue.size()
			<< "), starting async cleanup" << std::endl;
		ProcessCleanupQueueAsync();
	}
}

void EffectManager::QueueForCleanup(std::unique_ptr<EffectBase> effect)
{
	if (!effect) return;

	std::lock_guard<std::mutex> lock(cleanupQueueMutex);
	cleanupQueue.push(std::move(effect));
}

void EffectManager::EmergencyCleanup()
{
	std::cout << "EMERGENCY CLEANUP INITIATED!" << std::endl;

	// 1. 모든 비필수 활성 이펙트 즉시 정지
	auto it = activeEffects.begin();
	int emergencyCleanedCount = 0;

	while (it != activeEffects.end() && emergencyCleanedCount < activeEffects.size() / 2) {
		auto& effect = it->second;

		// 오래된 이펙트나 무한 이펙트 우선 정리
		if (effect->GetCurrentTime() > 5.0f || effect->GetDuration() < 0) {
			effect->Stop();
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);

			// 긴급상황이므로 풀에 반환하지 않고 즉시 소멸
			totalDestroyedEffects++;
			emergencyCleanedCount++;
		}
		else {
			++it;
		}
	}

	// 2. 풀 크기 강제 축소
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		int targetPoolSize = maxPoolSize / 4; // 25%로 축소

		while (universalPool.size() > targetPoolSize && !universalPool.empty()) {
			universalPool.pop();
			totalDestroyedEffects++;
		}
	}

	// 3. 정리 큐 강제 비우기
	{
		std::lock_guard<std::mutex> lock(cleanupQueueMutex);
		while (!cleanupQueue.empty()) {
			cleanupQueue.pop();
			totalDestroyedEffects++;
		}
	}

	std::cout << "EMERGENCY CLEANUP COMPLETED! Cleaned " << emergencyCleanedCount
		<< " active effects and reduced pool size" << std::endl;
}

bool EffectManager::ShouldForceCleanup() const
{
	auto now = std::chrono::steady_clock::now();
	auto timeSinceLastCleanup = std::chrono::duration_cast<std::chrono::seconds>(now - lastCleanupTime).count();

	bool memoryPressure = (activeEffects.size() > MAX_ACTIVE_EFFECTS * 0.8f);
	bool timeForCleanup = (timeSinceLastCleanup > 60);
	bool emergencyNeeded = (activeEffects.size() > MAX_ACTIVE_EFFECTS * 0.95f) ||
		(GetTotalMemoryUsage() > 500 * 1024 * 1024); // 500MB 초과시

	if (emergencyNeeded) {
		const_cast<EffectManager*>(this)->EmergencyCleanup();
		return false; // 긴급정리 했으므로 일반 정리는 스킵
	}

	return memoryPressure || timeForCleanup;
}

void EffectManager::ProcessCleanupQueueAsync()
{
// 별도 스레드에서 정리 작업 수행
	std::thread([this]() {
		if (isCleanupRunning.exchange(true)) {
			return; // 이미 정리 중이면 종료
		}

		std::cout << "Starting async cleanup..." << std::endl;

		while (true) {
			std::unique_ptr<EffectBase> effect = nullptr;

			{
				std::lock_guard<std::mutex> lock(cleanupQueueMutex);
				if (cleanupQueue.empty()) {
					break;
				}
				effect = std::move(cleanupQueue.front());
				cleanupQueue.pop();
			}

			// effect 소멸 (시간이 걸릴 수 있는 작업)
			if (effect) {
				totalDestroyedEffects++;
				std::this_thread::sleep_for(std::chrono::milliseconds(1)); // CPU 양보
			}
		}

		isCleanupRunning = false;
		std::cout << "Async cleanup completed" << std::endl;
		}).detach();
}

void UniversalEffectTemplate::LoadConfigFromJSON(const nlohmann::json& effectJson)
{
	// 기본값으로 초기화
	particleSystemConfigs.clear();
	name = "";
	duration = 1.0f;
	loop = false;

	// JSON 원본 저장
	originalJson = effectJson;

	try {
		if (effectJson.contains("name")) {
			name = effectJson["name"];
		}
		if (effectJson.contains("duration")) {
			duration = effectJson["duration"];
		}
		if (effectJson.contains("loop")) {
			loop = effectJson["loop"];
		}
		if (effectJson.contains("timeScale")) {
			timeScale = effectJson["timeScale"];
		}

		if (effectJson.contains("emitterTimings") && effectJson["emitterTimings"].is_array()) {
			for (const auto& delay : effectJson["emitterTimings"]) {
				emitterDelays.push_back(delay.get<float>());
			}
		}

		if (effectJson.contains("particleSystems") && effectJson["particleSystems"].is_array()) {
			for (const auto& psJson : effectJson["particleSystems"]) {
				ParticleSystemConfig psConfig;

				// maxParticles, dataType 설정
				if (psJson.contains("maxParticles")) {
					psConfig.maxParticles = (int)psJson["maxParticles"];
				}
				if (psJson.contains("particleDataType")) {
					psConfig.dataType = static_cast<ParticleDataType>(psJson["particleDataType"]);
				}

				// 모듈 활성화 플래그만 설정
				if (psJson.contains("modules")) {
					for (const auto& moduleJson : psJson["modules"]) {
						if (moduleJson.contains("type")) {
							std::string moduleType = moduleJson["type"];

							if (moduleType == "SpawnModuleCS") {
								psConfig.moduleConfig.spawnEnabled = true;
							}
							else if (moduleType == "ColorModuleCS") {
								psConfig.moduleConfig.colorEnabled = true;
							}
							else if (moduleType == "MovementModuleCS") {
								psConfig.moduleConfig.movementEnabled = true;
							}
							else if (moduleType == "SizeModuleCS") {
								psConfig.moduleConfig.sizeEnabled = true;
							}
							else if (moduleType == "MeshSpawnModuleCS") {
								psConfig.moduleConfig.meshSpawnEnabled = true;
							}
							else if (moduleType == "MeshColorModuleCS") {
								psConfig.moduleConfig.meshColorEnabled = true;
							}
							else if (moduleType == "MeshMovementModuleCS") {
								psConfig.moduleConfig.meshMovementEnabled = true;
							}
							else if (moduleType == "MeshSizeModuleCS") {
								psConfig.moduleConfig.meshSizeEnabled = true;
							}
							else if (moduleType == "TrailGenerateModule") {
								psConfig.moduleConfig.trailGenerateEnable = true;
							}
							else if (moduleType == "TrailGenerateModule") {
								psConfig.moduleConfig.trailCSEnabled = true;
							}
						}
					}
				}

				// 렌더 모듈도 동일하게
				if (psJson.contains("renderModules")) {
					for (const auto& renderModuleJson : psJson["renderModules"]) {
						if (renderModuleJson.contains("type")) {
							std::string renderModuleType = renderModuleJson["type"];

							if (renderModuleType == "BillboardModuleGPU") {
								psConfig.moduleConfig.billboardEnabled = true;
							}
							else if (renderModuleType == "MeshModuleGPU") {
								psConfig.moduleConfig.meshEnabled = true;
							}
							else if (renderModuleType == "TrailRenderModule") {
								psConfig.moduleConfig.trailEnable = true;
							}
						}
					}
				}

				particleSystemConfigs.push_back(psConfig);
			}
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Error parsing effect JSON: " << e.what() << std::endl;
	}
}

void EffectManager::ConfigureInstance(EffectBase* effect, const UniversalEffectTemplate& templateConfig)
{
	// EffectSerializer로 완전한 이펙트 구조 생성
	auto deserializedEffect = EffectSerializer::DeserializeEffect(templateConfig.originalJson);
	if (deserializedEffect) {
		// 기본 이펙트 설정 복사
		effect->SetDuration(deserializedEffect->GetDuration());
		effect->SetLoop(deserializedEffect->IsLooping());
		effect->SetName(deserializedEffect->GetName());
		effect->SetPosition(deserializedEffect->GetPosition());
		effect->SetTimeScale(deserializedEffect->GetTimeScale());

		// ParticleSystem 교체
		effect->ClearParticleSystems();
		for (size_t i = 0; i < deserializedEffect->GetAllParticleSystems().size(); ++i) {
			const auto& ps = deserializedEffect->GetAllParticleSystems()[i];
			effect->AddParticleSystem(ps);

			// 딜레이 정보 복원
			float delay = deserializedEffect->GetEmitterStartDelay(i);
			effect->SetEmitterStartDelay(i, delay);
		}
	}

	// 모든 ParticleSystem에 대해 모듈 비활성화
	DisableAllModules(effect);

	// 각 ParticleSystem별로 개별 설정 적용
	auto& particleSystems = effect->GetAllParticleSystems();
	for (int i = 0; i < particleSystems.size() && i < templateConfig.particleSystemConfigs.size(); ++i) {
		auto& ps = particleSystems[i];
		const auto& psConfig = templateConfig.particleSystemConfigs[i];

		// 해당 ParticleSystem의 모듈 활성화
		if (psConfig.moduleConfig.spawnEnabled) {
			if (auto* module = ps->GetModule<SpawnModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.colorEnabled) {
			if (auto* module = ps->GetModule<ColorModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.movementEnabled) {
			if (auto* module = ps->GetModule<MovementModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.sizeEnabled) {
			if (auto* module = ps->GetModule<SizeModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.trailCSEnabled) {
			if (auto* module = ps->GetModule<TrailModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.meshSpawnEnabled) {
			if (auto* module = ps->GetModule<MeshSpawnModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.meshColorEnabled) {
			if (auto* module = ps->GetModule<MeshColorModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.meshMovementEnabled) {
			if (auto* module = ps->GetModule<MeshMovementModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.meshSizeEnabled) {
			if (auto* module = ps->GetModule<MeshSizeModuleCS>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.trailGenerateEnable) {
			if (auto* module = ps->GetModule<TrailGenerateModule>()) {
				module->SetEnabled(true);
			}
		}

		// RenderModule도 동일하게
		if (psConfig.moduleConfig.billboardEnabled) {
			if (auto* module = ps->GetRenderModule<BillboardModuleGPU>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.meshEnabled) {
			if (auto* module = ps->GetRenderModule<MeshModuleGPU>()) {
				module->SetEnabled(true);
			}
		}

		if (psConfig.moduleConfig.trailEnable) {
			if (auto* module = ps->GetRenderModule<TrailRenderModule>()) {
				module->SetEnabled(true);
			}
		}
	}

	auto& particleSystemsd = effect->GetAllParticleSystems();
	std::cout << "=== ParticleSystem Order Check ===" << std::endl;
	for (int i = 0; i < particleSystemsd.size(); ++i) {
		std::cout << "ParticleSystem[" << i << "] name: " << particleSystemsd[i]->m_name << std::endl;
		if (i < templateConfig.particleSystemConfigs.size()) {
			const auto& psConfig = templateConfig.particleSystemConfigs[i];
			std::cout << "  Config[" << i << "] - spawn:" << psConfig.moduleConfig.spawnEnabled
				<< ", billboard:" << psConfig.moduleConfig.billboardEnabled << std::endl;
		}
	}
	std::cout << "=================================" << std::endl;
}