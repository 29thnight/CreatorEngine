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

		// ����� ����Ʈ�� �ڵ����� Ǯ�� ��ȯ
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

	// Ǯ���� �ν��Ͻ� ��������
	auto instance = AcquireFromPool();
	if (!instance) {
		std::cerr << "Pool exhausted! Cannot play effect: " << templateName << std::endl;
		return "";
	}

	// ���ø� ���� ����
	ConfigureInstance(instance.get(), templateIt->second);

	uint32_t currentId = nextInstanceId.fetch_add(1);  // �����ϰ� ����
	std::string instanceId = templateName + "_" + std::to_string(currentId);

	// ��ġ ���� �� ��� ����
	instance->Play();

	// Ȱ�� ����Ʈ ��Ͽ� �߰�
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
		// Ǯ�� ��ȯ
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

	// JSON ���� ����
	originalJson = effectJson;

	try {
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
		moduleConfig.spawnEnabled = true;
		moduleConfig.billboardEnabled = true;
	}
}
