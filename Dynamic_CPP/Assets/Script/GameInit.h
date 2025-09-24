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
};
