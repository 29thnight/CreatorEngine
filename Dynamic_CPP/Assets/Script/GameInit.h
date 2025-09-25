#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class GameInit : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(GameInit)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	void LoadNextScene();

	class GameManager* m_gameManager{ nullptr };
	class ImageSlideshow* m_slideshowComp{};
	class ImageComponent* m_bootBg{};

	float elapsed{};
	float m_fadeDuration{ 0.3f };
	Mathf::Color4 m_bootBgStartColor{ 1.f, 1.f, 1.f, 1.f };
};
