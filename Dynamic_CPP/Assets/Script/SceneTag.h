#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "SceneTag.generated.h"

class SceneTag : public ModuleBehavior
{
public:
   ReflectSceneTag
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(SceneTag)
	virtual void Awake() override;

public:
	[[Property]]
	std::string m_sceneTag{};
};
