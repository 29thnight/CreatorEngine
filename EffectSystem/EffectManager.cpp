#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "EffectProxyController.h"
#include "EffectSerializer.h"
#include "SceneManager.h"

void EffectManager::Initialize()
{
	std::filesystem::path effectPath = PathFinder::Relative("Effect\\");

	SceneManagers->resourceTrimEvent.AddLambda([this]() {
		EmergencyCleanup();
	});

	// ���丮 ���翩��
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

	// ������ ���� �� ���� ���� ���� ����
	if (!activeEffectList.empty()) {
		for (auto* effect : activeEffectList) {
			effect->Render(scene, camera);
		}
	}
}

void EffectManager::Update(float delta)
{
	// ���� ť ó�� (�� �����Ӹ���)
	ProcessCleanupQueue();

	// ���� ���� �ʿ����� Ȯ��
	if (ShouldForceCleanup()) {
		ForceCleanupOldEffects();
	}

	auto it = activeEffects.begin();
	while (it != activeEffects.end()) {
		auto& effect = it->second;
		effect->Update(delta);

		// Stop ������ ����Ʈ�� ����
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
	// Ȱ�� ����Ʈ ���� üũ
	if (activeEffects.size() >= MAX_ACTIVE_EFFECTS) {
		std::cout << "Cannot create effect: Active effect limit reached ("
			<< MAX_ACTIVE_EFFECTS << ")" << std::endl;
		return "";  // ���� �ź�
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

	// ����Ʈ ID �Ҵ� �ý��� ���
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

	// ������ ���� ID�� ������ ���� ����
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

		// ������ ���� ����Ʈ�� Ǯ�� ��ȯ�ϵ��� ����
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
	bool cleanupOk = cleanupQueue.size() < 50; // ���� ť�� �ʹ� ũ�� �ȵ�

	return sizeOk && activeOk && cleanupOk;
}

void EffectManager::ForceCleanupOldEffects()
{
	if (isCleanupRunning.exchange(true)) {
		return; // �̹� ���� ���̸� ��ŵ
	}

	std::cout << "Starting force cleanup of old effects..." << std::endl;

	auto it = activeEffects.begin();
	int cleanedCount = 0;

	while (it != activeEffects.end() && cleanedCount < 10) { // �� ���� �ִ� 10����
		auto& effect = it->second;

		if (effect->GetState() != EffectState::Stopped) {

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

	// ���� Ǯ ũ�Ⱑ �� ���Ѻ��� ũ�� ���
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

	// Ȱ�� ����Ʈ �޸� ��� (�뷫���� ����)
	totalMemory += activeEffects.size() * sizeof(EffectBase);

	// �� ����Ʈ�� ��ƼŬ �ý��� �޸� ���
	for (const auto& [id, effect] : activeEffects) {
		const auto& particleSystems = effect->GetAllParticleSystems();
		for (const auto& ps : particleSystems) {
			// ��ƼŬ �ý��۴� �뷫���� �޸� ��뷮
			totalMemory += ps->GetMaxParticles() * 256; // ��ƼŬ�� �뷫 256����Ʈ ����
		}
	}

	// Ǯ �޸� ���
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		totalMemory += universalPool.size() * sizeof(EffectBase);
		totalMemory += universalPool.size() * MAX_PARTICLES_PER_SYSTEM * 256; // Ǯ�� �� ����Ʈ
	}

	// ���� ť �޸�
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
		// ���� �ν��Ͻ��� �缳�� (����/���� ����)
		auto& effect = it->second;

		// ����Ʈ ����
		effect->Stop();

		// �� ���ø� ���� ����
		ConfigureInstance(effect.get(), templateIt->second);

		// ��� ����
		effect->Play();

		return instanceId; // ���� ID ��ȯ
	}

	// ���� �ν��Ͻ��� ������ ���� ����
	return PlayEffectWithCustomId(newTemplateName, instanceId);
}

uint32_t EffectManager::GetSmartAvailableId(const std::string& templateName)
{
	std::lock_guard<std::mutex> lock(smartIdMutex);

	// ���� Ȱ��ȭ�� ����Ʈ�鿡�� ��� ���� ID ����
	std::set<uint32_t> usedIds;

	for (const auto& [instanceName, effect] : activeEffects) {
		size_t underscorePos = instanceName.find_last_of('_');
		if (underscorePos != std::string::npos) {
			try {
				uint32_t id = std::stoul(instanceName.substr(underscorePos + 1));
				usedIds.insert(id);
			}
			catch (const std::exception&) {
				// �Ľ� ���н� ����
			}
		}
	}

	// ���� ���� ��� ������ ID ã��
	uint32_t availableId = 1;
	while (usedIds.find(availableId) != usedIds.end()) {
		availableId++;
	}

	std::cout << "Smart ID assignment: " << templateName << "_" << availableId << std::endl;
	return availableId;
}

// Ǯ ���� **************************************************************************************************************************************************

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

	// Ǯ�� ��������� ���� ����
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

	if (universalPool.size() < DEFAULT_POOL_SIZE * 0.3f) { // 30% ���Ϸ� ��������
		std::cout << "Pool running low (" << universalPool.size() << "), refilling..." << std::endl;
		RefillPoolAsync();
	}

	// ���� �غ� ���� Ȯ��
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

	// 1. ������ ����
	if (effect->GetState() != EffectState::Stopped) {
		effect->Stop();
	}

	// 2. ������ ����
	effect->ResetForReuse();

	std::lock_guard<std::mutex> lock(poolMutex);

	// 3. Ǯ ũ�� ���� Ȯ��
	if (universalPool.size() >= maxPoolSize) {
		std::cout << "Pool full (" << maxPoolSize << "), destroying effect instance" << std::endl;
		totalDestroyedEffects++;
		return; // effect�� �ڵ����� �Ҹ��
	}

	// 4. Ǯ�� ��ȯ
	if (effect->IsReadyForReuse()) {
		universalPool.push(std::move(effect));
		std::cout << "Effect returned to pool. Pool size: " << universalPool.size() << std::endl;
	}
	else {
		std::cerr << "Warning: Effect not ready for reuse, queueing for cleanup" << std::endl;
		QueueForCleanup(std::move(effect));
	}
}

void EffectManager::RefillPoolAsync()
{
	if (isCleanupRunning.exchange(true)) {
		return;
	}

	std::thread([this]() {
		std::cout << "Starting async pool refill..." << std::endl;

		int refillCount = 0;
		int targetRefill = DEFAULT_POOL_SIZE / 2;

		while (refillCount < targetRefill) {
			auto newEffect = CreateUniversalEffect();
			if (!newEffect) break;

			{
				std::lock_guard<std::mutex> lock(poolMutex);
				if (universalPool.size() >= maxPoolSize) break;
				universalPool.push(std::move(newEffect));
				totalCreatedEffects++;
				refillCount++;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		isCleanupRunning = false;
		std::cout << "Pool refill completed. Added " << refillCount << " instances" << std::endl;
		}).detach();
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

	// ParticleSystem ���� (�ִ� ����)
	auto particleSystem = std::make_shared<ParticleSystem>(
		1,
		ParticleDataType::Mesh
	);

	// ��� ParticleModule �߰� �� �ʱ�ȭ
	auto spawnModule = particleSystem->AddModule<SpawnModuleCS>();
	spawnModule->Initialize(); // PSO ����
	spawnModule->SetEnabled(false); // ��Ȱ��ȭ

	auto colorModule = particleSystem->AddModule<ColorModuleCS>();
	colorModule->Initialize(); // PSO ����
	colorModule->SetEnabled(false);

	auto movementModule = particleSystem->AddModule<MovementModuleCS>();
	movementModule->Initialize(); // PSO ����
	movementModule->SetEnabled(false);

	auto sizeModule = particleSystem->AddModule<SizeModuleCS>();
	sizeModule->Initialize(); // PSO ����
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
	
	// RenderModule�� �ʱ�ȭ
	auto billboardModule = particleSystem->AddRenderModule<BillboardModuleGPU>();
	billboardModule->Initialize(); // PSO ����
	billboardModule->SetEnabled(false);

	auto meshModule = particleSystem->AddRenderModule<MeshModuleGPU>();
	meshModule->Initialize(); // PSO ����
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
		// ParticleModule�� ��Ȱ��ȭ
		for (auto it = ps->GetModuleList().begin(); it != ps->GetModuleList().end(); ++it) {
			ParticleModule& module = *it;
			module.SetEnabled(false);
		}

		// RenderModule�� ��Ȱ��ȭ
		for (auto* renderModule : ps->GetRenderModules()) {
			renderModule->SetEnabled(false);
		}
	}
}

void EffectManager::CleanupAllResources()
{
	std::cout << "Cleaning up all EffectManager resources..." << std::endl;

	// 1. Ȱ�� ����Ʈ�� ����
	activeEffects.clear();

	// 2. Ǯ ����
	{
		std::lock_guard<std::mutex> lock(poolMutex);
		while (!universalPool.empty()) {
			universalPool.pop();
		}
	}

	// 3. ���� ť ����
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

	// �� ���� �ִ� 5������ ó�� (������ ��� ����)
	int processCount = 0;
	while (!cleanupQueue.empty() && processCount < 5) {
		auto effect = std::move(cleanupQueue.front());
		cleanupQueue.pop();
		totalDestroyedEffects++;
		processCount++;
	}

	// ���� ť�� �ʹ� ũ�� �񵿱� ó�� ����
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

	// 1. ���� ����Ʈ�� ������ ����Ʈ �켱 ����
	auto it = activeEffects.begin();
	int emergencyCleanedCount = 0;

	// 1�ܰ�: ���� ����Ʈ ��ü ����
	while (it != activeEffects.end()) {
		auto& effect = it->second;

		if (effect->GetDuration() < 0) { // ���� ����Ʈ�� ��� ����
			effect->Stop();
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);

			// ��޻�Ȳ�̹Ƿ� Ǯ�� ��ȯ���� �ʰ� ��� �Ҹ�
			totalDestroyedEffects++;
			emergencyCleanedCount++;
		}
		else {
			++it;
		}
	}

	// 2�ܰ�: ������ ���ٸ� ������ �Ϲ� ����Ʈ�� ����
	it = activeEffects.begin();
	while (it != activeEffects.end() && emergencyCleanedCount < activeEffects.size() / 2) {
		auto& effect = it->second;

		if (effect->GetCurrentTime() > 10.0f) { // 10�� �̻�� �Ϲ� ����Ʈ
			effect->Stop();
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);

			totalDestroyedEffects++;
			emergencyCleanedCount++;
		}
		else {
			++it;
		}
	}

	// 3. ���� ť�� ���� (Ǯ�� �ǵ帮�� ����)
	{
		std::lock_guard<std::mutex> lock(cleanupQueueMutex);
		int cleanupCount = 0;
		while (!cleanupQueue.empty()) {
			cleanupQueue.pop();
			totalDestroyedEffects++;
			cleanupCount++;
		}
		if (cleanupCount > 0) {
			std::cout << "Cleared " << cleanupCount << " items from cleanup queue" << std::endl;
		}
	}

	std::cout << "EMERGENCY CLEANUP COMPLETED! Cleaned " << emergencyCleanedCount
		<< " active effects (prioritized infinite effects)" << std::endl;
	std::cout << "Pool size preserved: " << GetPoolSize() << "/" << maxPoolSize << std::endl;
}

