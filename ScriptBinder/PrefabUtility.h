#pragma once
#include "Core.Minimal.h"

class GameObject;
class PrefabUtility : public Singleton<PrefabUtility>
{
private:
	friend class Singleton;
	PrefabUtility() = default;
	~PrefabUtility() = default;

public:
	Core::Delegate<void, GameObject&> prefabInstanceUpdated;
	Core::Delegate<void, GameObject&> prefabInstanceApplied;
	Core::Delegate<void, GameObject&> prefabInstanceReverted;
	Core::Delegate<void, GameObject&> prefabInstanceUnpacked;

};