#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "IllustrationMove.generated.h"

class IllustrationMove : public ModuleBehavior
{
public:
   ReflectIllustrationMove
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(IllustrationMove)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	[[Property]]
	float m_movingSpeed{};
	[[Property]]
	float m_waitTick{};
	[[Property]]
	float m_baseY{};
	[[Property]]
	float offset{};
private:
	float m_elapsedTime{};
	bool m_active{};
	Mathf::Vector2 pos{};
	class RectTransformComponent* m_movingTarget{ nullptr };
};