bool EffectManager::ShouldForceCleanup() const
{
	auto now = std::chrono::steady_clock::now();
	auto timeSinceLastCleanup = std::chrono::duration_cast<std::chrono::seconds>(now - lastCleanupTime).count();
	bool memoryPressure = (activeEffects.size() > MAX_ACTIVE_EFFECTS * 0.8f);
	bool emergencyNeeded = (activeEffects.size() > MAX_ACTIVE_EFFECTS * 0.95f);
	if (emergencyNeeded) {
		Debug->Log("Force Clean Up");
		const_cast<EffectManager*>(this)->EmergencyCleanup();
		return false; // ������� �����Ƿ� �Ϲ� ������ ��ŵ
	}
	return memoryPressure;
}

void EffectManager::ProcessCleanupQueueAsync()
{
// ���� �����忡�� ���� �۾� ����
	std::thread([this]() {
		if (isCleanupRunning.exchange(true)) {
			return; // �̹� ���� ���̸� ����
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

			// effect �Ҹ� (�ð��� �ɸ� �� �ִ� �۾�)
			if (effect) {
				totalDestroyedEffects++;
				std::this_thread::sleep_for(std::chrono::milliseconds(1)); // CPU �纸
			}
		}

		isCleanupRunning = false;
		std::cout << "Async cleanup completed" << std::endl;
		}).detach();
}

