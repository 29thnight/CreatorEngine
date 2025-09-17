#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class LoadingController : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(LoadingController)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	class ImageComponent* m_loadingImage{ nullptr };
	class TextComponent* m_loadingText{ nullptr };
};
