#include "EffectManager.h"
#include "ShaderSystem.h"
#include "SparkleEffect.h"

void EffectManager::Execute(RenderScene& scene, Camera& camera)
{
	for (auto& [key, effect] : effects) {
		effect->Render(scene, camera);
	}
}

void EffectManager::UpdateEffects(float delta)
{
	for (auto& [key, effect] : effects) {
		effect->Update(delta);
	}
}

void EffectManager::MakeEffects(Effect type, std::string_view name, Mathf::Vector3 pos, int maxParticle)
{
	switch (type)
	{
	//case Effect::Explode:
	//	effects[name.data()] = std::make_unique<FirePass>();
	//	break;
	case Effect::Sparkle:
		effects[name.data()] = std::make_unique<SparkleEffect>(pos, maxParticle);
		break;
	}
}

EffectModules* EffectManager::GetEffect(std::string_view name)
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
