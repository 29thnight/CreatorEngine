#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "LoadingController.generated.h"

class LoadingController : public ModuleBehavior
{
public:
   ReflectLoadingController
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(LoadingController)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	class ImageComponent* m_loadingImage{ nullptr };
	class TextComponent* m_loadingText{ nullptr };

	[[Property]]
	float m_rotateDegree{ 0.5f };
	float m_dotTimer = 0.0f;
	int   m_dotIdx = 0;
};
