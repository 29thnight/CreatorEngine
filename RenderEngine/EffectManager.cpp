#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "ImGuiRegister.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "EffectProxyController.h"

namespace ed = ax::NodeEditor;

void EffectManager::Initialize()
{

}

void EffectManager::Execute(RenderScene& scene, Camera& camera)
{
	EffectProxyController::GetInstance()->ExecuteEffectCommands();
	for (auto& [key, effect] : effects) {
		effect->Render(scene, camera);
	}
}	

void EffectManager::Update(float delta)
{
	for (auto& [key, effect] : effects) {
		effect->Update(delta);
		std::cout << effect->GetName();
	}
}

EffectBase* EffectManager::GetEffect(std::string_view name)
{
	auto it = effects.find(name.data());
	if (it != effects.end()) {
		return it->second.get();
	}
	return nullptr;
}

bool EffectManager::RemoveEffect(std::string_view name)
{
	return effects.erase(name.data()) > 0;
}

void EffectManager::RegisterCustomEffect(const std::string& name, const std::vector<std::shared_ptr<ParticleSystem>>& emitters)
{
	if (!emitters.empty()) {
		// EffectBase 직접 생성
		auto effect = std::make_unique<EffectBase>();
		effect->SetName(name);

		// 각 에미터 추가
		for (auto& emitter : emitters) {
			effect->AddParticleSystem(emitter);
		}

		effects[name] = std::move(effect);
	}
}

void EffectManager::CreateEffectInstance(const std::string& templateName, const std::string& instanceName)
{
	if(templateName.empty() || instanceName.empty()) {
		return; // 템플릿 이름이나 인스턴스 이름이 비어있으면 아무 작업도 하지 않음
	}

	auto instnaceIt = effects.find(instanceName);
	if (instnaceIt != effects.end()) {
		// 이미 같은 이름의 인스턴스가 존재하면 아무 작업도 하지 않음
		return;
	}

	auto templateIt = effects.find(templateName);
	if (templateIt != effects.end()) {
		// 템플릿을 복사해서 새 인스턴스 생성
		auto newEffect = std::make_unique<EffectBase>(*templateIt->second);
		newEffect->SetName(instanceName);
		effects[instanceName] = std::move(newEffect);
	}
}