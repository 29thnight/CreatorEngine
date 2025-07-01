#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "SparkleEffect.h"
#include "ImGuiRegister.h"
#include "imgui-node-editor/imgui_node_editor.h"

namespace ed = ax::NodeEditor;

void EffectManager::Initialize()
{

}

void EffectManager::Execute(RenderScene& scene, Camera& camera)
{
	for (auto& [key, effect] : effects) {
		effect->Render(scene, camera);
	}
}

void EffectManager::Update(float delta)
{
	for (auto& [key, effect] : effects) {
		effect->Update(delta);
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
