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

		// 종료된 이펙트는 자동으로 풀에 반환
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

std::string EffectManager::PlayEffect(const std::string& templateName, const Mathf::Vector3& position)
{
	auto templateIt = templates.find(templateName);
	if (templateIt == templates.end()) {
		return "";
	}

	// 풀에서 인스턴스 가져오기
	auto instance = AcquireFromPool();
	if (!instance) {
		std::cerr << "Pool exhausted! Cannot play effect: " << templateName << std::endl;
		return "";
	}

	// 템플릿 설정 적용
	ConfigureInstance(instance.get(), templateIt->second);

	// 고유 인스턴스 ID 생성
	std::string instanceId = templateName + "_" + std::to_string(nextInstanceId++);

	// 위치 설정 및 재생 시작
	instance->SetPosition(position);
	instance->Play();

	// 활성 이펙트 목록에 추가
	activeEffects[instanceId] = std::move(instance);

	return instanceId;
}

EffectBase* EffectManager::GetEffectInstance(const std::string& instanceId)
{
	auto it = activeEffects.find(instanceId);
	return (it != activeEffects.end()) ? it->second.get() : nullptr;
}


EffectBase* EffectManager::GetEffect(std::string_view name)
{
	auto it = activeEffects.find(name.data());
	if (it != activeEffects.end()) {
		return it->second.get();
	}
	return nullptr;
}

bool EffectManager::RemoveEffect(std::string_view name)
{
	return activeEffects.erase(name.data()) > 0;
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
		ParticleDataType::Mesh  // 더 큰 타입으로 설정
	);

	// 모든 ParticleModule 추가 (비활성화 상태)
	auto spawnModule = particleSystem->AddModule<SpawnModuleCS>();
	spawnModule->SetEnabled(false);

	auto colorModule = particleSystem->AddModule<ColorModuleCS>();
	colorModule->SetEnabled(false);

	auto movementModule = particleSystem->AddModule<MovementModuleCS>();
	movementModule->SetEnabled(false);

	auto sizeModule = particleSystem->AddModule<SizeModuleCS>();
	sizeModule->SetEnabled(false);

	auto billboardModule = particleSystem->AddRenderModule<BillboardModuleGPU>();
	billboardModule->SetEnabled(false);

	auto meshModule = particleSystem->AddRenderModule<MeshModuleGPU>();
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
	instance->ResetForReuse();
	return instance;
}

void EffectManager::ReturnToPool(std::unique_ptr<EffectBase> effect)
{
	if (effect && effect->IsReadyForReuse()) {
		effect->WaitForGPUCompletion();

		// 모든 모듈 비활성화 (다음 사용을 위해)
		DisableAllModules(effect.get());

		universalPool.push(std::move(effect));
	}
}