void UniversalEffectTemplate::LoadConfigFromJSON(const nlohmann::json& effectJson)
{
	// �⺻������ �ʱ�ȭ
	particleSystemConfigs.clear();
	name = "";
	duration = 1.0f;
	loop = false;

	// JSON ���� ����
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

				// maxParticles, dataType ����
				if (psJson.contains("maxParticles")) {
					psConfig.maxParticles = (int)psJson["maxParticles"];
				}
				if (psJson.contains("particleDataType")) {
					psConfig.dataType = static_cast<ParticleDataType>(psJson["particleDataType"]);
				}

				// ��� Ȱ��ȭ �÷��׸� ����
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

				// ���� ��⵵ �����ϰ�
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
	// EffectSerializer�� ������ ����Ʈ ���� ����
	auto deserializedEffect = EffectSerializer::DeserializeEffect(templateConfig.originalJson);
	if (deserializedEffect) {
		// �⺻ ����Ʈ ���� ����
		effect->SetDuration(deserializedEffect->GetDuration());
		effect->SetLoop(deserializedEffect->IsLooping());
		effect->SetName(deserializedEffect->GetName());
		effect->SetPosition(deserializedEffect->GetPosition());
		effect->SetTimeScale(deserializedEffect->GetTimeScale());

		// ParticleSystem ��ü
		effect->ClearParticleSystems();
		for (size_t i = 0; i < deserializedEffect->GetAllParticleSystems().size(); ++i) {
			const auto& ps = deserializedEffect->GetAllParticleSystems()[i];
			effect->AddParticleSystem(ps);

			// ������ ���� ����
			float delay = deserializedEffect->GetEmitterStartDelay(i);
			effect->SetEmitterStartDelay(i, delay);
		}
	}

	// ��� ParticleSystem�� ���� ��� ��Ȱ��ȭ
	DisableAllModules(effect);

	// �� ParticleSystem���� ���� ���� ����
	auto& particleSystems = effect->GetAllParticleSystems();
	for (int i = 0; i < particleSystems.size() && i < templateConfig.particleSystemConfigs.size(); ++i) {
		auto& ps = particleSystems[i];
		const auto& psConfig = templateConfig.particleSystemConfigs[i];

		// �ش� ParticleSystem�� ��� Ȱ��ȭ
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

		// RenderModule�� �����ϰ�
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