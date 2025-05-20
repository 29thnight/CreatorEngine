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

void EffectManager::MakeEffects(Effect type, std::string_view name, Mathf::Vector3 pos, int maxParticle)
{
	switch (type)
	{
	case Effect::Test:
		//effects[name.data()] = std::make_unique<TestEffect>(pos, maxParticle);
		break;
	case Effect::Sparkle:
		effects[name.data()] = std::make_unique<SparkleEffect>(pos, maxParticle);
		break;

	}
}

ParticleSystem* EffectManager::GetEffect(std::string_view name)
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

void EffectManager::InitializeImgui()
{

}