void EffectManager::ConfigureInstance(EffectBase* effect, const UniversalEffectTemplate& templateConfig)
{
	auto& particleSystems = effect->GetAllParticleSystems();
	if (particleSystems.empty()) return;

	auto& ps = particleSystems[0];  // 첫 번째 ParticleSystem 사용

	// 파티클 시스템 설정
	ps->ResizeParticleSystem(templateConfig.maxParticles);
	ps->SetParticleDatatype(templateConfig.dataType);

	// ParticleModule 활성화/비활성화
	if (auto* spawnModule = ps->GetModule<SpawnModuleCS>()) {
		spawnModule->SetEnabled(templateConfig.moduleConfig.spawnEnabled);
		if (templateConfig.moduleConfig.spawnEnabled)
			spawnModule->Initialize();
	}

	if (auto* colorModule = ps->GetModule<ColorModuleCS>()) {
		colorModule->SetEnabled(templateConfig.moduleConfig.colorEnabled);
		if (templateConfig.moduleConfig.colorEnabled)
			colorModule->Initialize();
	}

	if (auto* movementModule = ps->GetModule<MovementModuleCS>()) {
		movementModule->SetEnabled(templateConfig.moduleConfig.movementEnabled);
		if (templateConfig.moduleConfig.movementEnabled)
			movementModule->Initialize();
	}

	if (auto* sizeModule = ps->GetModule<SizeModuleCS>()) {
		sizeModule->SetEnabled(templateConfig.moduleConfig.sizeEnabled);
		if (templateConfig.moduleConfig.sizeEnabled)
			sizeModule->Initialize();
	}

	// RenderModule 활성화/비활성화
	if (auto* billboardModule = ps->GetRenderModule<BillboardModuleGPU>()) {
		billboardModule->SetEnabled(templateConfig.moduleConfig.billboardEnabled);
		if (templateConfig.moduleConfig.billboardEnabled)
			billboardModule->Initialize();
	}

	if (auto* meshModule = ps->GetRenderModule<MeshModuleGPU>()) {
		meshModule->SetEnabled(templateConfig.moduleConfig.meshEnabled);
		if (templateConfig.moduleConfig.meshEnabled)
			meshModule->Initialize();
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

	try {
		// 기존 JSON 구조에서 EffectBase 정보 파싱
		if (effectJson.contains("particleSystems") && effectJson["particleSystems"].is_array()) {
			for (const auto& psJson : effectJson["particleSystems"]) {

				// maxParticles 설정
				if (psJson.contains("maxParticles")) {
					maxParticles = std::max(maxParticles, (int)psJson["maxParticles"]);
				}

				// dataType 설정
				if (psJson.contains("particleDataType")) {
					int dataTypeInt = psJson["particleDataType"];
					dataType = static_cast<ParticleDataType>(dataTypeInt);
				}

				// ParticleModule 체크
				if (psJson.contains("modules") && psJson["modules"].is_array()) {
					for (const auto& moduleJson : psJson["modules"]) {
						if (moduleJson.contains("type")) {
							std::string moduleType = moduleJson["type"];

							// 모듈 타입에 따라 활성화 플래그 설정
							if (moduleType == "SpawnModuleCS") {
								moduleConfig.spawnEnabled = true;
							}
							else if (moduleType == "ColorModuleCS") {
								moduleConfig.colorEnabled = true;
							}
							else if (moduleType == "MovementModuleCS") {
								moduleConfig.movementEnabled = true;
							}
							else if (moduleType == "SizeModuleCS") {
								moduleConfig.sizeEnabled = true;
							}
							else if (moduleType == "MeshSpawnModuleCS") {
								moduleConfig.meshSpawnEnabled = true;
							}
						}
					}
				}

				// RenderModule 체크
				if (psJson.contains("renderModules") && psJson["renderModules"].is_array()) {
					for (const auto& renderModuleJson : psJson["renderModules"]) {
						if (renderModuleJson.contains("type")) {
							std::string renderModuleType = renderModuleJson["type"];

							if (renderModuleType == "BillboardModuleGPU") {
								moduleConfig.billboardEnabled = true;
							}
							else if (renderModuleType == "MeshModuleGPU") {
								moduleConfig.meshEnabled = true;
							}
						}
					}
				}
			}
		}

		// 디버그 출력
		std::cout << "Loaded config - Modules: "
			<< "Spawn:" << moduleConfig.spawnEnabled
			<< " Color:" << moduleConfig.colorEnabled
			<< " Movement:" << moduleConfig.movementEnabled
			<< " Size:" << moduleConfig.sizeEnabled
			<< " Billboard:" << moduleConfig.billboardEnabled
			<< " Mesh:" << moduleConfig.meshEnabled
			<< " MaxParticles:" << maxParticles << std::endl;

	}
	catch (const std::exception& e) {
		std::cerr << "Error parsing effect JSON: " << e.what() << std::endl;

		// 에러 발생시 기본 설정 (최소한 spawn + billboard는 켜둠)
		moduleConfig.spawnEnabled = true;
		moduleConfig.billboardEnabled = true;
	}
}
