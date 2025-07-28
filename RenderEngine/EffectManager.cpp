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
		// ID ��Ȱ�� �ڵ� ���� (����Ʈ �Ҵ��� �˾Ƽ� ó��)
		auto effectToReturn = std::move(it->second);
		activeEffects.erase(it);
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
	return PlayEffect(newTemplateName);
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

	// RenderModule�� �ʱ�ȭ
	auto billboardModule = particleSystem->AddRenderModule<BillboardModuleGPU>();
	billboardModule->Initialize(); // PSO ����
	billboardModule->SetEnabled(false);

	auto meshModule = particleSystem->AddRenderModule<MeshModuleGPU>();
	meshModule->Initialize(); // PSO ����
	meshModule->SetEnabled(false);

	effect->AddParticleSystem(particleSystem);
	return effect;
}

std::unique_ptr<EffectBase> EffectManager::AcquireFromPool()
{
	if (universalPool.empty()) {
		return nullptr;  // Ǯ ��
	}

	auto instance = std::move(universalPool.front());
	universalPool.pop();

	// ������ ���� ����
	//instance->ResetForReuse();

	return instance;
}

void EffectManager::ReturnToPool(std::unique_ptr<EffectBase> effect)
{
	if (effect && effect->IsReadyForReuse()) {
		// ���⼭ ���� �۾� ����
		effect->WaitForGPUCompletion();

		// ���¸� �ʱ�ȭ (Stop ȣ������ ����)
		effect->ResetForReuse();

		universalPool.push(std::move(effect));
	}
}

void EffectManager::ConfigureInstance(EffectBase* effect, const UniversalEffectTemplate& templateConfig)
{
	effect->SetDuration(templateConfig.duration);
	effect->SetLoop(templateConfig.loop);
	effect->SetName(templateConfig.name);

	auto& ps = effect->GetAllParticleSystems()[0];

	// ��ƼŬ �ý��� ����
	ps->ResizeParticleSystem(templateConfig.maxParticles);
	ps->SetParticleDatatype(templateConfig.dataType);

	// ��� ��� ��Ȱ��ȭ
	DisableAllModules(effect);

	// ParticleModule ������ ���� + Ȱ��ȭ
	if (templateConfig.moduleConfig.spawnEnabled) {
		if (auto* module = ps->GetModule<SpawnModuleCS>()) {
			if (!templateConfig.spawnModuleData.empty() && templateConfig.spawnModuleData.contains("data")) {
				module->DeserializeData(templateConfig.spawnModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}

	if (templateConfig.moduleConfig.colorEnabled) {
		if (auto* module = ps->GetModule<ColorModuleCS>()) {
			if (!templateConfig.colorModuleData.empty() && templateConfig.colorModuleData.contains("data")) {
				//module->DeserializeData(templateConfig.colorModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}

	if (templateConfig.moduleConfig.movementEnabled) {
		if (auto* module = ps->GetModule<MovementModuleCS>()) {
			if (!templateConfig.movementModuleData.empty() && templateConfig.movementModuleData.contains("data")) {
				//module->DeserializeData(templateConfig.movementModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}

	if (templateConfig.moduleConfig.sizeEnabled) {
		if (auto* module = ps->GetModule<SizeModuleCS>()) {
			if (!templateConfig.sizeModuleData.empty() && templateConfig.sizeModuleData.contains("data")) {
				//module->DeserializeData(templateConfig.sizeModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}

	if (templateConfig.moduleConfig.meshSpawnEnabled) {
		if (auto* module = ps->GetModule<MeshSpawnModuleCS>()) {
			if (!templateConfig.meshSpawnModuleData.empty() && templateConfig.meshSpawnModuleData.contains("data")) {
				module->DeserializeData(templateConfig.meshSpawnModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}


	// RenderModule ������ ���� + Ȱ��ȭ
	if (templateConfig.moduleConfig.billboardEnabled) {
		if (auto* module = ps->GetRenderModule<BillboardModuleGPU>()) {
			if (!templateConfig.billboardModuleData.empty() && templateConfig.billboardModuleData.contains("data")) {
				module->DeserializeData(templateConfig.billboardModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}

	if (templateConfig.moduleConfig.meshEnabled) {
		if (auto* module = ps->GetRenderModule<MeshModuleGPU>()) {
			if (!templateConfig.meshModuleData.empty() && templateConfig.meshModuleData.contains("data")) {
				module->DeserializeData(templateConfig.meshModuleData["data"]);
			}
			module->SetEnabled(true);
		}
	}
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
	moduleConfig = ModuleConfig{};
	maxParticles = 1000;
	dataType = ParticleDataType::Standard;

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

				// maxParticles, dataType ���� (���� ����)
				if (psJson.contains("maxParticles")) {
					maxParticles = (int)psJson["maxParticles"];
				}
				if (psJson.contains("particleDataType")) {
					dataType = static_cast<ParticleDataType>(psJson["particleDataType"]);
				}

				// ��� Ȱ��ȭ �÷��� ���� + ��� ������ ����
				if (psJson.contains("modules")) {
					for (const auto& moduleJson : psJson["modules"]) {
						if (moduleJson.contains("type")) {
							std::string moduleType = moduleJson["type"];

							// Ȱ��ȭ �÷��� ����
							if (moduleType == "SpawnModuleCS") {
								moduleConfig.spawnEnabled = true;
								spawnModuleData = moduleJson; // ������ ����
							}
							else if (moduleType == "ColorModuleCS") {
								moduleConfig.colorEnabled = true;
								colorModuleData = moduleJson;
							}
							else if (moduleType == "MovementModuleCS") {
								moduleConfig.movementEnabled = true;
								movementModuleData = moduleJson;
							}
							else if (moduleType == "SizeModuleCS") {
								moduleConfig.sizeEnabled = true;
								sizeModuleData = moduleJson;
							}
							else if (moduleType == "MeshSpawnModuleCS")
							{
								moduleConfig.meshSpawnEnabled = true;
								meshSpawnModuleData = moduleJson;
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
								moduleConfig.billboardEnabled = true;
								billboardModuleData = renderModuleJson;
							}
							else if (renderModuleType == "MeshModuleGPU") {
								moduleConfig.meshEnabled = true;
								meshModuleData = renderModuleJson;
							}
						}
					}
				}
			}
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Error parsing effect JSON: " << e.what() << std::endl;
		//moduleConfig.spawnEnabled = true;
		//moduleConfig.billboardEnabled = true;
	}
}
