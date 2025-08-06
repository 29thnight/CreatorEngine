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

		// 풀 반환 조건을 더 관대하게 수정
		bool shouldReturn = false;

		if (effect->GetState() == EffectState::Stopped) {
			shouldReturn = true;
		}
		// 무한 이펙트가 오래 실행되고 있으면 강제 정리 (선택적)
		//else if (effect->GetDuration() < 0 && effect->GetCurrentTime() > 60.0f) {
		//	effect->Stop();
		//	shouldReturn = true;
		//}

		if (shouldReturn) {
			auto effectToReturn = std::move(effect);
			it = activeEffects.erase(it);

			// GPU 작업 완료 대기 후 풀에 반환
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
	// 풀이 비어있으면 새로 생성
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

	// 재사용 준비 상태 확인 (D3D 호출 없이)
	if (!instance->IsReadyForReuse()) {
		std::cerr << "Warning: Pool instance not ready for reuse!" << std::endl;
		// 강제로 새 인스턴스 생성
		return CreateUniversalEffect();
	}

	std::cout << "Acquired from pool. Remaining pool size: " << universalPool.size() << std::endl;
	return instance;
}

void EffectManager::ReturnToPool(std::unique_ptr<EffectBase> effect)
{
	if (!effect) return;

	// 1. 논리적 정리만 (D3D 호출 없음)
	if (effect->GetState() != EffectState::Stopped) {
		effect->Stop();
	}

	// 2. 논리적 리셋만 (스레드 안전)
	effect->ResetForReuse();

	// 3. 바로 풀에 반환 (GPU 대기 없음)
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
							else if (moduleType == "TrailGenerateModule") {
								psConfig.moduleConfig.trailGenerateEnable = true;
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
		for (const auto& ps : deserializedEffect->GetAllParticleSystems()) {
			effect->AddParticleSystem(ps);
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
}