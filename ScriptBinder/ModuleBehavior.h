#pragma once
#ifndef _inout
#define _inout
#define _in
#endif

#ifndef interface
#define interface struct
#endif

#include "Component.h"
#include "Scene.h"
#include "ModuleBehavior.generated.h"

struct ICollider;
// 사용자가 새로운 컴포넌트를 추가할 때 유용한 기능을 받기 위한 컴포넌트
class ModuleBehavior : public Component
{
public:
   ReflectModuleBehavior
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(ModuleBehavior)

public:
	virtual void Awake() {};
	virtual void OnEnable() {};
	virtual void Start() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void OnTriggerEnter(const Collision& collider) {};
	virtual void OnTriggerStay(const Collision& collider) {};
	virtual void OnTriggerExit(const Collision& collider) {};
	virtual void OnCollisionEnter(const Collision& collider) {};
	virtual void OnCollisionStay(const Collision& collider) {};
	virtual void OnCollisionExit(const Collision& collider) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};
	virtual void OnDisable() {};
	virtual void OnDestroy() {};

public:
    Core::DelegateHandle m_onEnableEventHandle{};
	Core::DelegateHandle m_startEventHandle{};
	Core::DelegateHandle m_fixedUpdateEventHandle{};
	Core::DelegateHandle m_onTriggerEnterEventHandle{};
	Core::DelegateHandle m_onTriggerStayEventHandle{};
	Core::DelegateHandle m_onTriggerExitEventHandle{};
	Core::DelegateHandle m_onCollisionEnterEventHandle{};
	Core::DelegateHandle m_onCollisionStayEventHandle{};
	Core::DelegateHandle m_onCollisionExitEventHandle{};
	Core::DelegateHandle m_updateEventHandle{};
	Core::DelegateHandle m_lateUpdateEventHandle{};
    Core::DelegateHandle m_onDisableEventHandle{};
    Core::DelegateHandle m_onDestroyEventHandle{};
	[[Property]]
	FileGuid m_scriptGuid{};
	HashedGuid m_scriptTypeID{ type_guid(ModuleBehavior) };
public:
	bool m_isCallStart{ false };
};
