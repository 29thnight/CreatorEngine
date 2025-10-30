#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class MenuKeyObserver : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(MenuKeyObserver)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	bool m_isMenuOpened{ false };
	float m_lastDelta{ 0.f };
	float m_elapsedTime{ 0.f };
	//class GameManager* m_gameManager{ nullptr };
	class GameObject* m_menuCanvasObject{ nullptr };
	class ImageComponent* m_menuImageComponent{ nullptr };
};
