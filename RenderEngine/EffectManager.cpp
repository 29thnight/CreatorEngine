#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "ImGuiRegister.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "EffectProxyController.h"
#include "EffectSerializer.h"

void EffectManager::Initialize()
{
	std::filesystem::path effectPath = PathFinder::Relative("Effect\\");

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
	for (auto& [key, effect] : activeEffects) {
		effect->Render(scene, camera);
	}
}

void EffectManager::Update(float delta)
{
	auto it = activeEffects.begin();
	while (it != activeEffects.end()) {
		auto& effect = it->second;
		effect->Update(delta);

		// Ǯ ��ȯ ������ �� �����ϰ� ����
		bool shouldReturn = false;

		if (effect->GetState() == EffectState::Stopped) {
			shouldReturn = true;
		}
		// ���� ����Ʈ�� ���� ����ǰ� ������ ���� ���� (������)
		//else if (effect->GetDuration() < 0 && effect->GetCurrentTime() > 60.0f) {
		//	effect->Stop();
		//	shouldReturn = true;
		//}

		if (shouldReturn) {
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);

			// GPU �۾� �Ϸ� ��� �� Ǯ�� ��ȯ
			effectToReturn->WaitForGPUCompletion();
			ReturnToPool(std::move(effectToReturn));
		}
		else {
			++it;
		}
	}
}

std::string EffectManager::PlayEffect(const std::string& templateName)
{
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
	// Ǯ�� ��������� ���� ����
	if (universalPool.empty()) {
		std::cout << "Pool empty, creating new effect instance" << std::endl;
		auto newEffect = CreateUniversalEffect();
		if (newEffect) {
			return newEffect;
		}
		return nullptr;
	}

	auto instance = std::move(universalPool.front());
	universalPool.pop();

	// ���� �غ� ���� Ȯ�� (D3D ȣ�� ����)
	if (!instance->IsReadyForReuse()) {
		std::cerr << "Warning: Pool instance not ready for reuse!" << std::endl;
		// ������ �� �ν��Ͻ� ����
		return CreateUniversalEffect();
	}

	std::cout << "Acquired from pool. Remaining pool size: " << universalPool.size() << std::endl;
	return instance;
}

void EffectManager::ReturnToPool(std::unique_ptr<EffectBase> effect)
{
	if (!effect) return;

	// 1. ���� ������ (D3D ȣ�� ����)
	if (effect->GetState() != EffectState::Stopped) {
		effect->Stop();
	}

	// 2. ���� ���¸� (������ ����)
	effect->ResetForReuse();

	// 3. �ٷ� Ǯ�� ��ȯ (GPU ��� ����)
	if (effect->IsReadyForReuse()) {
		universalPool.push(std::move(effect));
		std::cout << "Effect returned to pool. Pool size: " << universalPool.size() << std::endl;
	}
	else {
		std::cerr << "Warning: Effect not ready for reuse" << std::endl;
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

std::unique_ptr<EffectBase> EffectManager::CreateUniversalEffect()
{
	auto effect = std::make_unique<EffectBase>();

	// ParticleSystem ���� (�ִ� ����)
	auto particleSystem = std::make_shared<ParticleSystem>(
		MAX_PARTICLES_PER_SYSTEM,
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

	auto meshSpawnModule = particleSystem->AddModule<MeshSpawnModuleCS>();
	meshSpawnModule->Initialize();
	meshSpawnModule->SetEnabled(false);

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
							else if (moduleType == "TrailGenerateModule") {
								psConfig.moduleConfig.trailGenerateEnable = true;
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
		for (const auto& ps : deserializedEffect->GetAllParticleSystems()) {
			effect->AddParticleSystem(ps);
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

		if (psConfig.moduleConfig.meshSpawnEnabled) {
			if (auto* module = ps->GetModule<MeshSpawnModuleCS>()) {
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
}