#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "ImGuiRegister.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "EffectProxyController.h"
#include "EffectSerializer.h"

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

	// 스마트 ID 할당 시스템 사용
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
		// ID 재활용 코드 제거 (스마트 할당이 알아서 처리)
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
	return PlayEffect(newTemplateName);
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

	auto meshSpawnModule = particleSystem->AddModule<MeshSpawnModuleCS>();
	meshSpawnModule->Initialize();
	meshSpawnModule->SetEnabled(false);

	// RenderModule도 초기화
	auto billboardModule = particleSystem->AddRenderModule<BillboardModuleGPU>();
	billboardModule->Initialize(); // PSO 생성
	billboardModule->SetEnabled(false);

	auto meshModule = particleSystem->AddRenderModule<MeshModuleGPU>();
	meshModule->Initialize(); // PSO 생성
	meshModule->SetEnabled(false);

	effect->AddParticleSystem(particleSystem);
	return effect;
}

std::unique_ptr<EffectBase> EffectManager::AcquireFromPool()
{
	if (universalPool.empty()) {
		return nullptr;  // 풀 고갈
	}

	auto instance = std::move(universalPool.front());
	universalPool.pop();

	// 재사용을 위한 리셋
	//instance->ResetForReuse();

	return instance;
}

void EffectManager::ReturnToPool(std::unique_ptr<EffectBase> effect)
{
	if (effect && effect->IsReadyForReuse()) {
		// 여기서 정리 작업 수행
		effect->WaitForGPUCompletion();

		// 상태만 초기화 (Stop 호출하지 않음)
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

	// 파티클 시스템 설정
	ps->ResizeParticleSystem(templateConfig.maxParticles);
	ps->SetParticleDatatype(templateConfig.dataType);

	// 모든 모듈 비활성화
	DisableAllModules(effect);

	// ParticleModule 데이터 적용 + 활성화
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


	// RenderModule 데이터 적용 + 활성화
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

void UniversalEffectTemplate::LoadConfigFromJSON(const nlohmann::json& effectJson)
{
	// 기본값으로 초기화
	moduleConfig = ModuleConfig{};
	maxParticles = 1000;
	dataType = ParticleDataType::Standard;

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

		if (effectJson.contains("particleSystems") && effectJson["particleSystems"].is_array()) {
			for (const auto& psJson : effectJson["particleSystems"]) {

				// maxParticles, dataType 설정 (기존 로직)
				if (psJson.contains("maxParticles")) {
					maxParticles = (int)psJson["maxParticles"];
				}
				if (psJson.contains("particleDataType")) {
					dataType = static_cast<ParticleDataType>(psJson["particleDataType"]);
				}

				// 모듈 활성화 플래그 설정 + 모듈 데이터 저장
				if (psJson.contains("modules")) {
					for (const auto& moduleJson : psJson["modules"]) {
						if (moduleJson.contains("type")) {
							std::string moduleType = moduleJson["type"];

							// 활성화 플래그 설정
							if (moduleType == "SpawnModuleCS") {
								moduleConfig.spawnEnabled = true;
								spawnModuleData = moduleJson; // 데이터 저장
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

				// 렌더 모듈도 동일하게
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
