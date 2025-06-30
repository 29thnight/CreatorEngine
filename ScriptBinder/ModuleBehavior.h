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
	virtual Meta::Type ScriptReflect() 
	{ 
		return {}; 
	};
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
	[[Property]]
	FileGuid	m_scriptGuid{};
	HashedGuid	m_scriptTypeID{ type_guid(ModuleBehavior) };

public:
#pragma region ScriptBinder
	// 스크립트 바인딩을 위한 함수 및 델리게이트 핸들러
	Core::DelegateHandle m_awakeEventHandle{};
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

	void AwakeInvoke()
	{
		if (m_isCallAwake == false)
		{
			Awake();
			m_isCallAwake = true;
		}
	}

	void OnEnableInvoke()
	{
		if(false == IsEnabled()) return;

		if (false == m_isCallOnEnable)
		{
			OnEnable();
			m_isCallOnEnable = true;
		}
	}

	void StartInvoke()
	{
		if (m_isCallStart == false)
		{
			Start();
			m_isCallStart = true;
		}
	}

	void FixedUpdateInvoke(float fixedTick)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		FixedUpdate(fixedTick);
	}

	void OnTriggerEnterInvoke(const Collision& collider)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		OnTriggerEnter(collider);
	}

	void OnTriggerStayInvoke(const Collision& collider)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		OnTriggerStay(collider);
	}

	void OnTriggerExitInvoke(const Collision& collider)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		OnTriggerExit(collider);
	}

	void OnCollisionEnterInvoke(const Collision& collider)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		OnCollisionEnter(collider);
	}

	void OnCollisionStayInvoke(const Collision& collider)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		OnCollisionStay(collider);
	}

	void OnCollisionExitInvoke(const Collision& collider)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		OnCollisionExit(collider);
	}

	void UpdateInvoke(float tick)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		Update(tick);
	}

	void LateUpdateInvoke(float tick)
	{
		if (true == IsDestroyMark() || false == IsEnabled()) return;

		LateUpdate(tick);
	}

	void OnDisableInvoke()
	{
		if (true == IsEnabled()) return;

		if (true == m_isCallOnEnable)
		{
			OnDisable();
			m_isCallOnEnable = false;
		}
	}

	void OnDestroyInvoke()
	{
		if (true != IsDestroyMark()) return;

		if (true == m_isCallAwake)
		{
			OnDestroy();
			m_isCallAwake = false;
		}
	}

#pragma endregion

public:
	bool m_isCallAwake{ false };
	bool m_isCallStart{ false };
	bool m_isCallOnEnable{ false };
};
